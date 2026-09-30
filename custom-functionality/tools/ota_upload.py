#!/usr/bin/env python3
"""Upload application.sh03 over encrypted ESPHome API and UART4.

Install tools/requirements-ota.txt (or esphome/requirements.txt).
Never accepts a raw .bin: only a validated, relocated SH03 package.
"""
import argparse,asyncio,base64,dataclasses,json,os,secrets,struct,sys,time,zlib
from pathlib import Path

APP_BASE=0x08002000
SLOT_SIZE=0x1e000
LAYOUT=0x00010303
ENTER,HELLO,BEGIN,DATA,END,RUN,INFO=0x10,0x11,0x12,0x13,0x14,0x15,0x17
ERRORS={1:'invalid image/packet range',2:'busy: stop BOTH chambers and finish panel editing',
        3:'image CRC/vector validation failed',4:'flash erase/program failed',
        5:'unexpected transfer state',6:'MCU geometry does not match F1 HD 256 KiB'}

@dataclasses.dataclass(frozen=True)
class Image:
    data: bytes
    crc: int

    @classmethod
    def parse(cls,blob):
        if len(blob)<32: raise ValueError('Not a .sh03 package (header missing)')
        magic,version,base,size,crc,layout,hcrc,commit=struct.unpack_from('<8I',blob)
        if (magic,version,base,layout,commit)!=(0x55333053,1,APP_BASE,LAYOUT,0x5aa5a55a):
            raise ValueError('Wrong image format, load address or hardware layout')
        if not 304<=size<=SLOT_SIZE or size%4 or len(blob)!=size+32:
            raise ValueError('Invalid image length')
        if zlib.crc32(blob[:24])!=hcrc or zlib.crc32(blob[32:])!=crc:
            raise ValueError('Package CRC mismatch')
        sp,reset=struct.unpack_from('<II',blob,32)
        if not (0x20000700<sp<=0x20010000 and sp%8==0 and reset&1 and APP_BASE+304<=reset&~1<APP_BASE+size):
            raise ValueError('Invalid relocated application vectors')
        return cls(blob[32:],crc)

@dataclasses.dataclass(frozen=True)
class Reply:
    counter:int
    sequence:int
    operation:int
    status:int
    offset:int
    size:int
    crc:int
    layout:int

    @classmethod
    def parse(cls,text):
        try: values=[int(v) for v in text.split(',')]
        except ValueError: return None
        if len(values)!=8 or any(v<0 or v>0xffffffff for v in values): return None
        if values[1]>65535 or values[2]>255 or values[3]>255: return None
        return cls(*values)

class Link:
    def __init__(self,client,packet_service):
        self.client=client;self.service=packet_service;self.sequence=secrets.randbelow(65536)
        self.queue=asyncio.Queue(maxsize=64)

    def receive(self,text):
        reply=Reply.parse(text)
        if reply:
            if self.queue.full(): self.queue.get_nowait()
            self.queue.put_nowait(reply)

    async def request(self,operation,payload=b'',timeout=2.0,retries=3):
        self.sequence=(self.sequence+1)&65535
        sequence=self.sequence
        # Discard subscription snapshots and stale responses before sending.
        while not self.queue.empty(): self.queue.get_nowait()
        for attempt in range(retries):
            await self.client.execute_service(self.service,{'operation':operation,'sequence':sequence,'payload':list(payload)})
            deadline=asyncio.get_running_loop().time()+timeout
            try:
                while True:
                    remaining=deadline-asyncio.get_running_loop().time()
                    reply=await asyncio.wait_for(self.queue.get(),max(0,remaining))
                    if reply.sequence!=sequence or reply.operation!=operation: continue
                    if reply.layout!=LAYOUT: raise RuntimeError('Wrong bootloader layout/version')
                    if reply.status: raise RuntimeError(ERRORS.get(reply.status,f'MCU error {reply.status}'))
                    return reply
            except asyncio.TimeoutError:
                if attempt+1==retries: raise
        raise AssertionError('unreachable')

async def enter_loader(link,*,recovery=False):
    if recovery:
        deadline=time.monotonic()+30
        while time.monotonic()<deadline:
            try: return await link.request(HELLO,timeout=0.4,retries=1)
            except asyncio.TimeoutError: pass
        raise RuntimeError('Bootloader did not answer after restart. Check TX/RX and install the current factory image via ST-Link.')
    try: return await link.request(HELLO,timeout=0.4,retries=1)
    except asyncio.TimeoutError: pass
    try:
        await link.request(ENTER,timeout=2,retries=1)
    except asyncio.TimeoutError:
        # ACK may be lost during reset. HELLO still has to prove loader entry.
        pass
    try: return await link.request(HELLO,timeout=0.5,retries=5)
    except asyncio.TimeoutError as exc:
        raise RuntimeError('No bootloader response. Install factory.bin once via ST-Link, or use --recover for a stuck application.') from exc

async def upload(link,image,*,recovery=False,progress=print):
    hello=await enter_loader(link,recovery=recovery)
    if hello.size!=SLOT_SIZE or hello.crc!=1: raise RuntimeError('Unsupported bootloader capabilities')
    manifest=struct.pack('<4I',APP_BASE,len(image.data),image.crc,LAYOUT)
    begin=await link.request(BEGIN,manifest,timeout=8)
    if begin.size!=len(image.data) or begin.crc!=image.crc or begin.offset>len(image.data) or begin.offset%2:
        raise RuntimeError('Invalid BEGIN acknowledgement')
    # BEGIN is idempotent; resuming is possible within the same loader session.
    offset=begin.offset;last_progress=-1
    while offset<len(image.data):
        chunk=image.data[offset:offset+60]
        reply=await link.request(DATA,struct.pack('<I',offset)+chunk)
        if reply.offset!=offset+len(chunk) or reply.size!=len(image.data) or reply.crc!=image.crc:
            raise RuntimeError('Invalid DATA acknowledgement')
        offset=reply.offset
        percent=offset*100//len(image.data)
        if percent//5!=last_progress:
            last_progress=percent//5;progress(f'Transfer {percent}% ({offset}/{len(image.data)} bytes)')
    try:
        await link.request(END,timeout=2,retries=1)
    except asyncio.TimeoutError:
        # END may have committed successfully: never resend BEGIN blindly.
        pass
    progress('Image sent. Waiting for installation and application verification…')
    deadline=time.monotonic()+45
    while time.monotonic()<deadline:
        try:
            info=await link.request(INFO,timeout=1,retries=1)
            if info.size!=len(image.data) or info.crc!=image.crc:
                raise RuntimeError('Application is running, but its image does not match this upload')
            progress(f'OK: SH03 application running, CRC32 {image.crc:08x}. Drying remains stopped.')
            return
        except asyncio.TimeoutError: pass
    raise RuntimeError('Application did not confirm installation. Reset SH03 to resume a committed update; use --recover if needed.')

@dataclasses.dataclass
class Session:
    client: object
    actions: dict
    link: Link
    disconnected: asyncio.Event

    async def recovery(self,command):
        action=self.actions.get('sh03_ota_recovery')
        if action is None: raise RuntimeError('Update ESPHome YAML first: shared-power recovery action is missing')
        response=await self.client.execute_service(action,{'command':command},return_response=True,timeout=5)
        if response is None or not response.success:
            raise RuntimeError('ESP32 did not confirm recovery state in flash; do not power-cycle yet')
        try:
            state=json.loads(response.response_data)
            if type(state['boot_id']) is not int or not 0<=state['boot_id']<=0xffffffff or type(state['holding']) is not bool:
                raise ValueError('fields')
        except (ValueError,KeyError,TypeError) as exc:
            raise RuntimeError('Invalid ESP32 recovery response') from exc
        return state

    async def close(self,release=True):
        try:
            if release: await self.client.execute_service(self.actions['sh03_ota_release'],{})
        except Exception: pass
        try: await self.client.disconnect()
        except Exception: pass

async def open_session(args,key):
    from aioesphomeapi import APIClient,TextSensorInfo,TextSensorState
    client=APIClient(args.host,args.port,password=None,noise_psk=key,client_info='SH03 OTA uploader')
    disconnected=asyncio.Event()
    async def stopped(expected): disconnected.set()
    try:
        await client.connect(on_stop=stopped,login=True,log_errors=False)
        entities,services=await client.list_entities_services()
        actions={s.name:s for s in services}
        for name in ('sh03_ota_packet','sh03_ota_release'):
            if name not in actions: raise RuntimeError('ESP32 is missing OTA bridge actions; update its ESPHome YAML first')
        matches=[e for e in entities if isinstance(e,TextSensorInfo) and
                 (e.object_id=='sh03_ota_reply' or e.name=='SH03 OTA reply' or e.name.endswith(' SH03 OTA reply'))]
        if len(matches)!=1: raise RuntimeError('Cannot identify SH03 OTA reply entity')
        link=Link(client,actions['sh03_ota_packet']);reply_key=matches[0].key
        def on_state(state):
            if isinstance(state,TextSensorState) and state.key==reply_key and not state.missing_state: link.receive(state.state)
        client.subscribe_states(on_state)
        return Session(client,actions,link,disconnected)
    except BaseException:
        try: await client.disconnect()
        except Exception: pass
        raise

async def connect_and_upload(args,image,key):
    session=await open_session(args,key)
    try:
        if args.recover:
            before=await session.recovery(1)
            print('Recovery saved in ESP32. Power off/on the dryer and ESP32 together now. Waiting up to 4 minutes for reconnection…',flush=True)
            deadline=time.monotonic()+240
            while True:
                try:
                    await asyncio.wait_for(session.disconnected.wait(),max(0,deadline-time.monotonic()))
                except asyncio.TimeoutError as exc:
                    raise RuntimeError('No shared power-cycle detected; recovery cancelled') from exc
                await session.close(release=False);session=None
                # Both boards are off/rebooting. The ESP32's local UART probe
                # holds SH03 before Wi-Fi returns; no live socket is required.
                while session is None:
                    remaining=deadline-time.monotonic()
                    if remaining<=0: raise RuntimeError('ESP32 did not reconnect. Check power/Wi-Fi, then run --recover again.')
                    try: session=await asyncio.wait_for(open_session(args,key),min(10,remaining))
                    except Exception: await asyncio.sleep(min(1,max(0,deadline-time.monotonic())))
                after=await session.recovery(0)
                if after['boot_id']==before['boot_id']:
                    # A Wi-Fi outage alone is not the requested power-cycle.
                    continue
                if not after['holding']:
                    raise RuntimeError('ESP32 restarted without an active recovery window; run --recover again')
                print('ESP32 reconnected after shared restart. Uploading to the held bootloader…',flush=True)
                break
        await upload(session.link,image,recovery=args.recover)
    finally:
        if session is not None:
            try:
                if args.recover: await session.recovery(2)
            except Exception: pass  # A consumed marker/5-minute hold bounds offline cleanup.
            await session.close()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image',type=Path,help='build/ota/application.sh03 (never factory.bin)')
    parser.add_argument('--host',default='filament-dryer-01.local')
    parser.add_argument('--port',type=int,default=6053)
    parser.add_argument('--secrets',type=Path,default=Path(__file__).resolve().parents[1]/'esphome/secrets.yaml')
    parser.add_argument('--recover',action='store_true',help='Arm ESP32, then power-cycle BOTH boards; reconnect and upload automatically')
    args=parser.parse_args()
    try:
        image=Image.parse(args.image.read_bytes())
        key=os.environ.get('SH03_API_KEY')
        if not key:
            import yaml
            config=yaml.safe_load(args.secrets.read_text())
            key=config.get('filament_dryer_01__api_key',config.get('api_encryption_key')) if isinstance(config,dict) else None
        if not isinstance(key,str) or len(base64.b64decode(key,validate=True))!=32:
            raise ValueError('Provide a valid filament_dryer_01__api_key (legacy: api_encryption_key) in --secrets or SH03_API_KEY')
        print(f'Uploading {len(image.data)} bytes to {args.host}, CRC32 {image.crc:08x}',flush=True)
        asyncio.run(connect_and_upload(args,image,key))
    except KeyboardInterrupt:
        print('\nInterrupted. An uncommitted transfer keeps the previous app; a committed installation resumes after reset.',file=sys.stderr)
        return 130
    except Exception as exc:
        # Never include secrets in command lines/logs. No library tracebacks.
        print(f'Upload failed: {exc}',file=sys.stderr)
        return 1
    return 0

if __name__=='__main__':sys.exit(main())
