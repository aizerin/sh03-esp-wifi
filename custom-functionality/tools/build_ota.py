#!/usr/bin/env python3
"""Build the loader, relocated application, factory image and OTA package."""
import hashlib,json,struct,subprocess,zlib
from build_standalone import ROOT,PREFIX,main as build_app,run

def main():
    build_app(ota=True)
    output='build/ota'
    app_output='build/ota-app'
    build=ROOT/output;build.mkdir(parents=True,exist_ok=True)
    flags=['-mcpu=cortex-m3','-mthumb','-mfloat-abi=soft','-Os','-std=gnu11',
           '-ffreestanding','-fno-builtin','-ffunction-sections','-fdata-sections',
           '-Wall','-Wextra','-Werror','-Iinclude','-g']
    objects=[]
    for name in ('main','update'):
        obj=build/(name+'.o');objects.append(str(obj))
        run([PREFIX+'gcc',*flags,'-c',f'bootloader/{name}.c','-o',str(obj)])
    elf=str(build/'bootloader.elf')
    run([PREFIX+'gcc',*flags,'-nostdlib',f'-Wl,--gc-sections,-T,bootloader/bootloader.ld,-Map,{output}/bootloader.map',*objects,'-lgcc','-o',elf])
    run([PREFIX+'objcopy','-O','binary',elf,str(build/'bootloader.bin')])
    assert not subprocess.check_output([PREFIX+'nm','-u',elf],text=True).strip()
    boot=(build/'bootloader.bin').read_bytes()
    app=(ROOT/app_output/'firmware.bin').read_bytes()
    app+=b'\xff'*(-len(app)%4)
    assert len(boot)<=0x2000 and 304<=len(app)<=0x1e000
    sp,reset=struct.unpack_from('<II',app)
    assert sp==0x20010000 and reset&1 and 0x08002130<=reset<0x08002000+len(app)
    header=struct.pack('<6I',0x55333053,1,0x08002000,len(app),zlib.crc32(app),0x00010303)
    header+=struct.pack('<II',zlib.crc32(header),0x5aa5a55a)
    (build/'application.sh03').write_bytes(header+app)
    factory=bytearray(b'\xff'*0x40000)
    factory[:len(boot)]=boot;factory[0x2000:0x2000+len(app)]=app
    factory[0x3e800:0x3e820]=header
    (build/'factory.bin').write_bytes(factory)
    report={'layout':'00010303','bootloader_bytes':len(boot),'application_bytes':len(app),
            'application_crc32':f'{zlib.crc32(app):08x}',
            'files':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [build/'bootloader.bin',build/'factory.bin',build/'application.sh03']}}
    (build/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    run([PREFIX+'size',elf]);print(json.dumps(report,indent=2))

if __name__=='__main__':main()
