#!/usr/bin/env python3
"""Build a source-only firmware: reconstructed application + official RTOS.

No dump, decompiler output, baseline binary, or firmware.S is read. Tables and
initial state are editable JSON; material profiles share the baseline JSON.
Original addresses are retained as provenance keys in a checked call resolver.
"""
from pathlib import Path
import hashlib,json,re,struct,subprocess
from build_config import ROOT,PREFIX,REPLACEMENTS,SOURCES

RUNTIME={
    0x08002bc0:'sh03_delete_task_handle',0x0800ae8c:'sh03_debug_printf',
    0x0800b798:'eTaskGetState',0x0800d268:'vPortEnterCritical',0x0800d298:'vPortExitCritical',
    0x0800d3cc:'vTaskDelay',0x0800d418:'vTaskDelete',0x0800d900:'vTaskResume',
    0x0800d9f8:'vTaskStartScheduler',0x0800da6c:'vTaskSuspend',
    0x0800df44:'xEventGroupGetBitsFromISR',0x0800df70:'xEventGroupCreate',0x0800df9c:'xEventGroupSetBits',
    0x0800e07c:'xEventGroupWaitBits',0x0800e200:'xQueueCreateMutex',0x0800e228:'xQueueGenericCreate',
    0x0800e378:'xQueueGenericSend',0x0800e52c:'xQueueGiveFromISR',0x0800e61c:'xQueueReceive',
    0x0800e770:'xQueueSemaphoreTake',0x0800e910:'xTaskAbortDelay',0x0800ead0:'xTaskCreate',
    0x0800eb24:'xTaskDelayUntil',0x0800ebe0:'xTaskGenericNotify',0x0800eda0:'xTaskGenericNotifyWait',
    0x0800ef04:'xTaskGetTickCount',0x0800f77c:'xTimerCreate',
    0x0800f818:'xTimerGenericCommandFromTask',0x0800f894:'xTimerIsTimerActive',
}

def run(args):subprocess.run(args,check=True,cwd=ROOT)

def generate_tables(build):
    config=json.loads((ROOT/'config/flash-tables.json').read_text());base=int(config['base'],16)
    blob=bytearray();rows=['#include <stdint.h>','__attribute__((section(".sh03_tables"),used))','const uint8_t sh03_flash_tables[]={']
    materials=json.loads((ROOT/'config/materials.json').read_text())
    for segment in config['segments']:
        address=int(segment['address'],16);assert address==base+len(blob)
        data=bytes(segment['bytes'])
        if address==0x08010bac:
            data=b''.join(struct.pack('<5sBH',p['name'].encode('ascii'),p['temperature_c'],p['duration_seconds']) for p in materials)
            assert len(data)==80
        blob.extend(data);rows.append(f'    /* 0x{address:08x}: {segment["name"]} */')
        rows.extend('    '+','.join(f'0x{v:02x}' for v in data[i:i+16])+',' for i in range(0,len(data),16))
    rows+=['};','const uint32_t sh03_initial_state[170]={']
    state=json.loads((ROOT/'config/initial-state.json').read_text())
    assert len(state['words'])==170
    for i,w in enumerate(state['words']):
        assert int(w['address'],16)==0x20000000+i*4
        rows.append(f'    0x{w["value"]}u, /* 0x{w["address"]} */')
    rows+=['};',''];(build/'tables.c').write_text('\n'.join(rows))
    return bytes(blob)

def generate_resolver(build):
    mapping={**REPLACEMENTS,**RUNTIME}
    # Reject unresolved literal calls in every reconstructed source/header.
    required=set()
    for directory,pattern in [('src','*.c'),('include','*.h')]:
        for path in (ROOT/directory).glob(pattern):
            required.update(int(a,16)&~1 for a in re.findall(r'SH03_FN\((0x[0-9a-fA-F]+)',path.read_text()))
    assert required<=mapping.keys(),[hex(x) for x in required-mapping.keys()]
    rows=['#include <stdint.h>']
    rows.extend(f'extern void {name}(void);' for name in sorted(set(mapping.values())))
    rows+=['static void unresolved_call(void) { for (;;) { } }','uintptr_t sh03_resolve(uintptr_t address) {','    switch (address & ~(uintptr_t)1) {']
    rows.extend(f'    case 0x{old:08x}: return (uintptr_t)&{name};' for old,name in sorted(mapping.items()))
    rows+=['    default: return (uintptr_t)&unresolved_call;','    }','}','']
    (build/'resolver.c').write_text('\n'.join(rows))
    return mapping

def main():
    build=ROOT/'build/standalone';build.mkdir(parents=True,exist_ok=True)
    tables=generate_tables(build);mapping=generate_resolver(build)
    kernel='vendor/FreeRTOS-Kernel'
    sources=[f'src/{s}.c' for s in SOURCES]+['src/debug.c','src/rtos_adapter.c','src/reset.c','build/standalone/tables.c','build/standalone/resolver.c']
    sources += [f'{kernel}/{s}.c' for s in ['tasks','queue','list','timers','event_groups','portable/GCC/ARM_CM3/port','portable/MemMang/heap_4']]
    flags=['-mcpu=cortex-m3','-mthumb','-mfloat-abi=soft','-ffreestanding','-fno-builtin','-fno-strict-aliasing','-ffp-contract=off',
           '-Os','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-override-init','-fstack-usage','-DSH03_STANDALONE',
           '-Iinclude',f'-I{kernel}/include',f'-I{kernel}/portable/GCC/ARM_CM3']
    objects=[]
    for source in sources:
        obj=build/(source.replace('/','_').removesuffix('.c')+'.o');objects.append(str(obj))
        run([PREFIX+'gcc',*flags,'-c',source,'-o',str(obj)])
    run([PREFIX+'gcc',*flags,'-nostdlib','-Wl,-T,standalone.ld,-Map,build/standalone/firmware.map',*objects,'-lgcc','-o','build/standalone/firmware.elf'])
    for fmt,suffix in [('binary','bin'),('ihex','hex')]:run([PREFIX+'objcopy','-O',fmt,'build/standalone/firmware.elf',f'build/standalone/firmware.{suffix}'])
    nm=subprocess.check_output([PREFIX+'nm','-n',str(build/'firmware.elf')],text=True)
    symbols={l.split()[2]:int(l.split()[0],16) for l in nm.splitlines() if len(l.split())==3}
    blob=(build/'firmware.bin').read_bytes()
    assert blob[0x10b74:0x10b74+len(tables)]==tables
    assert not subprocess.check_output([PREFIX+'nm','-u',str(build/'firmware.elf')],text=True).strip()
    report={'sha256':hashlib.sha256(blob).hexdigest(),'bytes':len(blob),'source_only':True,'kernel_version':'V11.1.0',
            'sources':sources,'source_hashes':{s:hashlib.sha256((ROOT/s).read_bytes()).hexdigest() for s in sources},
            'bss_end':f'{symbols["_bss_end"]:08x}','stack_top':f'{symbols["_stack_top"]:08x}',
            'functions':{f'{old:08x}':{'symbol':name,'address':f'{symbols[name]:08x}'} for old,name in sorted(mapping.items())},
            'symbols':{name:f'{address:08x}' for name,address in symbols.items()}}
    (build/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ('functions','symbols','source_hashes','sources')},indent=2))
    run([PREFIX+'size','build/standalone/firmware.elf'])

if __name__=='__main__':main()
