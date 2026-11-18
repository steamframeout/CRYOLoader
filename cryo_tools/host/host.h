#ifndef HOST_H
#define HOST_H
#include "cryo.h"
#ifndef HOST_ASSETS
#define HOST_ASSETS "../../cryo_res" /* set by the Makefile */
#endif
void host_log_sound(const char *name, int mode, float pitch);
void host_after_frame(const float *fb, int w, int h);
void host_script_step(cpad_t *pad, uint32_t now);
void host_init_fb(int ps2);
void host_save_png(const char *path, const float *buf, int w, int h);
ctex *host_rgb_tex(int w, int h, const uint8_t *rgb);
void host_data_init(void);
#endif
