#ifndef SH03_BOARD_H
#define SH03_BOARD_H
/* Explicit profiles: ST-Link's 0x414 lookup alone does not identify the core
 * or actual SRAM. The M4 board was measured through SWD, without inferring
 * a manufacturer or part number from the compatible debug ID. Keep Thumb-2
 * soft-float code and the existing peripheral layout for both profiles. */
#ifdef SH03_BOARD_M4_48K
#define SH03_RAM_END 0x2000c000u
#define SH03_CORE_ID 0x410fc240u
#else
#define SH03_RAM_END 0x20010000u
#define SH03_CORE_ID 0x410fc230u
#endif
#endif
