#!/usr/bin/env python3
"""Run the ARM panel task with native FreeRTOS timers, queues and callbacks.

Task yields/ticks and sensor samples are scripted; there is no interrupt or
physical display model. Unlike test_ui, timer APIs and callback dispatch are
not stubbed. Address zero contains the real boot-flash alias as on the board.
"""
import os,struct
os.environ['SH03_SYSTEM_VARIANT']='ota'
from test_system import System,SYMBOLS,BLOB,BUFFER,STOP
from unicorn import UC_HOOK_CODE,UC_HOOK_MEM_READ,UC_HOOK_MEM_WRITE
from unicorn.arm_const import *

class Panel(System):
    def __init__(self):
        super().__init__()
        self.uc.mem_map(0,0x40000);self.uc.mem_write(0,BLOB)
        self.put32(SYMBOLS['uxCriticalNesting'],0)
        self.now=0;self.contexts={};self.yielded=False;self.timer_waits=0
        self.created_timers=set();self.freed_timers=set()
        self.uc.mem_write(BUFFER,b'panel-test\0')
        assert self.call('xTaskCreate',SYMBOLS['sh03_task_interactive']|1,BUFFER,256,0,5,BUFFER+64)==1
        self.ui_task=self.get32(BUFFER+64);self.put32(0x200004e8,self.ui_task)
        assert self.call('xTimerCreateTimerTask')==1
        self.timer_task=self.get32(SYMBOLS['xTimerTaskHandle'])
        self.put32(SYMBOLS['pxCurrentTCB'],self.ui_task)
        self.put32(SYMBOLS['xSchedulerRunning'],1)
        self.put32(0x2000036c,self.call('xQueueGenericCreate',4,4,0))
        for ch in range(2):
            self.put32(0x2000032c+4*ch,self.call('xEventGroupCreate'))
            self.put32(0x20000364+4*ch,self.call('xQueueGenericCreate',1,16,0))
            self.uc.mem_write(0x200003b4+20*ch,struct.pack('<fiIiI',25.,25,40,30,4000))
        self.hook('vTaskDelay',self.delay)
        self.hook('vQueueWaitForMessageRestricted',self.timer_wait)
        self.hook('vPortFree',self.free)
        self.hook('xTimerIsTimerActive',self.timer_handle)
        self.hook('xTimerGenericCommandFromTask',self.timer_handle)
        # A NULL dereference into vector memory is always a bug, even when the
        # aliased byte happens to be zero in a particular build.
        def null_access(uc,access,address,size,value,data):
            raise AssertionError(f'NULL object access at {address:#x}, PC={uc.reg_read(UC_ARM_REG_PC):#x}')
        self.uc.hook_add(UC_HOOK_MEM_READ,null_access,None,0,0xfff)
        self.uc.hook_add(UC_HOOK_MEM_WRITE,null_access,None,0,0xfff)
    def hook(self,name,callback):
        return self.uc.hook_add(UC_HOOK_CODE,callback,None,SYMBOLS[name],SYMBOLS[name])
    def free(self,uc,address,size,data):
        handle=uc.reg_read(UC_ARM_REG_R0)
        if handle in self.created_timers:self.freed_timers.add(handle)
    def timer_handle(self,uc,address,size,data):
        handle=uc.reg_read(UC_ARM_REG_R0)
        assert handle and handle not in self.freed_timers, f'Invalid/deleted timer used: {handle:#x}'
        self.created_timers.add(handle)
    def delay(self,uc,address,size,data):
        self.yielded=True
        uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR));uc.emu_stop()
    def timer_wait(self,uc,address,size,data):
        self.timer_waits+=1
        if self.timer_waits==2:uc.emu_stop()
        else:uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
    def slice(self,name,entry,task,argument=0):
        self.put32(SYMBOLS['pxCurrentTCB'],task)
        self.put32(SYMBOLS['xTickCount'],self.now)
        if name in self.contexts:self.uc.context_restore(self.contexts[name])
        else:
            stacks={'ui':0x2000e000,'dry0':0x2000d000,'dry1':0x2000c000}
            self.uc.reg_write(UC_ARM_REG_SP,stacks[name])
            self.uc.reg_write(UC_ARM_REG_XPSR,0x01000000)
            self.uc.reg_write(UC_ARM_REG_LR,STOP|1)
            self.uc.reg_write(UC_ARM_REG_R0,argument)
            self.uc.reg_write(UC_ARM_REG_PC,SYMBOLS[entry])
        self.yielded=False
        self.uc.emu_start(self.uc.reg_read(UC_ARM_REG_PC)|1,STOP,count=2000000)
        assert self.yielded,f'{name} did not yield'
        self.contexts[name]=self.uc.context_save()
    def ui(self):self.slice('ui','sh03_task_interactive',self.ui_task)
    def timers(self):
        self.put32(SYMBOLS['pxCurrentTCB'],self.timer_task)
        self.put32(SYMBOLS['xTickCount'],self.now)
        self.timer_waits=0
        self.uc.reg_write(UC_ARM_REG_SP,0x2000b000)
        self.uc.reg_write(UC_ARM_REG_XPSR,0x01000000)
        self.uc.reg_write(UC_ARM_REG_LR,STOP|1)
        self.uc.emu_start(SYMBOLS['prvTimerTask']|1,STOP,count=2000000)
        assert self.timer_waits==2,'Timer task did not finish processing'
        self.call('xTaskResumeAll') # Balance suspension at the intercepted wait.
        self.put32(SYMBOLS['pxCurrentTCB'],self.ui_task)
    def key(self,key):
        self.put32(SYMBOLS['pxCurrentTCB'],self.ui_task)
        self.put32(BUFFER,key|(100<<16))
        assert self.call('xQueueGenericSend',self.get32(0x2000036c),BUFFER,0,0)==1
        self.ui();self.timers();self.now+=100
    def byte(self,address):return self.uc.mem_read(address,1)[0]
    def dry(self,ch):
        task=self.get32(0x200004ec+4*ch);assert task
        self.slice(f'dry{ch}','sh03_task_drying',task,0x20000344+16*ch)

def fault_tests():
    for handler in ('Default_Handler','NMI_Handler','HardFault_Handler','MemManage_Handler',
                    'BusFault_Handler','UsageFault_Handler','DebugMon_Handler','vApplicationStackOverflowHook'):
        m=System();m.put32(0x40010c00,0x99444444);m.put32(0x40010c0c,0xffff)
        m.put32(0x40011000,0x99444444);m.put32(0x4001100c,0xffff)
        m.uc.mem_write(SYMBOLS['sh03_heater_percent'],b'\x32\x32')
        def bsrr(uc,access,address,size,value,data):
            m.put32(0x40010c0c,(m.get32(0x40010c0c)|(value&0xffff))&~(value>>16))
        m.uc.hook_add(UC_HOOK_MEM_WRITE,bsrr,None,0x40010c10,0x40010c13)
        m.uc.mem_write(BUFFER,b'overflow-test\0')
        m.uc.reg_write(UC_ARM_REG_SP,0x2000f000);m.uc.reg_write(UC_ARM_REG_XPSR,0x01000000)
        m.uc.reg_write(UC_ARM_REG_R0,0);m.uc.reg_write(UC_ARM_REG_R1,BUFFER)
        m.uc.emu_start(SYMBOLS[handler]|1,STOP,count=2000)
        assert m.get32(0x40010c00)==0x22444444,handler
        assert m.get32(0x40010c0c)==0xff3f and m.get32(0x40021018)&8,handler
        assert bytes(m.uc.mem_read(SYMBOLS['sh03_heater_percent'],2))==b'\0\0',handler
        assert m.get32(0x40011000)==0x99444444 and m.get32(0x4001100c)==0xffff,handler
    print('PASS: fault/stack-overflow handlers force both heater pins off without changing fans')

def main():
    m=Panel()
    assert m.call('sh03_timer_is_active',0)==0
    assert m.call('sh03_timer_command',0,2,0,0,0)==0
    m.ui();m.timers()
    assert len(m.created_timers)==1,'Blink timer was not created'
    m.key(1);m.key(2)
    assert m.byte(0x20000018)==2
    before=m.byte(0x20000371);m.now=500;m.timers()
    assert m.byte(0x20000371)!=before,'Real timer callback did not blink'
    m.key(1);assert m.byte(0x20000370)==1
    m.key(1);assert m.byte(0x20000370)==0
    m.now+=10100;m.timers();assert m.byte(0x20000018)==0
    m.key(1);m.key(2);m.key(3);m.key(4)
    m.key(1) # Select chamber 1, then start/stop both chambers through panel.
    for ch in (1,0):
        assert m.byte(0x20000370)==ch
        m.key(5);m.dry(ch);m.ui();m.timers()
        assert m.byte(0x2000001d+8*ch)==1
        m.key(1);m.key(1) # Switch away and back while drying.
        m.key(5)
        for _ in range(10):
            m.dry(ch)
            if m.byte(0x200004fd+208*ch)==0:break
        m.ui();m.timers()
        assert m.byte(0x2000001d+8*ch)==0
        assert m.byte(SYMBOLS['sh03_heater_percent']+ch)==0
        if ch==1:m.key(1);m.key(1) # STOP left edit mode; select, then switch.
    heap=m.call('xPortGetFreeHeapSize')
    for _ in range(12):
        m.key(1);m.key(2)
        m.now+=10100;m.timers();m.ui()
        assert m.byte(0x20000018)==0
    assert m.call('xPortGetFreeHeapSize')==heap, 'Settings cycles leaked timer allocations'
    assert len(m.created_timers)==2 and not m.freed_timers
    print('PASS: actual ARM UI + native FreeRTOS timers/queues, NULL guards, blinking, chamber switches, timeout/re-entry and start/stop both chambers; scripted task yields')
    fault_tests()

if __name__=='__main__':main()
