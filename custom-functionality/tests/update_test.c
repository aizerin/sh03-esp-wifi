#include "sh03_update.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t flash[0x40000],snapshot[0x40000],image[SH03_SLOT_SIZE];
static jmp_buf power;
static unsigned events,cut;
static int fault_mode;
static unsigned index_of(uint32_t address)
{
    assert(address>=0x08000000 && address<0x08040000); return address-0x08000000;
}
const uint8_t *sh03_flash_at(uint32_t address) { return flash+index_of(address); }
static void event(void) { if (++events==cut) longjmp(power,1); }
int sh03_flash_erase(uint32_t address)
{
    assert(address>=SH03_APP_BASE && address<SH03_ACTIVE_BASE+SH03_PAGE_SIZE && !(address&(SH03_PAGE_SIZE-1)));
    unsigned i=index_of(address);event();
    /* A loss during erase may leave only some bits/cells erased. */
    if (fault_mode) for (unsigned j=0;j<SH03_PAGE_SIZE;++j) flash[i+j]|=0x55;
    else memset(flash+i,0xff,SH03_PAGE_SIZE/2);
    event();memset(flash+i,0xff,SH03_PAGE_SIZE);event();return 1;
}
int sh03_flash_program(uint32_t address,uint16_t value)
{
    assert(address>=SH03_APP_BASE && address<SH03_ACTIVE_BASE+SH03_PAGE_SIZE && !(address&1));
    unsigned i=index_of(address);uint16_t old=sh03_get16(flash+i);
    assert(old==0xffff || old==value);event();
    sh03_put16(flash+i,old&(value|0x5555));event();sh03_put16(flash+i,value);event();return 1;
}
static void manifest(uint32_t address,const uint8_t *data,unsigned size)
{
    uint8_t *p=flash+index_of(address);
    sh03_put32(p,SH03_IMAGE_MAGIC);sh03_put32(p+4,1);sh03_put32(p+8,SH03_APP_BASE);
    sh03_put32(p+12,size);sh03_put32(p+16,sh03_image_crc(data,size));sh03_put32(p+20,SH03_LAYOUT);
    sh03_put32(p+24,sh03_image_crc(p,24));sh03_put32(p+28,SH03_IMAGE_COMMIT);
}
static void create_image(unsigned size,unsigned seed)
{
    for (unsigned i=0;i<size;++i) image[i]=(uint8_t)(i*73+seed);
    sh03_put32(image,0x20010000);sh03_put32(image+4,SH03_APP_BASE+305);
}
static void begin_payload(uint8_t *p,unsigned size)
{
    sh03_put32(p,SH03_APP_BASE);sh03_put32(p+4,size);
    sh03_put32(p+8,sh03_image_crc(image,size));sh03_put32(p+12,SH03_LAYOUT);
}
static void download(Sh03Update *u,unsigned size)
{
    uint8_t p[64];begin_payload(p,size);assert(sh03_update_begin(u,p)==0);
    while (u->received<size) {
        unsigned n=size-u->received;if(n>60)n=60;
        sh03_put32(p,u->received);memcpy(p+4,image+u->received,n);
        assert(sh03_update_data(u,p,n+4)==0);
    }
}
static void transaction(unsigned size)
{
    Sh03Update u={0};download(&u,size);assert(sh03_update_end(&u)==0);
    assert(sh03_install_pending());assert(sh03_active_valid());
}
static void fault_injection(void)
{
    memset(flash,0xff,sizeof flash);memset(flash,0xa7,0x2000);
    create_image(512,1);memcpy(flash+0x2000,image,512);manifest(SH03_ACTIVE_BASE,image,512);
    memcpy(snapshot,flash,sizeof flash);create_image(768,2);
    events=0;cut=0;transaction(768);unsigned total=events;
    for (fault_mode=0;fault_mode<2;++fault_mode) for (unsigned stop=1;stop<=total;++stop) {
        memcpy(flash,snapshot,sizeof flash);events=0;cut=stop;
        if (!setjmp(power)) { transaction(768);assert(0); }
        cut=0; /* Reboot: volatile transfer state is gone. */
        assert(!memcmp(flash,snapshot,0x2000));
        assert(sh03_install_pending());assert(sh03_active_valid());
        uint32_t size=sh03_get32(sh03_flash_at(SH03_ACTIVE_BASE)+12);
        if (size==512) assert(!memcmp(flash+0x2000,snapshot+0x2000,512));
        else { assert(size==768);assert(!memcmp(flash+0x2000,image,768)); }
        /* After any interrupted transfer, a fresh complete upload still works. */
        transaction(768);assert(!memcmp(flash+0x2000,image,768));
        assert(!memcmp(flash,snapshot,0x2000));
    }
    printf("PASS: %u power cuts (before/during/after every erase and half-word write, two torn erase patterns), reboot and re-upload\n",total*2);
}
static void boundaries(void)
{
    memcpy(flash,snapshot,sizeof flash);create_image(512,3);Sh03Update u={0};uint8_t p[64];
    begin_payload(p,512);unsigned before=events;
    const uint32_t sizes[]={0,4,303,305,SH03_SLOT_SIZE+4,0xfffffffc};
    for (unsigned i=0;i<sizeof sizes/sizeof *sizes;++i) {
        sh03_put32(p+4,sizes[i]);assert(sh03_update_begin(&u,p)==SH03_UPDATE_RANGE);
    }
    begin_payload(p,512);p[0]^=1;assert(sh03_update_begin(&u,p)==SH03_UPDATE_RANGE);
    begin_payload(p,512);p[12]^=1;assert(sh03_update_begin(&u,p)==SH03_UPDATE_RANGE);assert(events==before);
    begin_payload(p,512);assert(sh03_update_begin(&u,p)==0);
    before=events;assert(sh03_update_begin(&u,p)==0 && events==before); /* Duplicate begin. */
    assert(sh03_update_end(&u)==SH03_UPDATE_STATE);
    sh03_put32(p,0xffffffff);assert(sh03_update_data(&u,p,64)==SH03_UPDATE_RANGE);
    sh03_put32(p,512);assert(sh03_update_data(&u,p,64)==SH03_UPDATE_RANGE);
    sh03_put32(p,2);assert(sh03_update_data(&u,p,64)==SH03_UPDATE_RANGE);
    sh03_put32(p,0);memcpy(p+4,image,60);assert(sh03_update_data(&u,p,64)==0);
    before=events;assert(sh03_update_data(&u,p,64)==0 && events==before);p[5]^=1;
    assert(sh03_update_data(&u,p,64)==SH03_UPDATE_STATE);
    u.receiving=0;download(&u,512);flash[index_of(SH03_STAGE_BASE)+400]^=1;
    assert(sh03_update_end(&u)==SH03_UPDATE_CRC && sh03_active_valid());
    /* A correctly checksummed package with invalid vectors must still fail. */
    create_image(512,3);sh03_put32(image+4,0x08000001);u.receiving=0;download(&u,512);
    assert(sh03_update_end(&u)==SH03_UPDATE_CRC && sh03_active_valid());
    create_image(512,3);u.receiving=0;download(&u,512);assert(sh03_update_end(&u)==0);
    before=events;begin_payload(p,512);assert(sh03_update_begin(&u,p)==SH03_UPDATE_BUSY && events==before);
    /* Invalid staging after commit cannot trigger an application erase. */
    flash[index_of(SH03_STAGE_BASE)+400]^=1;before=events;assert(!sh03_install_pending() && events==before);
    assert(sh03_active_valid());
    /* No valid image: remain in recovery. */
    memset(flash+0x2000,0xff,sizeof flash-0x2000);assert(!sh03_install_pending());
    /* Maximum slot length and cross-page streaming. */
    create_image(SH03_SLOT_SIZE,7);transaction(SH03_SLOT_SIZE);assert(!memcmp(flash+0x2000,image,SH03_SLOT_SIZE));
    puts("PASS: bounds, duplicates, CRC/vectors, recovery, maximum 120 KiB image");
}
static void built_image(const char *factory,const char *package)
{
    FILE *f=fopen(factory,"rb");assert(f);assert(fread(flash,1,sizeof flash,f)==sizeof flash);fclose(f);
    assert(sh03_active_valid() && sh03_install_pending());memcpy(snapshot,flash,sizeof flash);
    f=fopen(package,"rb");assert(f);uint8_t header[32];assert(fread(header,1,32,f)==32);
    unsigned size=sh03_get32(header+12);assert(size<=sizeof image);assert(fread(image,1,size,f)==size);fclose(f);
    /* Exercise a different real-size image without affecting its vectors. */
    image[size-4]^=1;transaction(size);
    assert(!memcmp(flash,snapshot,0x2000) && !memcmp(flash+0x2000,image,size));
    puts("PASS: factory image and full built application installation");
}
int main(int argc,char **argv)
{
    assert(argc==3);assert(sh03_image_crc((const uint8_t *)"123456789",9)==0xcbf43926);
    fault_injection();boundaries();built_image(argv[1],argv[2]);return 0;
}
