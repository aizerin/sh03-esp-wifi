#!/usr/bin/env python3
"""Execute relocated ARM app + bootloader handoff with scripted MMIO in Unicorn."""
import os,struct
os.environ['SH03_SYSTEM_VARIANT']='ota'
import test_system as system
from unicorn import UC_HOOK_MEM_WRITE,UC_HOOK_MEM_READ
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_XPSR,UC_ARM_REG_PRIMASK

# Use the existing real RTOS/UART harness against the factory image.
from test_link import Link,frame,frames,main as link_regressions

def main():
    m=system.System(initialized=False)
    m.uc.mem_map(0x1ffff000,0x1000);m.uc.mem_write(0x1ffff7e0,struct.pack('<H',256))
    m.uc.mem_map(0xe0042000,0x1000);m.put32(0xe0042000,0) # No debugger: ES0340.
    m.put32(0xe000ed00,0x411fc231)
    time=0;heater_off=[];handoff=[];backlight=[]
    def read(uc,access,address,size,value,data):
        nonlocal time
        if address==0x40000024:
            time+=1;uc.mem_write(address,(time&65535).to_bytes(size,'little'))
        elif address==0x40004c00:uc.mem_write(address,(0xc0).to_bytes(size,'little'))
    def write(uc,access,address,size,value,data):
        if address==0x40010c10:heater_off.append(value)
    def backlight_write(uc,access,address,size,value,data):
        assert size==4 and value in (0,1) and not m.calls
        assert m.get32(0x40000834)&65535==m.get32(0x40000838)&65535==0
        assert m.get32(0x40013434)&65535==m.get32(0x40013438)&65535==0
        assert m.get32(0x40010c00)&0xf0==0x10
        backlight.append(value)
    m.uc.hook_add(UC_HOOK_MEM_WRITE,backlight_write,None,0x42218184,0x42218187)
    m.uc.hook_add(UC_HOOK_MEM_READ,read,None,0x40000000,0x40004fff)
    m.uc.hook_add(UC_HOOK_MEM_WRITE,write,None,0x40010c00,0x40010c1f)
    def app_entry(uc,address,size,data):
        handoff.append((m.get32(0xe000ed08),uc.reg_read(UC_ARM_REG_SP),uc.reg_read(UC_ARM_REG_PRIMASK)))
        assert m.get32(0x40011000)>>24==0x22, 'Fans must stay driven low until application takes over'
    from unicorn import UC_HOOK_CODE
    m.uc.hook_add(UC_HOOK_CODE,app_entry,None,system.SYMBOLS['Reset_Handler'],system.SYMBOLS['Reset_Handler'])
    stop_hook=m.stop_at('vTaskStartScheduler')
    m.uc.reg_write(UC_ARM_REG_SP,0x20010000);m.uc.reg_write(UC_ARM_REG_XPSR,0x01000000)
    m.uc.emu_start(m.get32(0x08000004),system.STOP,count=15000000)
    assert m.stop_reason=='vTaskStartScheduler'
    m.uc.hook_del(stop_hook)
    assert handoff==[(0x08002000,0x20010000,0)]
    assert backlight==[0,1], 'Startup must enable backlights without repeating the diagnostic pulse'
    assert heater_off and heater_off[0]==0x00c00000
    assert m.get32(0xe000ed08)==0x08002000
    assert m.get32(0x40000834)&65535==0 and m.get32(0x40000838)&65535==0
    assert m.get32(0x40011000)>>24==0x99 # PC6/7 taken over by TIM8.
    assert m.get32(0x40013434)&65535==0 and m.get32(0x40013438)&65535==0
    assert m.get32(0x40013418)&0x7070==0x6060 # PWM1, active high, zero duty.
    assert m.get32(0x40013420)&0x33==0x11
    assert m.get32(0x40013444)&0x8000 and m.get32(0x40013400)&1
    assert m.call('uxTaskGetNumberOfTasks')==1
    m.run_to('vTaskStartScheduler','prvPortStartFirstTask')
    assert m.call('uxTaskGetNumberOfTasks')==3
    assert not m.uart
    print('PASS: ARM bootloader -> relocated reset -> real FreeRTOS startup, VTOR/MSP/interrupt state, heaters off, fan GPIO -> zero-duty PWM handoff')

    for address in [0x2000001d,0x20000025,0x200004fd,0x200005cd,
                    system.SYMBOLS['sh03_heater_percent'],system.SYMBOLS['sh03_heater_percent']+1,
                    0x20000018,0x20000318]:
        m=Link();assert m.call('sh03_update_allowed')==0
        m.u8(address,1);assert m.call('sh03_update_allowed')==2
        m.feed(frame(0x10,99));m.call('sh03_link_poll')
        reply=next(p for kind,seq,p in m.drain() if kind==0x18 and seq==99)
        assert reply[:2]==b'\x10\x02'
    m=Link();m.ticks(100);assert m.call('sh03_update_allowed')==2
    for ch in (0,1):
        m=Link();task=m.suspended_task(ch);assert m.call('sh03_update_allowed')==0
        m.call('vTaskResume',task);assert m.call('sh03_update_allowed')==2
    m=Link();m.feed(frame(0x17,7));m.call('sh03_link_poll')
    reply=next(p for kind,seq,p in m.drain() if kind==0x18 and seq==7)
    header=system.BLOB[0x3e800:0x3e820]
    assert reply[:2]==b'\x17\x00' and reply[6:14]==header[12:20]
    m=Link();m.feed(frame(4,1));m.call('sh03_link_poll') # ACK must follow queued STATE frames.
    m.feed(frame(0x10,123));reset=[]
    def reset_write(uc,access,address,size,value,data):
        if address==0xe000ed0c:
            reset.append(value);uc.emu_stop()
    m.uc.hook_add(UC_HOOK_MEM_WRITE,reset_write,None,0xe000ed0c,0xe000ed0f)
    m.put32(0x40004c00,0xc0)
    m.uc.reg_write(UC_ARM_REG_SP,0x2000f000);m.uc.reg_write(UC_ARM_REG_LR,system.STOP|1)
    m.uc.emu_start(system.SYMBOLS['sh03_link_poll']|1,system.STOP,count=1000000)
    assert reset==[0x05fa0004] and m.uc.reg_read(UC_ARM_REG_PRIMASK)==1
    replies=frames(bytes(m.uart));assert [r[0] for r in replies]==[1,1,5,0x18]
    assert replies[2][2]==b'\x00\x00'
    assert replies[-1][0:2]==(0x18,123) and replies[-1][2][:2]==b'\x10\x00'
    print('PASS: ARM update interlock, busy replies, image identity and ACK before software reset')
    link_regressions(directory=system.ROOT/'analysis/ota')

if __name__=='__main__':main()
