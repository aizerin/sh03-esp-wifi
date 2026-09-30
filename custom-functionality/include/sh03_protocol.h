#ifndef SH03_PROTOCOL_H
#define SH03_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>

/* UART v1: A5 5A version type sequence-LE16 length payload CRC-LE16.
 * CRC-16/CCITT-FALSE covers version through payload (not sync bytes).
 * Wire integers are explicit little endian; never serialize C structures. */
#define SH03_WIRE_VERSION 1
#define SH03_WIRE_PAYLOAD 64
#define SH03_WIRE_FRAME (SH03_WIRE_PAYLOAD + 9)
enum { SH03_STATE = 1, SH03_COMMAND = 2, SH03_ACK = 3, SH03_QUERY = 4,
       SH03_DISPLAY_CONFIG = 5 }; /* Legacy timeout report: always zero. */
enum { SH03_OP_RUN = 1, SH03_OP_TEMPERATURE, SH03_OP_DURATION,
       SH03_OP_MODE, SH03_OP_HUMIDITY, SH03_OP_MATERIAL, SH03_OP_DISPLAY,
       SH03_OP_DISPLAY_TIMEOUT }; /* Deprecated: only value 0 is accepted. */
enum { SH03_OK = 0, SH03_RANGE, SH03_BUSY, SH03_FAULT, SH03_NOT_READY, SH03_UNSUPPORTED };
enum { SH03_FLAG_ENABLED = 1, SH03_FLAG_DRYING = 2, SH03_FLAG_COUNTDOWN = 4, SH03_FLAG_FAN = 8,
       SH03_FLAG_DISPLAY_ENABLED = 16, SH03_FLAG_DISPLAY_CONTROL = 32 };
#define SH03_STATE_SIZE 30

static inline uint16_t sh03_get16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static inline uint32_t sh03_get32(const uint8_t *p) { return (uint32_t)sh03_get16(p) | ((uint32_t)sh03_get16(p+2) << 16); }
static inline void sh03_put16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static inline void sh03_put32(uint8_t *p, uint32_t v) { sh03_put16(p,(uint16_t)v); sh03_put16(p+2,(uint16_t)(v>>16)); }
static inline uint16_t sh03_wire_crc(const uint8_t *p, size_t n)
{
    uint16_t crc=0xffff;
    while (n--) {
        crc ^= (uint16_t)*p++ << 8;
        for (unsigned i=0;i<8;++i) crc=(uint16_t)((crc<<1)^((crc&0x8000)?0x1021:0));
    }
    return crc;
}
static inline size_t sh03_wire_encode(uint8_t *out, uint8_t type, uint16_t sequence,
                                     const uint8_t *payload, uint8_t length)
{
    if (length>SH03_WIRE_PAYLOAD) return 0;
    out[0]=0xa5; out[1]=0x5a; out[2]=SH03_WIRE_VERSION; out[3]=type;
    sh03_put16(out+4,sequence); out[6]=length;
    for (unsigned i=0;i<length;++i) out[7+i]=payload[i];
    sh03_put16(out+7+length,sh03_wire_crc(out+2,5+length));
    return (size_t)length+9;
}
typedef struct { uint8_t bytes[SH03_WIRE_FRAME]; uint8_t used; } Sh03WireParser;
/* Sliding resynchronization also recovers a valid frame nested after corrupt
 * length/CRC bytes. Callers reset used after a >500 ms inter-byte timeout. */
static inline void sh03_wire_shift(Sh03WireParser *p)
{
    for (unsigned i=1;i<p->used;++i) p->bytes[i-1]=p->bytes[i];
    --p->used;
}
static inline int sh03_wire_feed(Sh03WireParser *p, uint8_t byte, uint8_t *frame)
{
    if (p->used==SH03_WIRE_FRAME) sh03_wire_shift(p);
    p->bytes[p->used++]=byte;
    while (p->used) {
        if (p->bytes[0]!=0xa5 || (p->used>1 && p->bytes[1]!=0x5a) ||
            (p->used>2 && p->bytes[2]!=SH03_WIRE_VERSION) ||
            (p->used>6 && p->bytes[6]>SH03_WIRE_PAYLOAD)) { sh03_wire_shift(p); continue; }
        if (p->used<7) return 0;
        unsigned n=p->bytes[6]+9u;
        if (p->used<n) return 0;
        if (sh03_get16(p->bytes+n-2)!=sh03_wire_crc(p->bytes+2,n-4)) { sh03_wire_shift(p); continue; }
        for (unsigned i=0;i<n;++i) frame[i]=p->bytes[i];
        for (unsigned i=n;i<p->used;++i) p->bytes[i-n]=p->bytes[i];
        p->used=(uint8_t)(p->used-n);
        return 1;
    }
    return 0;
}
#endif
