#!/usr/bin/env python3
"""Uploader transport tests: loss/retries, ACK identity, failures and packages."""
import asyncio,base64,dataclasses,os,struct,unittest,zlib
import json
from types import SimpleNamespace
from unittest.mock import patch
from pathlib import Path
from ota_upload import Image,Reply,Link,Session,upload,connect_and_upload,main,APP_BASE,SLOT_SIZE,LAYOUT,ENTER,HELLO,BEGIN,DATA,END,INFO

class CliTests(unittest.TestCase):
    def test_device_and_legacy_secret_selection(self):
        image=Path(__file__).resolve().parents[1]/'build/ota/application.sh03'
        device=base64.b64encode(bytes([1])*32).decode()
        legacy=base64.b64encode(bytes([2])*32).decode()
        for secrets,environment,expected in [
            ({'filament_dryer_01__api_key':device},{},device),
            ({'api_encryption_key':legacy},{},legacy),
            ({'filament_dryer_01__api_key':device,'api_encryption_key':legacy},{},device),
            ({'filament_dryer_01__api_key':device},{'SH03_API_KEY':legacy},legacy),
            ({'filament_dryer_01__api_key':'','api_encryption_key':legacy},{},None),
        ]:
            with self.subTest(secrets=list(secrets),env=bool(environment)), \
                    patch.dict(os.environ,environment,clear=True), \
                    patch('sys.argv',['ota_upload.py',str(image)]), \
                    patch('ota_upload.Path.read_text',return_value=json.dumps(secrets)), \
                    patch('ota_upload.connect_and_upload') as transfer, \
                    patch('builtins.print'):
                self.assertEqual(main(),0 if expected else 1)
                if expected:
                    transfer.assert_awaited_once()
                    args,_,key=transfer.call_args.args
                    self.assertEqual(args.host,'filament-dryer-01.local')
                    self.assertEqual(key,expected)
                else: transfer.assert_not_called()

class FakeClient:
    def __init__(self,image):
        self.image=image;self.link=None;self.mode='app';self.busy=False;self.counter=0
        self.received=bytearray();self.drop={};self.seen=[];self.wrong_offset=False;self.old_image=False

    async def execute_service(self,service,data):
        op=data['operation'];seq=data['sequence'];payload=bytes(data['payload']);self.seen.append((op,seq,payload))
        size=0;crc=0;status=0;offset=0
        if op==HELLO:
            if self.mode!='loader': return
            size=SLOT_SIZE;crc=1
        elif op==ENTER:
            if self.busy: status=2
            else: self.mode='loader'
        elif op==BEGIN:
            assert self.mode=='loader'
            assert payload==struct.pack('<4I',APP_BASE,len(self.image.data),self.image.crc,LAYOUT)
            size=len(self.image.data);crc=self.image.crc;offset=len(self.received)
        elif op==DATA:
            assert self.mode=='loader'
            position=struct.unpack_from('<I',payload)[0]
            if position==len(self.received): self.received.extend(payload[4:])
            else: assert self.received[position:position+len(payload)-4]==payload[4:]
            offset=len(self.received)+(2 if self.wrong_offset else 0);size=len(self.image.data);crc=self.image.crc
        elif op==END:
            assert self.received==self.image.data;self.mode='app'
        elif op==INFO:
            if self.mode!='app': return
            size=len(self.image.data);crc=self.image.crc^(1 if self.old_image else 0)
        else: raise AssertionError(op)
        if self.drop.get(op,0): self.drop[op]-=1;return
        self.counter+=1
        # Wrong-sequence / wrong-operation stale ACKs must be ignored.
        self.link.receive(f'{self.counter},{(seq+1)&65535},{op},0,0,0,0,{LAYOUT}')
        self.link.receive(f'{self.counter},{seq},99,0,0,0,0,{LAYOUT}')
        self.link.receive(f'{self.counter},{seq},{op},{status},{offset},{size},{crc},{LAYOUT}')

class FastLink(Link):
    async def request(self,operation,payload=b'',timeout=2,retries=3):
        # Leave room for event-loop scheduling under concurrent compiler load.
        return await super().request(operation,payload,timeout=0.05,retries=retries)

class RecoveryClient(FakeClient):
    def __init__(self,image,boot_id,holding=False):
        super().__init__(image)
        self.boot_id=boot_id;self.holding=holding;self.mode='loader' if holding else 'app'
        self.disconnected=asyncio.Event();self.commands=[];self.persist_ok=True;self.closed=False;self.released=False
    async def execute_service(self,service,data,**kwargs):
        if service=='recovery':
            command=data['command'];self.commands.append(command)
            assert kwargs=={'return_response':True,'timeout':5}
            if command==1 and self.persist_ok:self.disconnected.set()
            if command==2:self.holding=False
            return SimpleNamespace(success=self.persist_ok,response_data=json.dumps({'boot_id':self.boot_id,'holding':self.holding}).encode())
        if service=='release':self.released=True;return
        await super().execute_service(service,data)
    async def disconnect(self):self.closed=True
    def session(self):
        self.link=FastLink(self,'packet')
        return Session(self,{'sh03_ota_recovery':'recovery','sh03_ota_release':'release'},self.link,self.disconnected)

class Tests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.blob=(Path(__file__).resolve().parents[1]/'build/ota/application.sh03').read_bytes()
        self.image=Image.parse(self.blob)

    def bridge(self):
        client=FakeClient(self.image);link=FastLink(client,object());client.link=link;return client,link

    async def test_upload_lost_acks(self):
        client,link=self.bridge();client.drop={ENTER:1,BEGIN:1,DATA:1,END:1}
        messages=[];await upload(link,self.image,progress=messages.append)
        self.assertEqual(client.received,self.image.data);self.assertTrue(messages[-1].startswith('OK:'))
        for op in (BEGIN,DATA):
            requests=[r for r in client.seen if r[0]==op];self.assertEqual(requests[0],requests[1])

    async def test_busy(self):
        client,link=self.bridge();client.busy=True
        with self.assertRaisesRegex(RuntimeError,'stop BOTH'): await upload(link,self.image,progress=lambda _:None)
        self.assertFalse(any(op==BEGIN for op,_,_ in client.seen))

    async def test_bad_ack(self):
        client,link=self.bridge();client.wrong_offset=True
        with self.assertRaisesRegex(RuntimeError,'DATA acknowledgement'): await upload(link,self.image,progress=lambda _:None)
        self.assertFalse(any(op==END for op,_,_ in client.seen))

    async def test_wrong_app(self):
        client,link=self.bridge();client.old_image=True
        with self.assertRaisesRegex(RuntimeError,'does not match'): await upload(link,self.image,progress=lambda _:None)

    async def test_recovery_waits_for_loader(self):
        client,link=self.bridge();client.busy=True
        send=client.execute_service;hellos=0
        async def manual_power_cycle(service,data):
            nonlocal hellos
            if data['operation']==HELLO:
                hellos+=1
                # First poll goes unanswered by the stuck app. Simulate the
                # user cycling SH03 power before the next poll reaches it.
                if hellos==2: client.mode='loader'
            await send(service,data)
        client.execute_service=manual_power_cycle
        await upload(link,self.image,recovery=True,progress=lambda _:None)
        self.assertGreaterEqual(hellos,2);self.assertEqual(client.received,self.image.data)
        self.assertFalse(any(op==ENTER for op,_,_ in client.seen))

    async def test_both_boards_restart_and_wifi_reconnects(self):
        old=RecoveryClient(self.image,10);new=RecoveryClient(self.image,11,holding=True)
        with patch('ota_upload.open_session',side_effect=[old.session(),OSError('ESP32 still off'),new.session()]) as opened:
            await connect_and_upload(SimpleNamespace(recover=True),self.image,'test-key')
        self.assertEqual(opened.await_count,3)
        self.assertEqual(old.commands,[1]);self.assertTrue(old.closed);self.assertFalse(old.released)
        self.assertEqual(new.commands,[0,2]);self.assertTrue(new.closed and new.released)
        self.assertEqual(new.received,self.image.data)
        self.assertFalse(any(op==ENTER for op,_,_ in new.seen))

    async def test_wifi_outage_is_not_a_power_cycle(self):
        old=RecoveryClient(self.image,10);wifi=RecoveryClient(self.image,10);new=RecoveryClient(self.image,11,holding=True)
        wifi.disconnected.set() # A later real power cycle follows this Wi-Fi-only reconnect.
        with patch('ota_upload.open_session',side_effect=[old.session(),wifi.session(),new.session()]):
            await connect_and_upload(SimpleNamespace(recover=True),self.image,'test-key')
        self.assertEqual(wifi.commands,[0]);self.assertFalse(wifi.seen)
        self.assertEqual(new.received,self.image.data)

    async def test_recovery_marker_failure_stops_before_power_cycle(self):
        old=RecoveryClient(self.image,10);old.persist_ok=False
        with patch('ota_upload.open_session',return_value=old.session()) as opened:
            with self.assertRaisesRegex(RuntimeError,'did not confirm recovery'):
                await connect_and_upload(SimpleNamespace(recover=True),self.image,'test-key')
        self.assertEqual(opened.await_count,1);self.assertFalse(old.seen);self.assertTrue(old.closed)

    async def test_recovery_window_expired_after_restart(self):
        old=RecoveryClient(self.image,10);new=RecoveryClient(self.image,11,holding=False)
        with patch('ota_upload.open_session',side_effect=[old.session(),new.session()]):
            with self.assertRaisesRegex(RuntimeError,'without an active recovery window'):
                await connect_and_upload(SimpleNamespace(recover=True),self.image,'test-key')
        self.assertFalse(new.seen);self.assertEqual(new.commands,[0,2]);self.assertTrue(new.closed)

    async def test_session_resume(self):
        client,link=self.bridge();client.mode='loader';client.received.extend(self.image.data[:600])
        await upload(link,self.image,progress=lambda _:None)
        first=next(p for op,_,p in client.seen if op==DATA)
        self.assertEqual(struct.unpack_from('<I',first)[0],600)

    async def test_timeout_and_layout(self):
        client,link=self.bridge()
        with self.assertRaises(asyncio.TimeoutError): await link.request(HELLO,retries=2)
        self.assertEqual(len(client.seen),2)
        async def wrong(service,data): link.receive(f'1,{data["sequence"]},{data["operation"]},0,0,0,0,123')
        client.execute_service=wrong
        with self.assertRaisesRegex(RuntimeError,'layout'): await link.request(HELLO)

    def test_packages(self):
        self.assertEqual(zlib.crc32(self.image.data),self.image.crc)
        for blob in (b'',self.blob[:-1],self.blob+b'x',self.blob[32:],self.blob[:100]+bytes([self.blob[100]^1])+self.blob[101:]):
            with self.assertRaises(ValueError): Image.parse(blob)
        for index in range(32):
            blob=bytearray(self.blob);blob[index]^=1
            with self.assertRaises(ValueError): Image.parse(blob)
        # Even if its checksums are recomputed, a base-address vector fails.
        blob=bytearray(self.blob);struct.pack_into('<I',blob,36,0x08000001)
        struct.pack_into('<I',blob,16,zlib.crc32(blob[32:]));struct.pack_into('<I',blob,24,zlib.crc32(blob[:24]))
        with self.assertRaisesRegex(ValueError,'vectors'): Image.parse(blob)
        for value in ('','a,b,c','1,2,3','-1,0,0,0,0,0,0,0','1,65536,0,0,0,0,0,0'):
            self.assertIsNone(Reply.parse(value))

if __name__=='__main__':unittest.main()
