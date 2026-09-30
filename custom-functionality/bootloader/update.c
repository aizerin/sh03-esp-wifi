#include "sh03_update.h"

uint32_t sh03_image_crc(const uint8_t *data,uint32_t size)
{
    uint32_t crc=0xffffffffu;
    while (size--) {
        crc^=*data++;
        for (unsigned bit=0;bit<8;++bit) crc=(crc>>1)^((crc&1)?0xedb88320u:0);
    }
    return ~crc;
}
static int valid_size(uint32_t size) { return size>=76*4 && size<=SH03_SLOT_SIZE && !(size&3); }
static int vectors_valid(uint32_t base,uint32_t size)
{
    const uint8_t *p=sh03_flash_at(base);
    uint32_t sp=sh03_get32(p),reset=sh03_get32(p+4);
    return sp>0x20000700u && sp<=0x20010000u && !(sp&7) && (reset&1) &&
           (reset&~1u)>=SH03_APP_BASE+76*4 && (reset&~1u)<SH03_APP_BASE+size;
}
int sh03_manifest_valid(uint32_t address)
{
    const uint8_t *p=sh03_flash_at(address);
    return sh03_get32(p)==SH03_IMAGE_MAGIC && sh03_get32(p+4)==1 &&
           sh03_get32(p+8)==SH03_APP_BASE && valid_size(sh03_get32(p+12)) &&
           sh03_get32(p+20)==SH03_LAYOUT && sh03_get32(p+28)==SH03_IMAGE_COMMIT &&
           sh03_get32(p+24)==sh03_image_crc(p,24);
}
static int image_valid(uint32_t base,uint32_t manifest)
{
    if (!sh03_manifest_valid(manifest)) return 0;
    const uint8_t *p=sh03_flash_at(manifest);
    uint32_t size=sh03_get32(p+12);
    return vectors_valid(base,size) && sh03_image_crc(sh03_flash_at(base),size)==sh03_get32(p+16);
}
int sh03_active_valid(void) { return image_valid(SH03_APP_BASE,SH03_ACTIVE_BASE); }
static int program(uint32_t address,const uint8_t *data,uint32_t size)
{
    for (uint32_t i=0;i<size;i+=2)
        if (!sh03_flash_program(address+i,sh03_get16(data+i))) return 0;
    return 1;
}
static int manifest_write(uint32_t address,uint32_t size,uint32_t crc)
{
    uint8_t p[SH03_MANIFEST_SIZE];
    sh03_put32(p,SH03_IMAGE_MAGIC); sh03_put32(p+4,1); sh03_put32(p+8,SH03_APP_BASE);
    sh03_put32(p+12,size); sh03_put32(p+16,crc); sh03_put32(p+20,SH03_LAYOUT);
    sh03_put32(p+24,sh03_image_crc(p,24)); sh03_put32(p+28,SH03_IMAGE_COMMIT);
    /* Commit is written LAST. An incomplete record is never actionable. */
    return program(address,p,sizeof p) && sh03_manifest_valid(address);
}
int sh03_install_pending(void)
{
    if (!sh03_manifest_valid(SH03_PENDING_BASE)) return sh03_active_valid();
    if (!image_valid(SH03_STAGE_BASE,SH03_PENDING_BASE)) return 0;
    const uint8_t *p=sh03_flash_at(SH03_PENDING_BASE);
    uint32_t size=sh03_get32(p+12),crc=sh03_get32(p+16);
    const uint8_t *active=sh03_flash_at(SH03_ACTIVE_BASE);
    if (!sh03_active_valid() || sh03_get32(active+12)!=size || sh03_get32(active+16)!=crc) {
        /* Invalidate the old manifest BEFORE altering any application bytes.
         * Pending manifest + staging remain intact throughout this operation.
         * After reset, copying from the beginning is safe and idempotent. */
        if (!sh03_flash_erase(SH03_ACTIVE_BASE)) return 0;
        for (uint32_t offset=0;offset<SH03_SLOT_SIZE;offset+=SH03_PAGE_SIZE)
            if (!sh03_flash_erase(SH03_APP_BASE+offset)) return 0;
        if (!program(SH03_APP_BASE,sh03_flash_at(SH03_STAGE_BASE),size)) return 0;
        if (!vectors_valid(SH03_APP_BASE,size) || sh03_image_crc(sh03_flash_at(SH03_APP_BASE),size)!=crc) return 0;
        if (!manifest_write(SH03_ACTIVE_BASE,size,crc) || !sh03_active_valid()) return 0;
    }
    /* A torn erase here is harmless: active already verifies, staging survives. */
    return sh03_flash_erase(SH03_PENDING_BASE);
}
uint8_t sh03_update_begin(Sh03Update *u,const uint8_t *p)
{
    uint32_t size=sh03_get32(p+4),crc=sh03_get32(p+8);
    if (sh03_get32(p)!=SH03_APP_BASE || sh03_get32(p+12)!=SH03_LAYOUT || !valid_size(size)) return SH03_UPDATE_RANGE;
    /* Lost BEGIN ACK: do not erase data already received for the same image. */
    if (u->receiving && u->size==size && u->crc==crc) return SH03_UPDATE_OK;
    u->receiving=0;
    /* Refuse to discard an installation that is still recoverable. */
    if (image_valid(SH03_STAGE_BASE,SH03_PENDING_BASE)) return SH03_UPDATE_BUSY;
    if (!sh03_flash_erase(SH03_PENDING_BASE)) return SH03_UPDATE_FLASH;
    for (uint32_t offset=0;offset<size;offset+=SH03_PAGE_SIZE)
        if (!sh03_flash_erase(SH03_STAGE_BASE+offset)) return SH03_UPDATE_FLASH;
    u->size=size; u->crc=crc; u->received=0; u->receiving=1;
    return SH03_UPDATE_OK;
}
uint8_t sh03_update_data(Sh03Update *u,const uint8_t *p,uint8_t size)
{
    if (!u->receiving) return SH03_UPDATE_STATE;
    if (size<6 || size>64 || (size&1)) return SH03_UPDATE_RANGE;
    uint32_t offset=sh03_get32(p),n=size-4;
    if ((offset&1) || offset>u->size || n>u->size-offset) return SH03_UPDATE_RANGE;
    if (offset<u->received) {
        if (n>u->received-offset) return SH03_UPDATE_RANGE;
        const uint8_t *stored=sh03_flash_at(SH03_STAGE_BASE+offset);
        for (uint32_t i=0;i<n;++i) if (stored[i]!=p[4+i]) return SH03_UPDATE_STATE;
        return SH03_UPDATE_OK;
    }
    if (offset!=u->received) return SH03_UPDATE_RANGE;
    if (!program(SH03_STAGE_BASE+offset,p+4,n)) { u->receiving=0; return SH03_UPDATE_FLASH; }
    u->received+=n; return SH03_UPDATE_OK;
}
uint8_t sh03_update_end(Sh03Update *u)
{
    if (!u->receiving || u->received!=u->size) return SH03_UPDATE_STATE;
    if (!vectors_valid(SH03_STAGE_BASE,u->size) || sh03_image_crc(sh03_flash_at(SH03_STAGE_BASE),u->size)!=u->crc) return SH03_UPDATE_CRC;
    if (!manifest_write(SH03_PENDING_BASE,u->size,u->crc)) { u->receiving=0; return SH03_UPDATE_FLASH; }
    u->receiving=0; return SH03_UPDATE_OK;
}
