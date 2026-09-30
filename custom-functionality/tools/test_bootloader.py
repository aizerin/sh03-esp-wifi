#!/usr/bin/env python3
"""Execute the ARM UART bootloader and its flash register driver in Unicorn.

MMIO models UART, timer, flash unlock, page erase and program operations;
actual analogue flash timing and instruction stalls remain board tests.
"""
import binascii,collections,struct,zlib
from pathlib import Path
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_CODE,UC_HOOK_MEM_READ,UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_PC,UC_ARM_REG_XPSR
ROOT=Path(__file__).resolve().parents[1]
BASE=0x08000000;APP=BASE+0x2000;UART=0x40004c00;FLASH=0x40022000

def frame(op,seq,p=b''):
    body=bytes([1,op])+struct.pack('<H',seq)+bytes([len(p)])+p
    return b'\xa5\x5a'+body+struct.pack('<H',binascii.crc_hqx(body,0xffff))

class Board:
    def __init__(self,geometry=256,device_id=0,cpuid=0x411fc231):
        self.uc=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
        for base,size in [(BASE,0x40000),(0x20000000,0x10000),(0x40000000,0x23000),
                          (0xe000e000,0x2000),(0xe0042000,0x1000),(0x1ffff000,0x1000)]:self.uc.mem_map(base,size)
        self.original=(ROOT/'build/ota/factory.bin').read_bytes();self.uc.mem_write(BASE,self.original)
        # STM32F1 ES0340: normal execution reads DBGMCU_IDCODE as zero.
        # Keep debugger-attached identification a separate test case.
        self.put(0x1ffff7e0,geometry,2);self.put(0xe0042000,device_id)
        self.put(0xe000ed00,cpuid)
        # Floating pins do not stop the fans on this board. Seed high output
        # latches to also catch a pulse if GPIO output mode is enabled before
        # loading the inactive level. Other GPIO modes must be preserved.
        self.output_ports=(0x40010c00,0x40011000) # heaters PB6/7, fans PC6/7
        for port in self.output_ports:
            self.put(port,0x44444444);self.put(port+4,0x44444444)
            self.put(port+12,0xffff)
        self.rx=collections.deque();self.tx=bytearray();self.now=0;self.loop_pc=None;self.jumped=False
        self.locked=True;self.key=0;self.flash_cr=0x80;self.erases=[];self.programs=0
        self.idle_budget=0
        self.uc.hook_add(UC_HOOK_MEM_READ,self.read,None,0x40000000,0x40022fff)
        self.uc.hook_add(UC_HOOK_MEM_WRITE,self.write,None,0x40000000,0x40022fff)
        self.uc.hook_add(UC_HOOK_MEM_WRITE,self.program,None,BASE,BASE+0x3ffff)
        self.uc.hook_add(UC_HOOK_CODE,self.app_entry,None,APP,BASE+0x1ffff)
        self.uc.reg_write(UC_ARM_REG_SP,0x20010000);self.uc.reg_write(UC_ARM_REG_XPSR,0x01000000)
        self.pc=self.get(BASE+4);self.run()
    def get(self,a):return int.from_bytes(self.uc.mem_read(a,4),'little')
    def put(self,a,v,n=4):self.uc.mem_write(a,v.to_bytes(n,'little'))
    def read(self,uc,access,a,n,value,data):
        if a==0x40000024:self.put(a,self.now&65535,n)
        elif a==FLASH+0xc:self.put(a,0,n)
        elif a==FLASH+0x10:self.put(a,(self.flash_cr&~0x80)|(0x80 if self.locked else 0),n)
        elif a==UART:
            pc=uc.reg_read(UC_ARM_REG_PC)
            if self.loop_pc is None:self.loop_pc=pc
            self.put(a,0xc0|(0x20 if self.rx else 0),n)
            if pc==self.loop_pc and not self.rx:
                if self.idle_budget: self.idle_budget-=1
                else: uc.emu_stop()
        elif a==UART+4:self.put(a,self.rx.popleft() if self.rx else 0,n)
    def write(self,uc,access,a,n,v,data):
        for port in self.output_ports:
            if a==port+16: # GPIO BSRR updates the ODR latch atomically.
                self.put(port+12,(self.get(port+12)|(v&0xffff))&~(v>>16))
            elif a==port:
                for pin in (6,7):
                    if (v>>(4*pin))&3:
                        assert not self.get(port+12)&(1<<pin), 'Output enabled before inactive level was loaded'
        if a==UART+4:self.tx.append(v&255)
        elif a==FLASH+4:
            if self.key==0x45670123 and v==0xcdef89ab:self.locked=False
            self.key=v
        elif a==FLASH+0x10:
            self.flash_cr=v
            if v&0x80:self.locked=True
            if v&0x40:
                assert not self.locked and v&2
                address=self.get(FLASH+0x14)
                assert APP<=address<BASE+0x3f000 and address%2048==0
                self.uc.mem_write(address,b'\xff'*2048);self.erases.append(address)
    def program(self,uc,access,a,n,v,data):
        self.outputs_off()
        assert not self.locked and self.flash_cr&1 and n==2 and a%2==0
        assert APP<=a<BASE+0x3f000 and bytes(uc.mem_read(a,2))==b'\xff\xff'
        self.programs+=1
    def app_entry(self,uc,address,size,data):self.jumped=True;uc.emu_stop()
    def outputs_off(self):
        for port in self.output_ports:
            mode=self.get(port);odr=self.get(port+12)
            assert mode&0xffffff==0x444444, 'Unrelated GPIO modes changed'
            for pin in (6,7):
                assert (mode>>(4*pin))&15 in (1,2,3), 'Fan/heater pin must be driven, not floating'
                assert not odr&(1<<pin), 'Fan/heater must remain off in bootloader'
    def run(self):
        self.uc.emu_start(self.pc|1,BASE+0x40000,count=12000000)
        self.pc=self.uc.reg_read(UC_ARM_REG_PC)
        assert self.pc==self.loop_pc or self.jumped,(hex(self.pc),self.loop_pc)
        self.outputs_off() # Covers wait, HELLO lease, rejected updates and handoff.
    def request(self,op,seq,p=b''):
        self.rx.extend(frame(op,seq,p));self.run();data=bytes(self.tx);self.tx.clear()
        assert data[:7]==bytes([0xa5,0x5a,1,0x18,seq&255,seq>>8,18]),data.hex()
        assert len(data)==27 and struct.unpack_from('<H',data,25)[0]==binascii.crc_hqx(data[2:25],0xffff)
        return struct.unpack('<BB4I',data[7:25])
    def advance(self,ms):
        self.now+=ms;self.idle_budget=1;self.run()

def main():
    # ESP32 may boot several seconds after SH03. Wi-Fi may take much longer:
    # a local HELLO keeps renewing the lease across timer wrap without writes.
    b=Board();b.advance(9000);assert not b.jumped
    for seq in range(1,10):
        assert b.request(0x11,seq)[1]==0
        b.advance(20000);assert not b.jumped
    assert not b.erases and not b.programs
    assert b.request(0x15,50)[1]==0 and b.jumped
    b=Board();b.advance(10001);assert b.jumped
    b=Board(device_id=0x10036414);b.advance(10001);assert b.jumped
    b=Board();b.request(0x11,1);b.advance(30001);assert b.jumped
    for settings in ({'geometry':128},{'geometry':512},{'device_id':0x410},
                     {'device_id':0xffffffff},{'cpuid':0x410fc241}):
        b=Board(**settings);b.advance(10001);assert not b.jumped
        assert b.request(0x11,1)[1]==6 and not b.erases and not b.programs
    b=Board();assert b.request(0x11,2)==(0x11,0,0,0x1e000,1,0x10303)
    corrupt=bytearray(frame(0x12,3,bytes(16)));corrupt[-1]^=1;b.rx.extend(corrupt);b.run()
    assert not b.tx and not b.erases
    image=bytearray((i*23&255 for i in range(768)));struct.pack_into('<II',image,0,0x20010000,APP+305)
    crc=zlib.crc32(image);begin=struct.pack('<4I',APP,len(image),crc,0x10303)
    assert b.request(0x12,4,begin)[1:]==(0,0,len(image),crc,0x10303)
    assert b.get(APP)==0x20010000 # Original application still in place.
    for offset in range(0,len(image),60):
        p=struct.pack('<I',offset)+image[offset:offset+60]
        reply=b.request(0x13,10+offset//60,p)
        assert reply[1]==0 and reply[2]==min(offset+60,len(image))
    assert b.request(0x14,100)[1]==0 and b.jumped
    assert bytes(b.uc.mem_read(APP,len(image)))==image
    assert b.get(BASE+0x3e800+16)==crc and b.get(BASE+0x3e000)==0xffffffff
    assert bytes(b.uc.mem_read(BASE,0x2000))==b.original[:0x2000]
    assert b.locked and b.get(0xe000ed08)==APP
    print(f'PASS: standalone/debugger boot, fans/heaters off during wait/update/handoff, actual ARM UART BEGIN/DATA/END, CRC rejection, geometry/core guard, flash register driver ({len(b.erases)} erases, {b.programs} half-word writes), install and jump')

if __name__=='__main__':main()
