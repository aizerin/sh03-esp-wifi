#ifndef SH03_UPDATE_H
#define SH03_UPDATE_H
#include "sh03_protocol.h"

/* F1 high-density, 256 KiB flash, 2 KiB erase pages. Layout version 1. */
#define SH03_APP_BASE 0x08002000u
#define SH03_SLOT_SIZE 0x1e000u
#define SH03_STAGE_BASE 0x08020000u
#define SH03_PENDING_BASE 0x0803e000u
#define SH03_ACTIVE_BASE 0x0803e800u
#define SH03_PAGE_SIZE 0x800u
#define SH03_LAYOUT 0x00010303u
#define SH03_IMAGE_MAGIC 0x55333053u /* "S03U" */
#define SH03_IMAGE_COMMIT 0x5aa5a55au
#define SH03_MANIFEST_SIZE 32u
#define SH03_BOOT_REPLY_SIZE 18u
enum { SH03_BOOT_ENTER=0x10, SH03_BOOT_HELLO, SH03_BOOT_BEGIN,
       SH03_BOOT_DATA, SH03_BOOT_END, SH03_BOOT_RUN, SH03_BOOT_INFO=0x17,
       SH03_BOOT_REPLY=0x18 };
enum { SH03_UPDATE_OK=0, SH03_UPDATE_RANGE, SH03_UPDATE_BUSY,
       SH03_UPDATE_CRC, SH03_UPDATE_FLASH, SH03_UPDATE_STATE, SH03_UPDATE_HARDWARE };

static inline void sh03_boot_reply(uint8_t *p,uint8_t op,uint8_t status,
                                   uint32_t offset,uint32_t size,uint32_t crc)
{
    p[0]=op; p[1]=status; sh03_put32(p+2,offset); sh03_put32(p+6,size);
    sh03_put32(p+10,crc); sh03_put32(p+14,SH03_LAYOUT);
}

/* Portable transaction core. Only these three functions touch flash. */
const uint8_t *sh03_flash_at(uint32_t address);
int sh03_flash_erase(uint32_t address);
int sh03_flash_program(uint32_t address,uint16_t value);
uint32_t sh03_image_crc(const uint8_t *data,uint32_t size);
int sh03_manifest_valid(uint32_t address);
int sh03_active_valid(void);
int sh03_install_pending(void);
typedef struct { uint32_t size,crc,received; uint8_t receiving; } Sh03Update;
uint8_t sh03_update_begin(Sh03Update *u,const uint8_t *payload);
uint8_t sh03_update_data(Sh03Update *u,const uint8_t *payload,uint8_t size);
uint8_t sh03_update_end(Sh03Update *u);
#endif
