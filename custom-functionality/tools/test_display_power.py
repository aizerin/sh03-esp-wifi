#!/usr/bin/env python3
"""ARM UART/display power integration with LCD RAM and command-bit models.

Real display_command builds the serial command. Shift/select GPIO boundaries
are scripted; this validates command bits, not physical controller identity.
"""
from test_remote_display import RemotePanel,UI
from test_link import frame
from test_system import ROOT,BLOB,BUFFER
from unicorn import UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_PC,UC_ARM_REG_LR
import hashlib,json

BUS=0x08010db4

class PowerPanel(RemotePanel):
    def __init__(self):
        super().__init__()
        self.outputs=[True,True]
        self.bits=[[],[]]
        self.commands=[]
        self.backlight=True
        self.gate_events=[]
        self.gpio_writes=[]
        # UI harness skips main(); model its already-enabled PB1 output.
        self.put32(0x40010c0c,0xa55a)
        self.uc.hook_add(UC_HOOK_MEM_WRITE,self.gpio_write,None,0x40010c00,0x40010c1f)
        self.uc.hook_add(UC_HOOK_MEM_WRITE,self.gpio_write,None,0x42218000,0x422183ff)
        self.hook('sh03_display_select',self.select)
        self.hook('sh03_display_shift_out',self.shift)

    def gpio_write(self,uc,access,address,size,value,data):
        self.gpio_writes.append((address,size,value))
        if address==0x42218184:
            assert size==4 and value in (0,1)
            self.backlight=bool(value)
            self.gate_events.append(('PB1',value))
            # Unicorn has no bit-band peripheral; mirror the actual PB1 write.
            self.put32(0x40010c0c,(self.get32(0x40010c0c)&~2)|(value<<1))

    def channel(self,uc):
        offset=uc.reg_read(UC_ARM_REG_R0)-BUS
        assert offset in (0,48),offset
        return offset//48

    def select(self,uc,address,size,data):
        ch=self.channel(uc)
        if uc.reg_read(UC_ARM_REG_R1)==0:self.bits[ch]=[]
        elif self.bits[ch]:
            bits=''.join(map(str,self.bits[ch]));self.bits[ch]=[]
            self.commands.append((ch,bits))
            assert len(bits)==12 and bits[:3]=='100',bits
            if bits=='100000000100':self.outputs[ch]=False
            elif bits=='100000000110':self.outputs[ch]=True
            if bits in ('100000000100','100000000110'):
                self.gate_events.append(('LCD',ch,self.outputs[ch]))
        uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))

    def shift(self,uc,address,size,data):
        ch=self.channel(uc)
        value=uc.reg_read(UC_ARM_REG_R1);count=uc.reg_read(UC_ARM_REG_R2)
        self.bits[ch].extend((value>>(7-i))&1 for i in range(count))
        uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))

    def telemetry_power(self,enabled):
        assert self.backlight==enabled and self.outputs==[enabled,enabled]
        self.feed(frame(4,999));self.ui()
        states=[p for kind,seq,p in self.drain() if kind==1]
        assert len(states)==2,states
        assert all(p[1]&32 and bool(p[1]&16)==enabled for p in states)

    def telemetry_timeout(self,seconds):
        self.feed(frame(4,998));self.ui()
        configs=[p for kind,seq,p in self.drain() if kind==5]
        assert configs==[seconds.to_bytes(2,'little')],configs

    def at(self,tick,enabled):
        # Large idle intervals without replaying unrelated timer callbacks.
        self.now=tick&0xffffffff;self.ui();self.drain()
        assert self.outputs==[enabled,enabled] and self.backlight==enabled,(tick,self.outputs,self.backlight)

def manual_only_tests():
    m=PowerPanel()
    # A stale ESPHome installation reports the retired timer as disabled and
    # cannot turn it back on. Malformed/nonzero values have no side effects.
    m.telemetry_timeout(0)
    m.command(0,8,0)
    for seconds in (1,60,3600,3601,0xffffffff):m.command(1,8,seconds,status=5)
    m.command(2,8,0,status=1)
    m.telemetry_timeout(0)
    for tick in (60000,3600000,0xffffff00,60000):m.at(tick,True)
    m.command(0,7,0)
    for tick in (120000,7200000,0xffffff00,120000):m.at(tick,False)
    m.command(0,8,0);m.telemetry_power(False)
    m.key(1) # Wake without starting edit; the display now stays on.
    assert m.byte(0x20000018)==0
    for tick in (180000,10800000,0xffffff00,180000):m.at(tick,True)
    m.command(0,7,0);m.command(0,7,1)
    m.at(14400000,True)
    print('PASS: no automatic blanking after HA ON or touch wake, including tick wrap; retired timeout cannot be re-enabled')

def main():
    m=PowerPanel()
    assert m.call('sh03_display_enabled')==1
    m.telemetry_power(True)
    m.command(0,2,70);m.command(0,1,1);m.dry(0);m.step()
    m.assert_display([1,0],'before blanking')
    # Switching outputs must not rewrite LCD RAM, settings, task handles or
    # the running controller. Calls go through real UART and remote dispatch.
    before=bytes(m.uc.mem_read(UI,36));ram=[bytes(d) for d in m.display]
    control=bytes(m.uc.mem_read(0x200004fc,416))
    task=m.get32(0x200004ec)
    gpio=m.get32(0x40010c0c)
    pwm=[m.get32(a) for a in (0x40000834,0x40000838,0x40013434,0x40013438)]
    timers=set(m.created_timers);heap=m.call('xPortGetFreeHeapSize')
    m.command(0,7,0)
    assert m.gpio_writes==[(0x42218184,4,0)]
    assert m.get32(0x40010c0c)==gpio&~2
    assert m.gate_events==[('PB1',0),('LCD',0,False),('LCD',1,False)]
    assert [m.get32(a) for a in (0x40000834,0x40000838,0x40013434,0x40013438)]==pwm
    assert m.created_timers==timers and m.call('xPortGetFreeHeapSize')==heap
    assert m.outputs==[False,False] and not m.backlight
    assert m.commands==[(0,'100000000100'),(1,'100000000100')]
    assert bytes(m.uc.mem_read(UI,36))==before and m.display==ram
    assert bytes(m.uc.mem_read(0x200004fc,416))==control and m.get32(0x200004ec)==task
    m.telemetry_power(False)
    for t,h in [(26,41),(27,42),(28,43)]:
        m.sample(0,t,h);m.step()
        m.assert_display([1,0],'LCD RAM remains current while blank')
        assert m.outputs==[False,False]
    assert m.display!=ram and m.byte(0x200004fd)==1
    # Idempotent/duplicate OFF, invalid values and queries do not change it.
    count=len(m.commands);writes=len(m.gpio_writes)
    m.command(0,7,0,sequence=m.sequence);m.command(0,7,0)
    m.command(0,7,2,status=1);m.command(0,7,0xffffffff,status=1)
    assert len(m.commands)==count and m.outputs==[False,False] and len(m.gpio_writes)==writes
    # Original LCD initialization contains LCD ON. It must not undo OFF.
    for ch in (0,1):m.call('sh03_display_bus_initialize',BUS+48*ch)
    assert m.outputs==[False,False]
    assert not any(bits=='100000000110' for ch,bits in m.commands)
    # Both chamber indexes address the same global switch. ON exposes the
    # current RAM immediately without clearing or rebuilding cached glyphs.
    ram=[bytes(d) for d in m.display]
    writes=len(m.gpio_writes) # Bus initialization above also writes LCD clocks.
    m.command(1,7,1)
    assert m.gate_events[-3:]==[('LCD',0,True),('LCD',1,True),('PB1',1)]
    assert m.gpio_writes[writes:]==[(0x42218184,4,1)]
    assert m.get32(0x40010c0c)==gpio
    count=len(m.gate_events);m.command(0,7,1)
    assert len(m.gate_events)==count
    assert m.outputs==[True,True] and m.display==ram
    assert m.commands[-2:]==[(0,'100000000110'),(1,'100000000110')]
    m.telemetry_power(True)
    # Display control stays available during local editing and a latched
    # fault/diagnostic condition; it must not clear or alter those states.
    m=PowerPanel();m.key(1);m.key(2)
    m.uc.mem_write(0x2000002c,b'\x04');m.uc.mem_write(0x20000318,b'\x01')
    before=bytes(m.uc.mem_read(UI,36))
    m.command(0,7,0);m.command(0,7,1)
    m.telemetry_power(True)
    assert m.byte(0x20000018)==2 and m.byte(0x2000002c)==4 and m.byte(0x20000318)==1
    assert bytes(m.uc.mem_read(UI,36))==before
    # Reset restores the normal startup visibility; no flash settings stored.
    m.command(0,7,0)
    fresh=PowerPanel();assert fresh.call('sh03_display_enabled')==1
    # Every recognized short/long key wakes only. The next touch retains its
    # usual function. Wake never toggles diagnostics or sends a drying command.
    for key in range(1,7):
        for duration in (100,2500):
            m=PowerPanel();m.command(0,7,0)
            before=bytes(m.uc.mem_read(UI,36))
            m.put32(BUFFER,key|(duration<<16))
            assert m.call('xQueueGenericSend',m.get32(0x2000036c),BUFFER,0,0)==1
            m.ui()
            assert m.outputs==[True,True] and bytes(m.uc.mem_read(UI,36))==before
            assert m.byte(0x20000018)==0 and m.byte(0x20000318)==0
            assert m.get32(0x200004ec)==0 and m.get32(0x200004f0)==0
            m.telemetry_power(True)
            m.key(1);assert m.byte(0x20000018)==1
    # A wake press while running must not be mistaken for STOP.
    m=PowerPanel();m.command(0,1,1);m.dry(0);m.step();m.command(0,7,0)
    m.key(5)
    assert m.outputs==[True,True] and m.byte(0x200004fd)==1
    assert m.call('uxQueueMessagesWaiting',m.get32(0x20000364))==0
    manual_only_tests()
    report={'status':'PASS','candidate_sha256':hashlib.sha256(BLOB).hexdigest(),
            'scope':__doc__.strip()}
    (ROOT/'analysis/ota/display-power-tests.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
