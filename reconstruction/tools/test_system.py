#!/usr/bin/env python3
"""Execute source-only reset/startup and real FreeRTOS APIs in Unicorn.

Hardware status advances on reads. Reset executes actual initialization, display
traffic and debug output. Scheduler launch stops before its first exception
return: interrupt delivery and preemptive execution are NOT modeled here.
"""
from pathlib import Path
import hashlib,json,struct
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_CODE,UC_HOOK_MEM_READ,UC_HOOK_MEM_WRITE
from unicorn.arm_const import *

ROOT=Path(__file__).resolve().parents[1]
MANIFEST=json.loads((ROOT/'build/standalone/manifest.json').read_text())
BLOB=(ROOT/'build/standalone/firmware.bin').read_bytes()
SYMBOLS={k:int(v,16) for k,v in MANIFEST['symbols'].items()}
FLASH=0x08000000;RAM=0x20000000;STOP=FLASH+0x3f000;BUFFER=RAM+0xe000
REGS=[UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3]

class System:
    def __init__(self,initialized=True):
        self.uc=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
        for base,size in [(FLASH,0x40000),(RAM,0x10000),(0x40000000,0x25000),(0x42200000,0x30000),(0xe000e000,0x2000)]:self.uc.mem_map(base,size)
        self.uc.mem_write(FLASH,BLOB)
        self.uart=bytearray();self.calls=[];self.stop_reason=None
        self.uc.hook_add(UC_HOOK_MEM_READ,self.hardware_read,None,0x40000000,0x40024fff)
        self.uc.hook_add(UC_HOOK_MEM_WRITE,self.hardware_write,None,0x40000000,0x40024fff)
        self.uc.hook_add(UC_HOOK_CODE,self.task_create,None,SYMBOLS['xTaskCreate'],SYMBOLS['xTaskCreate'])
        if initialized:self.initialize_ram()
    def get32(self,address):return struct.unpack('<I',self.uc.mem_read(address,4))[0]
    def put32(self,address,value):self.uc.mem_write(address,struct.pack('<I',value&0xffffffff))
    def cstring(self,address):
        value=bytearray()
        for offset in range(256):
            byte=self.uc.mem_read(address+offset,1)[0]
            if not byte:return value.decode('ascii')
            value.append(byte)
        raise AssertionError('Unterminated string')
    def initialize_ram(self):
        self.uc.mem_write(RAM,bytes(0x10000))
        start=SYMBOLS['_data_start'];end=SYMBOLS['_data_end'];source=SYMBOLS['_data_load']
        self.uc.mem_write(start,bytes(self.uc.mem_read(source,end-start)))
        self.uc.mem_write(RAM,bytes(self.uc.mem_read(SYMBOLS['sh03_initial_state'],680)))
    def hardware_read(self,uc,access,address,size,value,data):
        current=int.from_bytes(uc.mem_read(address,size),'little')
        if address==0x40021000:
            if current&0x10000:current|=0x20000
            if current&0x1000000:current|=0x2000000
        elif address==0x40021004 and current&3==2:current=(current&~12)|8
        elif address in (0x40012408,0x40013c08):current&=~12
        elif address==0x40001024:current=65535
        elif address==0x40004c00:current|=0x80
        uc.mem_write(address,current.to_bytes(size,'little'))
    def hardware_write(self,uc,access,address,size,value,data):
        if address==0x40004c04:self.uart.append(value&255)
    def task_create(self,uc,address,size,data):
        a,b,c,d=[uc.reg_read(r) for r in REGS]
        sp=uc.reg_read(UC_ARM_REG_SP)
        self.calls.append({'entry':a,'name':self.cstring(b),'stack_words':c,'argument':d,'priority':self.get32(sp)})
    def call(self,name,*args,limit=1000000):
        uc=self.uc;uc.reg_write(UC_ARM_REG_SP,RAM+0xf000);uc.reg_write(UC_ARM_REG_LR,STOP|1);uc.reg_write(UC_ARM_REG_XPSR,0x01000000)
        for r,v in zip(REGS,args):uc.reg_write(r,v&0xffffffff)
        for i,v in enumerate(args[4:]):self.put32(RAM+0xf000+i*4,v)
        uc.emu_start(SYMBOLS[name]|1,STOP,count=limit)
        assert uc.reg_read(UC_ARM_REG_PC)==STOP,(name,hex(uc.reg_read(UC_ARM_REG_PC)))
        assert uc.reg_read(UC_ARM_REG_SP)==RAM+0xf000,name+' stack'
        return uc.reg_read(UC_ARM_REG_R0)
    def stop_at(self,name):
        def stop(uc,address,size,data):self.stop_reason=name;uc.emu_stop()
        return self.uc.hook_add(UC_HOOK_CODE,stop,None,SYMBOLS[name],SYMBOLS[name])
    def run_to(self,start,stop,limit=4000000):
        hook=self.stop_at(stop);self.stop_reason=None
        self.uc.reg_write(UC_ARM_REG_SP,self.get32(FLASH));self.uc.reg_write(UC_ARM_REG_LR,STOP|1);self.uc.reg_write(UC_ARM_REG_XPSR,0x01000000)
        self.uc.emu_start(SYMBOLS[start]|1,STOP,count=limit)
        self.uc.hook_del(hook)
        assert self.stop_reason==stop,(start,hex(self.uc.reg_read(UC_ARM_REG_PC)))

def reset_test():
    m=System(initialized=False);m.uc.mem_write(RAM,bytes([0xa5])*0x10000)
    vectors=struct.unpack_from('<76I',BLOB)
    assert vectors[0]==0x20010000
    for index,name in [(1,'Reset_Handler'),(11,'SVC_Handler'),(14,'PendSV_Handler'),(15,'SysTick_Handler'),
                       (25,'sh03_touch_irq'),(30,'sh03_dma1_channel4_irq'),(31,'sh03_dma1_channel5_irq'),(75,'sh03_dma2_channel4_5_irq')]:
        assert vectors[index]==SYMBOLS[name]|1
    m.run_to('Reset_Handler','vTaskStartScheduler')
    assert m.get32(0xe000ed08)==FLASH
    assert m.get32(0x40021004)&0x3fffff==0x1d840a
    assert m.get32(0x40000834)&65535==0 and m.get32(0x40000838)&65535==0
    assert m.get32(0x40013434)&65535==0 and m.get32(0x40013438)&65535==0
    assert m.calls==[{'entry':SYMBOLS['sh03_task_initialize']|1,'name':'Init_Task','stack_words':256,'argument':0,'priority':4}]
    assert m.call('uxTaskGetNumberOfTasks')==1
    assert 'Frimware Version: V3.5.1\r\nFreeRTOS Version: V11.1.0\r\nInit_Task create successed\r\n' in m.uart.decode('ascii')
    m.run_to('vTaskStartScheduler','prvPortStartFirstTask')
    assert m.get32(0xe000e014)==71999 and m.get32(0xe000e010)==7
    assert m.call('uxTaskGetNumberOfTasks')==3
    assert [(c['name'],c['stack_words'],c['priority']) for c in m.calls[1:]]==[('IDLE',256,0),('Tmr Svc',512,14)]
    return {'startup_tasks':m.calls,'uart':m.uart.decode('ascii'),'remaining_heap':m.call('xPortGetFreeHeapSize')}

def api_tests():
    m=System();m.uc.mem_write(BUFFER,b'api-test\0')
    assert m.call('xTaskCreate',SYMBOLS['sh03_task_initialize']|1,BUFFER,256,0,4,BUFFER+32)==1
    task=m.get32(BUFFER+32);assert task
    queue=m.call('xQueueGenericCreate',2,4,0);assert queue
    for value in (17,23):m.put32(BUFFER+64,value);assert m.call('xQueueGenericSend',queue,BUFFER+64,0,0)==1
    assert m.call('xQueueGenericSend',queue,BUFFER+64,0,0)==0
    for value in (17,23):assert m.call('xQueueReceive',queue,BUFFER+68,0)==1 and m.get32(BUFFER+68)==value
    assert m.call('xQueueReceive',queue,BUFFER+68,0)==0
    mutex=m.call('xQueueCreateMutex',1);assert mutex
    assert m.call('xQueueSemaphoreTake',mutex,0)==1
    assert m.call('xQueueSemaphoreTake',mutex,0)==0
    assert m.call('xQueueGenericSend',mutex,0,0,0)==1
    event=m.call('xEventGroupCreate');assert event
    assert m.call('xEventGroupSetBits',event,5)==5
    assert m.call('xEventGroupWaitBits',event,1,1,0,0)==5
    assert m.call('xEventGroupGetBitsFromISR',event)==4
    assert m.call('xEventGroupWaitBits',event,3,0,1,0)==4
    assert m.call('xTaskGenericNotify',task,0,0x1234,3,BUFFER+72)==1
    assert m.get32(BUFFER+72)==0
    assert m.call('xTaskGenericNotifyWait',0,0,0xffffffff,BUFFER+76,0)==1
    assert m.get32(BUFFER+76)==0x1234
    assert m.call('xTaskGenericNotifyWait',0,0,0xffffffff,BUFFER+76,0)==0
    assert m.call('xTaskCreate',SYMBOLS['sh03_task_countdown']|1,BUFFER,128,0,2,BUFFER+36)==1
    other=m.get32(BUFFER+36);assert m.call('eTaskGetState',other)==1
    m.call('vTaskSuspend',other);assert m.call('eTaskGetState',other)==3
    m.call('vTaskResume',other);assert m.call('eTaskGetState',other)==1
    assert m.call('xTaskAbortDelay',other)==0
    m.call('vTaskDelete',other);assert m.call('uxTaskGetNumberOfTasks')==1
    timer=m.call('xTimerCreate',BUFFER,10,1,2,SYMBOLS['sh03_blink_callback']|1);assert timer
    assert m.call('xTimerIsTimerActive',timer)==0
    assert m.call('xTimerGenericCommandFromTask',timer,1,0,0,0)==1
    assert m.call('uxQueueMessagesWaiting',m.get32(SYMBOLS['xTimerQueue']))==1
    # Run the timer task through one command, stopping before its second wait.
    # No automatic context switches occur in this emulator.
    waits=[]
    def timer_wait(uc,address,size,data):
        waits.append(address)
        if len(waits)==2:uc.emu_stop()
    hook=m.uc.hook_add(UC_HOOK_CODE,timer_wait,None,SYMBOLS['vQueueWaitForMessageRestricted'],SYMBOLS['vQueueWaitForMessageRestricted'])
    m.uc.reg_write(UC_ARM_REG_SP,RAM+0xf000);m.uc.reg_write(UC_ARM_REG_LR,STOP|1)
    m.uc.emu_start(SYMBOLS['prvTimerTask']|1,STOP,count=100000)
    m.uc.hook_del(hook);assert len(waits)==2
    m.call('xTaskResumeAll') # Balance the suspension preceding the second wait.
    assert m.call('xTimerIsTimerActive',timer)==1
    # ISR semaphore API is exercised with the real kernel and BASEPRI port.
    sem=m.call('xQueueGenericCreate',1,0,3);m.put32(BUFFER+80,0)
    assert m.call('xQueueGiveFromISR',sem,BUFFER+80)==1
    assert m.call('xQueueSemaphoreTake',sem,0)==1
    return {'task':hex(task),'remaining_heap':m.call('xPortGetFreeHeapSize'),'api_groups':8}

def debug_tests():
    m=System()
    for value in (0,1,-1,2147483647,-2147483648):
        m.uart.clear();m.uc.mem_write(BUFFER,b'value %d %s %%\r\n\0');m.uc.mem_write(BUFFER+64,b'ok\0')
        m.call('sh03_debug_printf',BUFFER,value,BUFFER+64)
        assert m.uart.decode('ascii')==f'value {value} ok %\r\n'
    m.uart.clear();m.uc.mem_write(BUFFER,b'%s\0');m.uc.mem_write(BUFFER+64,b'A'*400+b'\0')
    assert m.call('sh03_debug_printf',BUFFER,BUFFER+64)==255 and m.uart==b'A'*255

def main():
    path=ROOT/'analysis/standalone/system-tests.json';path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps({'status':'RUNNING','candidate_sha256':hashlib.sha256(BLOB).hexdigest()})+'\n')
    startup=reset_test();print('Real reset and scheduler setup passed',flush=True)
    kernel=api_tests();print('Real FreeRTOS API integration passed',flush=True)
    debug_tests()
    result={'status':'PASS','candidate_sha256':hashlib.sha256(BLOB).hexdigest(),'startup':startup,'kernel':kernel,
            'scope':'Actual reset, hardware initialization and RTOS scheduler setup through first-task launch; real heap/queue/event/mutex/notification/task/timer/ISR APIs; scripted MMIO; no context-switch/interrupt-delivery or board test'}
    path.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))

if __name__=='__main__':main()
