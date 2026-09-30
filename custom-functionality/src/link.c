#include "sh03_ui.h"
#include "sh03_protocol.h"
#include "sh03_link.h"
#include "sh03_display.h"
#ifdef SH03_OTA_APP
#include "sh03_update.h"
#endif

#define UART4 0x40004c00u
#define REG16(a) SH03_MEM(uint16_t,a)
#define RING_MASK 255u
static volatile uint8_t rx[256],tx[256],rx_error;
static volatile uint16_t rx_head,rx_tail,tx_head,tx_tail;
static Sh03WireParser parser;
static uint32_t last_rx,last_state,last_command;
static uint16_t state_sequence,cached_sequence;
static uint8_t cached_payload[6],cached_status,cache_valid,initialized;

/* No RTOS API or application work in the IRQ. An overflow invalidates the
 * entire pending input, rather than executing a frame with missing bytes. */
void sh03_uart4_irq(void)
{
    uint16_t status=REG16(UART4);
    if (status&0x2f) {
        uint8_t byte=(uint8_t)REG16(UART4+4); /* SR then DR clears RX/errors. */
        uint16_t next=(rx_head+1u)&RING_MASK;
        if ((status&0x0f) || next==rx_tail) rx_error=1;
        else if (status&0x20) { rx[rx_head]=byte; rx_head=next; }
    }
    if ((status&0x80) && (REG16(UART4+12)&0x80)) {
        if (tx_tail!=tx_head) { REG16(UART4+4)=tx[tx_tail]; tx_tail=(tx_tail+1u)&RING_MASK; }
        else REG16(UART4+12)&=(uint16_t)~0x80u;
    }
}

void sh03_link_init(void)
{
    rx_head=rx_tail=tx_head=tx_tail=0; rx_error=0; parser.used=0; cache_valid=0;
    last_rx=last_state=fw_ticks(); initialized=1;
    (void)REG16(UART4); (void)REG16(UART4+4);
    SH03_U8(0xe000e400u+52)=0x60; /* UART4 IRQ52, below FreeRTOS BASEPRI. */
    SH03_U32(0xe000e280u+4)=1u<<20; /* Clear pending IRQ52. */
    SH03_U32(0xe000e100u+4)=1u<<20;
    REG16(UART4+12)|=0x20; /* RXNEIE; UART is already configured by board.c. */
}

static void send_frame(uint8_t type,uint16_t sequence,const uint8_t *payload,uint8_t length)
{
    uint8_t frame[SH03_WIRE_FRAME];
    unsigned n=(unsigned)sh03_wire_encode(frame,type,sequence,payload,length);
    fw_enter_critical();
    unsigned free_bytes=(tx_tail-tx_head-1u)&RING_MASK;
    if (n<=free_bytes) {
        for (unsigned i=0;i<n;++i) { tx[tx_head]=frame[i]; tx_head=(tx_head+1u)&RING_MASK; }
        REG16(UART4+12)|=0x80;
    }
    fw_exit_critical();
}

static void send_state(uint8_t ch)
{
    uint8_t p[SH03_STATE_SIZE]={0};
    Sh03SensorSnapshot s;
    /* Snapshot writers do not yield inside their scalar updates. */
    fw_enter_critical();
    sh03_sensor_snapshot(&s,ch);
    uint8_t enabled=sh03_ui_running(ch);
    Sh03ChamberUi *ui=&sh03_ui.chambers[ch];
    Sh03DryingMessage *m=&sh03_messages[ch];
    uint32_t duration=enabled?m->duration_seconds:ui->duration_seconds;
    uint32_t elapsed=sh03_elapsed(ch);
    p[0]=ch; p[1]=(enabled?SH03_FLAG_ENABLED:0) |
        (sh03_controls[ch].running?SH03_FLAG_DRYING:0) |
        (sh03_controls[ch].countdown_enabled?SH03_FLAG_COUNTDOWN:0) |
        (SH03_U8(0x20000080+ch*0x50u)?SH03_FLAG_FAN:0) |
        SH03_FLAG_DISPLAY_CONTROL |
        (sh03_display_enabled()?SH03_FLAG_DISPLAY_ENABLED:0);
    p[2]=sh03_mode(ch); p[3]=enabled?m->material:ui->material;
    p[4]=enabled?m->target_temperature:ui->target_temperature;
    p[5]=enabled?m->humidity_threshold:ui->humidity_threshold;
    p[6]=SH03_U8(0x2000002c+2u*ch); p[7]=sh03_heater_percent[ch];
    fw_exit_critical();
    /* The displayed/filtered temperature is the useful value for HA. */
    sh03_put16(p+8,(uint16_t)(int16_t)(s.filtered_temperature*10));
    sh03_put16(p+10,(uint16_t)s.humidity);
    sh03_put16(p+12,(uint16_t)(int16_t)s.heater_temperature);
    sh03_put32(p+14,(p[1]&SH03_FLAG_FAN)?s.fan_rpm:0);
    sh03_put32(p+18,duration); sh03_put32(p+22,elapsed);
    sh03_put32(p+26,elapsed<duration?duration-elapsed:0);
    send_frame(SH03_STATE,++state_sequence,p,sizeof p);
}

#ifdef SH03_OTA_APP
/* Called under the UI mutex + scheduler critical section. Check both actual
 * control state and task state, so queued starts/stops cannot race the reset. */
uint8_t sh03_update_allowed(void)
{
    if (fw_ticks()<2000 || sh03_setting || sh03_diagnostic) return SH03_UPDATE_BUSY;
    for (unsigned ch=0;ch<2;++ch) {
        void *task=sh03_drying_task(ch);
        if (sh03_ui_running(ch) || sh03_controls[ch].running || sh03_heater_percent[ch] ||
            (task && fw_task_state(task)!=3)) return SH03_UPDATE_BUSY;
    }
    return SH03_UPDATE_OK;
}
static void boot_message(uint8_t op,uint16_t sequence)
{
    uint8_t p[SH03_BOOT_REPLY_SIZE];
    const uint8_t *manifest=(const uint8_t *)SH03_ACTIVE_BASE;
    uint8_t status=SH03_UPDATE_BUSY;
    if (op==SH03_BOOT_INFO) {
        sh03_boot_reply(p,op,SH03_UPDATE_OK,0,sh03_get32(manifest+12),sh03_get32(manifest+16));
        send_frame(SH03_BOOT_REPLY,sequence,p,sizeof p); return;
    }
    if (fw_semaphore_take(sh03_ui_mutex,0)==1) {
        fw_enter_critical();
        status=sh03_update_allowed();
        if (status==SH03_UPDATE_OK) {
            /* Finish queued UART bytes and the ACK synchronously. Interrupts
             * stay masked through reset; no panel/task can restart a heater. */
            __asm volatile("cpsid i");
            uint8_t frame[SH03_WIRE_FRAME];
            sh03_boot_reply(p,op,0,0,0,0);
            unsigned n=sh03_wire_encode(frame,SH03_BOOT_REPLY,sequence,p,sizeof p);
            while (tx_tail!=tx_head) {
                for (unsigned wait=0;wait<100000 && !(REG16(UART4)&0x80);++wait) { }
                REG16(UART4+4)=tx[tx_tail]; tx_tail=(tx_tail+1u)&RING_MASK;
            }
            for (unsigned i=0;i<n;++i) {
                for (unsigned wait=0;wait<100000 && !(REG16(UART4)&0x80);++wait) { }
                REG16(UART4+4)=frame[i];
            }
            for (unsigned wait=0;wait<100000 && !(REG16(UART4)&0x40);++wait) { }
            __asm volatile("dsb");
            SH03_U32(0xe000ed0c)=0x05fa0004; /* SYSRESETREQ, bootloader starts. */
            __asm volatile("dsb");
            for (;;) { }
        }
        fw_exit_critical(); sh03_mutex_give(sh03_ui_mutex);
    }
    sh03_boot_reply(p,op,status,0,0,0); send_frame(SH03_BOOT_REPLY,sequence,p,sizeof p);
}
#endif

void sh03_link_poll(void)
{
    if (!initialized) return;
    uint8_t frame[SH03_WIRE_FRAME];
    uint32_t now=fw_ticks();
    if (now-last_rx>500) parser.used=0;
    fw_enter_critical();
    if (rx_error) { rx_tail=rx_head; rx_error=0; parser.used=0; }
    fw_exit_critical();
    /* Bound receive work and handle at most one complete frame per UI pass. */
    for (unsigned count=0;count<128 && rx_tail!=rx_head;++count) {
        uint8_t byte=rx[rx_tail]; rx_tail=(rx_tail+1u)&RING_MASK; last_rx=now;
        if (!sh03_wire_feed(&parser,byte,frame)) continue;
#ifdef SH03_OTA_APP
        if ((frame[3]==SH03_BOOT_ENTER || frame[3]==SH03_BOOT_INFO) && frame[6]==0) {
            boot_message(frame[3],sh03_get16(frame+4)); break;
        }
#endif
        if (frame[3]==SH03_QUERY && frame[6]==0) last_state=now-1000;
        else if (frame[3]==SH03_COMMAND && frame[6]==6) {
            uint16_t sequence=sh03_get16(frame+4);
            uint8_t *p=frame+7,status=SH03_BUSY;
            int duplicate=cache_valid && cached_sequence==sequence && now-last_command<5000;
            for (unsigned i=0;i<6;++i) if (cached_payload[i]!=p[i]) duplicate=0;
            if (duplicate) status=cached_status;
            else {
                if (fw_semaphore_take(sh03_ui_mutex,0)==1) {
                    status=sh03_remote_command(p[0],p[1],sh03_get32(p+2));
                    sh03_mutex_give(sh03_ui_mutex);
                }
                cache_valid=1; cached_sequence=sequence; cached_status=status; last_command=now;
                for (unsigned i=0;i<6;++i) cached_payload[i]=p[i];
            }
            uint8_t ack[3]={p[0],p[1],status};
            send_frame(SH03_ACK,sequence,ack,sizeof ack);
            last_state=now-1000;
        }
        break;
    }
    if (now-last_state>=1000) {
        last_state=now; send_state(0); send_state(1);
        /* Legacy clients still showing Display auto-off must see disabled.
         * New clients ignore this frame; the inactivity timer was removed. */
        const uint8_t config[2]={0,0};
        send_frame(SH03_DISPLAY_CONFIG,++state_sequence,config,sizeof config);
    }
}
