#pragma once
#define AUDSRV_IRX 0x870884e
#define MAX_VOLUME 100
typedef struct audsrv_adpcm_t { int size; void *buffer; int loop; int pitch; int channels; } audsrv_adpcm_t;
int audsrv_adpcm_init(void);
int audsrv_load_adpcm(audsrv_adpcm_t *a, void *buf, int size);
int audsrv_free_adpcm(audsrv_adpcm_t *a);
int audsrv_ch_play_adpcm(int ch, audsrv_adpcm_t *a);
int audsrv_adpcm_set_volume_and_pan(int ch, int vol, int pan);
