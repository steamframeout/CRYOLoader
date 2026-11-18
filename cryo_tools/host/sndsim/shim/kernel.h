#pragma once
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef int32_t s32; typedef uint64_t u64;
typedef struct { int count, max_count, init_count, wait_threads; u32 attr, option; } ee_sema_t;
int CreateSema(ee_sema_t *s); int WaitSema(int id); int SignalSema(int id);
static inline void nopdelay(void) {}
#define UNCACHED_SEG(x) (x)
