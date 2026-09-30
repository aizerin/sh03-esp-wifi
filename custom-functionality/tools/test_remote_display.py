#!/usr/bin/env python3
"""UART -> real ARM UI/FreeRTOS -> persistent LCD nibble RAM.

Uses scripted task yields/sensors, as in test_ui_rtos. Only LCD transport is
replaced; rendering, caching, protocol dispatch and timer callbacks execute.
This does not validate physical display timing or preemptive scheduling.
"""
import hashlib,json,struct
from test_ui_rtos import Panel,SYMBOLS,BLOB
from test_system import ROOT
from test_link import Link,frame
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_PC,UC_ARM_REG_LR

UI=0x20000370

class RemotePanel(Panel):
    feed=Link.feed
    drain=Link.drain

    def __init__(self):
        super().__init__()
        self.display=[bytearray(64),bytearray(64)]
        self.sequence=0
        self.hook('sh03_display_read_nibble',self.read_nibble)
        self.hook('sh03_display_write_nibble',self.write_nibble)
        self.ui();self.timers()
        self.now=2500
        self.step()

    def read_nibble(self,uc,address,size,data):
        display=uc.reg_read(UC_ARM_REG_R0);index=uc.reg_read(UC_ARM_REG_R1)
        uc.reg_write(UC_ARM_REG_R0,self.display[display][index&63])
        uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))

    def write_nibble(self,uc,address,size,data):
        display=uc.reg_read(UC_ARM_REG_R0);index=uc.reg_read(UC_ARM_REG_R1)
        self.display[display][index&63]=uc.reg_read(UC_ARM_REG_R2)&15
        uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))

    def step(self):
        self.now+=100
        self.timers();self.ui()
        self.drain()

    def command(self,ch,op,value,status=0,sequence=None):
        self.sequence+=1
        seq=self.sequence if sequence is None else sequence
        self.feed(frame(2,seq,bytes([ch,op])+struct.pack('<I',value)))
        self.ui()
        out=self.drain()
        ack=[p for kind,s,p in out if kind==3 and s==seq]
        assert ack==[bytes([ch,op,status])],out

    def sample(self,ch,temperature,humidity):
        self.uc.mem_write(0x200003b4+20*ch,struct.pack('<fiIiI',float(temperature),temperature,humidity,30,4000))

    def assert_display(self,visible,label):
        """Compare incremental output with complete independently requested
        chamber frames. Visibility is a scenario expectation, not UI.selected.
        Existing differential tests validate the shared glyph/segment code.
        """
        actual=self.display
        self.display=[bytearray(64),bytearray(64)]
        try:
            for ch,show in enumerate(visible):
                state=UI+4+16*ch
                self.call('sh03_display_chamber',ch,state,show)
                if self.byte(0x2000001c+8*ch)==1 and self.get32(state+8)==7200:
                    self.call('sh03_display_time',ch,2,0,2)
                self.call('sh03_display_humidity_icon',ch,self.byte(state+13))
                self.call('sh03_display_running_icon',ch,self.byte(state+14))
                self.call('sh03_display_timer_icon',ch,self.byte(UI+1) if show else 0)
            expected=self.display
        finally:
            self.display=actual
        differences=[(ch,i,a,b) for ch in range(2) for i,(a,b) in enumerate(zip(actual[ch],expected[ch])) if a!=b]
        assert not differences,f'{label}: LCD (ch,address,actual,expected) {differences}'
        assert self.byte(0x20000018)==0,label+' unexpectedly entered local edit mode'

def main():
    checks=0
    for ch in (0,1):
        m=RemotePanel()
        visible=[0,0];visible[ch]=1
        other=1-ch
        # The old code forced both displays on while leaving selected=0.
        # Changing sensor values then erased just those digits.
        m.command(ch,2,70)
        m.assert_display(visible,'remote temperature / other chamber stays blank');checks+=1
        for t,h in [(26,41),(27,42),(25,40)]:
            m.sample(ch,t,h);m.sample(other,t+1,h+1)
            m.step();m.assert_display(visible,'sensor changes after remote setting');checks+=1
        for op,value in [(6,1),(2,65),(3,14400),(5,35)]:
            m.command(ch,op,value);m.step()
            m.assert_display(visible,'preset and individual settings');checks+=1
        # Real drying task acknowledges start, then remote STOP suspends it.
        m.command(ch,1,1);m.dry(ch);m.step()
        assert m.byte(0x2000001d+8*ch)==1
        m.assert_display(visible,'remote start');checks+=1
        m.command(ch,2,75,status=2)
        m.assert_display(visible,'rejected setting while drying');checks+=1
        m.command(ch,1,0)
        for _ in range(10):
            m.dry(ch)
            if m.byte(0x200004fd+208*ch)==0:break
        m.step();m.step()
        assert m.byte(0x2000001d+8*ch)==0
        m.assert_display([0,0],'remote stop');checks+=1
        # Duplicate/no-op STOP must not relight a stopped panel.
        m.command(ch,1,0,sequence=m.sequence)
        m.assert_display([0,0],'duplicate stop');checks+=1
        m.command(ch,1,0)
        m.assert_display([0,0],'already stopped');checks+=1
        # Restart without changing settings must restore the display, even
        # when reusing a suspended task rather than creating a new one.
        m.command(ch,1,1)
        for _ in range(10):
            m.dry(ch)
            if m.byte(0x200004fd+208*ch):break
        m.step();m.step()
        m.assert_display(visible,'remote restart existing task');checks+=1
        m.command(other,6,2);visible[other]=1
        m.step();m.assert_display(visible,'configure other chamber while drying');checks+=1
        for _ in range(8):m.step();m.assert_display(visible,'both panels stay consistent');checks+=1
        # Humidity monitoring is active even while its heater is idle. Its
        # screen must remain complete through start, STOP and task reuse too.
        m=RemotePanel();visible=[0,0];visible[ch]=1
        m.command(ch,4,1);m.step()
        m.assert_display(visible,'humidity mode settings');checks+=1
        m.command(ch,5,35);m.sample(ch,25,30)
        m.command(ch,1,1)
        for _ in range(10):
            m.dry(ch);m.step()
            if m.byte(0x2000001d+8*ch)==1:break
        m.step()
        assert m.byte(0x2000001d+8*ch)==1 and m.byte(0x200004fd+208*ch)==0
        m.assert_display(visible,'humidity monitoring without heating');checks+=1
        m.sample(ch,26,40);m.dry(ch);m.step()
        assert m.byte(0x200004fd+208*ch)==1
        m.assert_display(visible,'humidity trigger starts drying');checks+=1
        m.command(ch,1,0)
        for _ in range(10):
            m.dry(ch)
            if m.byte(0x200004fd+208*ch)==0:break
        m.step();m.step()
        assert m.byte(0x2000001d+8*ch)==0
        m.assert_display(visible,'humidity stop preserves full settings');checks+=1
        m.command(ch,1,1)
        for _ in range(10):
            m.dry(ch)
            if m.byte(0x200004fd+208*ch):break
        m.step();m.step()
        assert m.byte(UI+4+16*ch+13)==1,'Humidity icon missing after restart'
        m.assert_display(visible,'humidity restart');checks+=1
        # Queries, rejected commands and duplicate ACKs must preserve local
        # panel editing, including its intentionally hidden blink phase.
        m=RemotePanel();m.command(ch,2,70)
        seq=m.sequence
        m.key(1);m.key(2)
        m.ui();before=[bytes(d) for d in m.display]
        m.command(ch,2,70,sequence=seq)
        assert m.display==before and m.byte(0x20000018)==2;checks+=1
        m.command(other,2,65,status=2)
        assert m.display==before and m.byte(0x20000018)==2;checks+=1
        m.feed(frame(4,999));m.ui();m.drain()
        assert m.display==before and m.byte(0x20000018)==2;checks+=1
        m.now+=10100;m.step();m.step()
        assert m.byte(0x20000018)==0;checks+=1
    report={'status':'PASS','candidate_sha256':hashlib.sha256(BLOB).hexdigest(),'checks':checks,
            'scope':__doc__.strip()}
    path=ROOT/'analysis/ota/remote-display-tests.json';path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
