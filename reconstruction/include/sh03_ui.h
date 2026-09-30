#ifndef SH03_UI_H
#define SH03_UI_H
#include "sh03_runtime.h"

typedef struct {
    uint8_t fault_shown, humidity, humidity_threshold, material;
    uint8_t target_temperature, temperature, reserved[2];
    uint32_t duration_seconds;
    uint8_t selected, humidity_icon, running_icon, setting_item;
} Sh03ChamberUi;
typedef struct {
    uint8_t chamber, blink, reserved[2];
    Sh03ChamberUi chambers[2];
} Sh03Ui;
typedef struct {
    char name[5];
    uint8_t temperature;
    uint16_t duration_seconds;
} Sh03Material;
_Static_assert(sizeof(Sh03ChamberUi) == 16, "chamber UI ABI");
_Static_assert(sizeof(Sh03Ui) == 36, "UI ABI");
_Static_assert(sizeof(Sh03Material) == 8, "material ABI");
#define sh03_ui (*(Sh03Ui *)0x20000370)
#define sh03_setting SH03_U8(0x20000018)
#define sh03_materials ((const Sh03Material *)0x08010bac)
#define sh03_ui_mutex SH03_HANDLE(0x2000033c)

#define fw_ui_initialize SH03_FN(0x08004dd4, void, Sh03Ui *, uint8_t)
#define fw_drying_command SH03_FN(0x08001f54, uint8_t, uint8_t, uint8_t)
#define fw_clear_ui_fault SH03_FN(0x08001db8, void, uint8_t)
#define fw_copy_settings SH03_FN(0x08001e70, void, uint8_t, uint8_t)
#define fw_display_setting SH03_FN(0x08002f60, void, const Sh03ChamberUi *, uint8_t, uint8_t)
#define fw_display_material SH03_FN(0x08002ea0, void, uint8_t, uint8_t, uint8_t)
#define fw_display_humidity SH03_FN(0x08002de4, void, uint8_t, uint8_t, uint8_t)
#define fw_display_temperature SH03_FN(0x080030d8, void, uint8_t, uint8_t, uint8_t)
#define fw_display_time SH03_FN(0x08003168, void, uint8_t, uint8_t, uint8_t, uint8_t)
#define fw_display_render SH03_FN(0x08005f84, void, const void *, const Sh03Ui *, uint8_t)
#define fw_queue_create SH03_FN(0x0800e228, void *, uint32_t, uint32_t, uint8_t)
#define fw_event_wait SH03_FN(0x0800e07c, uint32_t, void *, uint32_t, int32_t, int32_t, uint32_t)
#define fw_timer_active SH03_FN(0x0800f894, int32_t, void *)
#define fw_timer_create SH03_FN(0x0800f77c, void *, const char *, uint32_t, uint32_t, void *, void (*)(void *))
#define fw_timer_command SH03_FN(0x0800f818, int32_t, void *, int32_t, uint32_t, int32_t *, uint32_t)
#define fw_notify SH03_FN(0x0800ebe0, int32_t, void *, uint32_t, uint32_t, uint8_t, uint32_t *)
#define fw_notify_wait SH03_FN(0x0800eda0, int32_t, uint32_t, uint32_t, uint32_t, uint32_t *, uint32_t)
#define fw_task_delete SH03_FN(0x0800d418, void, void *)
#define fw_delete_task_handle SH03_FN(0x08002bc0, void, void **)
#define fw_bus_initialize SH03_FN(0x0800bc24, void, void *)
#define fw_get_diagnostic SH03_FN(0x08004434, uint8_t, void)
#define fw_set_diagnostic SH03_FN(0x080068f4, void, uint8_t)

void sh03_ui_initialize(Sh03Ui *state, uint8_t chambers);
void sh03_clear_setting_callback(void *timer);
void sh03_blink_callback(void *timer);
void sh03_clear_ui_fault(uint8_t chamber);
void sh03_copy_settings(uint8_t source, uint8_t destination);
void sh03_display_setting(const Sh03ChamberUi *, uint8_t chamber, uint8_t visible);
void sh03_task_interactive(void *argument);
#endif
