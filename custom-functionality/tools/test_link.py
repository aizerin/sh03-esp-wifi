#!/usr/bin/env python3
"""Execute the built ARM UART IRQ, protocol and remote commands with real RTOS
queues/semaphores. MMIO and task scheduling are scripted, not a board test.
"""
import binascii, hashlib, json, struct
from test_system import System, ROOT, SYMBOLS, BLOB, BUFFER

UART=0x40004c00
UI=0x20000370

def frame(kind,sequence,payload=b''):
    body=bytes([1,kind])+struct.pack('<H',sequence)+bytes([len(payload)])+payload
    return b'\xa5\x5a'+body+struct.pack('<H',binascii.crc_hqx(body,0xffff))

def frames(data):
    result=[]
    while data:
        assert data[:3]==b'\xa5\x5a\x01',data.hex()
        n=data[6]+9; f=data[:n]; assert len(f)==n
        assert struct.unpack('<H',f[-2:])[0]==binascii.crc_hqx(f[2:-2],0xffff)
        result.append((f[3],struct.unpack_from('<H',f,4)[0],f[7:-2]));data=data[n:]
    return result

class Link(System):
    def __init__(self):
        super().__init__()
        self.put32(SYMBOLS['uxCriticalNesting'],0)
        self.ticks(5000)
        # Keep an interactive task ready while the drying task is suspended.
        self.uc.mem_write(BUFFER,b'ui-test\0')
        assert self.call('xTaskCreate',SYMBOLS['sh03_task_interactive']|1,BUFFER,256,0,5,BUFFER+64)==1
        self.call('sh03_ui_initialize',UI,2)
        self.put32(0x2000033c,self.call('xQueueCreateMutex',1))
        for ch in range(2):
            self.u8(0x2000002c+2*ch,10)
            self.uc.mem_write(0x200003b4+20*ch,struct.pack('<fiI iI',50.,50,30,65,4500))
            self.put32(0x20000364+4*ch,self.call('xQueueGenericCreate',1,16,0))
            self.put32(0x20000324+4*ch,self.call('xQueueGenericCreate',1,0,3))
        self.call('sh03_link_init')
    def ticks(self,value): self.put32(SYMBOLS['xTickCount'],value)
    def u8(self,address,value): self.uc.mem_write(address,bytes([value]))
    def byte(self,address): return self.uc.mem_read(address,1)[0]
    def feed(self,data):
        for byte in data:
            self.put32(UART,0x20);self.put32(UART+4,byte);self.call('sh03_uart4_irq')
        self.put32(UART,0x80)
    def drain(self):
        for _ in range(512):
            if not self.get32(UART+12)&0x80:break
            self.call('sh03_uart4_irq')
        else:raise AssertionError('TX failed to drain')
        out=frames(bytes(self.uart));self.uart.clear();return out
    def command(self,ch,op,value,seq=1):
        self.feed(frame(2,seq,bytes([ch,op])+struct.pack('<I',value)))
        self.call('sh03_link_poll');out=self.drain()
        ack=[p for kind,s,p in out if kind==3 and s==seq]
        assert len(ack)==1,out
        assert ack[0][:2]==bytes([ch,op])
        return ack[0][2],out
    def suspended_task(self,ch):
        self.uc.mem_write(BUFFER,b'dryer-test\0')
        assert self.call('xTaskCreate',SYMBOLS['sh03_task_drying']|1,BUFFER,256,0,6,BUFFER+64)==1
        task=self.get32(BUFFER+64);self.put32(0x200004ec+4*ch,task)
        self.call('vTaskSuspend',task)
        self.call('xQueueGenericSend',self.get32(0x20000324+4*ch),0,0,0)
        return task

def main(directory=None):
    checks=0
    # Every command passes through bytes, the actual UART ISR, CRC parser,
    # dispatcher, queue/semaphore API and TX ISR, not a reimplementation.
    for ch in (0,1):
        for op,value,offset,expected in [(2,70,4,70),(3,14400,8,14400),(5,35,2,35),(6,1,3,1)]:
            m=Link();status,out=m.command(ch,op,value)
            assert status==0
            address=UI+4+16*ch+offset
            assert (m.get32(address) if offset==8 else m.byte(address))==expected
            assert len([p for k,s,p in out if k==1])==2
            checks+=1
        m=Link();assert m.command(ch,4,1)[0]==0
        assert m.byte(0x2000001c+8*ch)==1
        assert m.command(ch,3,14400,2)[0]==5;checks+=2
    for ch,op,value in [(2,1,1),(0,1,2),(0,2,44),(0,2,86),(0,2,0xffffffff),
                         (0,3,0),(0,3,352801),(0,3,7201),(0,4,2),(0,5,15),(0,5,21),(0,6,10),(0,255,0)]:
        m=Link();before=bytes(m.uc.mem_read(UI,36));status,_=m.command(ch,op,value)
        assert status in (1,5);assert bytes(m.uc.mem_read(UI,36))==before;checks+=1
    for fault in range(10):
        m=Link();m.u8(0x2000002c,fault)
        assert m.command(0,1,1)[0]==3 and m.get32(0x200004ec)==0;checks+=1
    m=Link();m.ticks(1000);assert m.command(0,1,1)[0]==4;checks+=1
    m=Link();m.u8(0x20000018,1);assert m.command(0,2,60)[0]==2;checks+=1
    m=Link();m.u8(0x20000318,1);assert m.command(0,1,1)[0]==2;checks+=1
    m=Link();m.put32(0x200003bc,0);assert m.command(0,1,1)[0]==3;checks+=1
    m=Link();assert m.command(0,1,1)[0]==0
    assert m.get32(0x200004ec)!=0
    assert m.command(0,2,70,2)[0]==2;checks+=2
    # Existing stopped tasks restart via a RESET message with new settings.
    m=Link();task=m.suspended_task(0)
    assert m.command(0,2,70)[0]==0
    assert m.command(0,1,1,2)[0]==0
    queue=m.get32(0x20000364)
    assert m.call('xQueueReceive',queue,BUFFER,0)==1
    message=bytes(m.uc.mem_read(BUFFER,16));assert message[1]==2 and message[5]==70
    assert m.call('eTaskGetState',task)!=3;checks+=3
    # STOP remains available in fault/edit mode; queue failure is not ACKed OK.
    for full in (False,True):
        m=Link();task=m.suspended_task(0);m.call('vTaskResume',task)
        m.u8(0x2000001d,1);m.u8(0x2000002c,4);m.u8(0x20000018,1)
        queue=m.get32(0x20000364)
        if full: assert m.call('xQueueGenericSend',queue,BUFFER,0,0)==1
        assert m.command(0,1,0)[0]==(2 if full else 0)
        assert m.byte(0x2000002c)==4
        assert m.call('xQueueReceive',queue,BUFFER,0)==1
        if not full: assert m.byte(BUFFER+1)==0
        else: assert m.call('xQueueSemaphoreTake',m.get32(0x20000324),0)==1
        checks+=2
    # Duplicate command ACK without overwriting a subsequent panel change.
    m=Link();assert m.command(0,2,70,42)[0]==0
    m.u8(UI+8,65);assert m.command(0,2,70,42)[0]==0
    assert m.byte(UI+8)==65;checks+=1
    valid=frame(2,50,bytes([0,2])+struct.pack('<I',75))
    for invalid in [valid[:-1]+bytes([valid[-1]^1]), frame(2,50,b'\0\2'),
                    b'\xa5\x5a\x01\x02\x32\0\xff', frame(99,50,b'\0\2')]:
        m=Link();m.feed(invalid);m.call('sh03_link_poll');assert m.byte(UI+8)==50
        m.drain();m.ticks(5600);m.call('sh03_link_poll');m.feed(valid);m.call('sh03_link_poll')
        assert m.byte(UI+8)==75;checks+=2
    # Fragmentation, inter-byte timeout, line noise, RX errors and overflow.
    m=Link();m.feed(valid[:6]);m.call('sh03_link_poll');assert m.byte(UI+8)==50
    m.feed(valid[6:]);m.call('sh03_link_poll');assert m.byte(UI+8)==75;checks+=1
    m=Link();m.feed(valid[:6]);m.call('sh03_link_poll');m.ticks(5600)
    m.feed(valid[6:]);m.call('sh03_link_poll');assert m.byte(UI+8)==50;checks+=1
    m=Link();m.feed(b'boot\r\n\xa5'+valid);m.call('sh03_link_poll');assert m.byte(UI+8)==75;checks+=1
    for overflow in (False,True):
        m=Link();m.feed(valid)
        if overflow:m.feed(bytes(256))
        else:m.put32(UART,0x28);m.call('sh03_uart4_irq');m.put32(UART,0x80)
        m.call('sh03_link_poll');assert m.byte(UI+8)==50;checks+=1
    m=Link();m.u8(0x2000001d,1);m.u8(0x20000080,1)
    m.put32(0x20000344+12,7200);m.put32(0x2000069c,7201)
    m.ticks(6000);m.call('sh03_link_poll');out=m.drain()
    state=next(p for k,s,p in out if k==1 and p[0]==0)
    assert state[1]&1 and struct.unpack_from('<I',state,26)[0]==0
    assert struct.unpack_from('<h',state,8)[0]==500;checks+=1
    report={'status':'PASS','candidate_sha256':hashlib.sha256(BLOB).hexdigest(),'checks':checks,
      'scope':'Actual ARM IRQ/parser/commands and real RTOS queues; scripted MMIO, no preemption or physical wiring test'}
    path=(directory or ROOT/'analysis/standalone')/'link-tests.json';path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))

if __name__=='__main__':main()
