/* Cryo Loader: the few things OPL's own files need to know about the Cryo front end. */
#ifndef CRYO_OPL_H
#define CRYO_OPL_H

/* nonzero when the Cryo front end is in charge (always, in this build) */
extern int gCryo;

/* ---- sound (sound.c) ---- */
/* menu sound setup: pitch ratio against the recorded sounds, mode 0 PS2, 1 soft, 2 off.
   A pitch change re-uploads the menu sounds (the boot sound is left alone). */
void sfxCryoSetup(float ratio, int mode);
/* id: 0..9, Cryo's CS_MOVE..CS_LAUNCH */
void sfxCryoPlay(int id);
/* the boot sound, in stereo; a copy still playing fades out quickly first */
void sfxCryoPlayBoot(void);
/* fades: the boot sound only, or everything that may still be sounding (non-blocking) */
void sfxCryoBootFade(int ms);
void sfxCryoFadeAll(int ms);
/* fades everything and waits until it is silent (before audio is shut down) */
void sfxCryoQuiet(int ms);
/* once per frame: moves fades along */
void sfxCryoTick(void);
/* nonzero while the boot sound can still be heard */
int sfxCryoBootAudible(void);
/* nonzero once the sounds are loaded */
int sfxCryoReady(void);
int sfxCryoRpcState(void);
/* Cryo Loader: scales a freshly loaded cover down to the size Cryo shows (texcache.c, IO thread) */
struct gsTexture;
void cryoFitArt(struct gsTexture *t, int userId);
/* the same scaling fed row by row (the PNG loader decodes art straight into it) */
void *cryoFitStart(int userId, int w, int h, int bpp, int *ow, int *oh);
void cryoFitRow(void *f, const unsigned char *row);
unsigned char *cryoFitFinish(void *f);
/* textures.c: art loads on the calling thread are scaled for this cache while they decode (0: off) */
void texSetArtFit(int userId);
/* called by sound.c in place of its own code when gCryo is set */
int cryoSfxInit(void);
void cryoSfxPlayOpl(int id);
int cryoBootBusy(void);
int cryoBootScreenDone(void);
void sfxCryoBootChanged(void); /* the boot sound file setting changed: load it now (cryo_sound.c) */ /* the boot sound is playing, or waiting to (cryo_ps2.c) */
void cryoSfxEnd(void);
void cryoSfxLevels(void);

/* ---- hooks into OPL's screens (cryo_ps2.c) ---- */
void cryoOplInit(void);
void cryoOplFrame(void);
void cryoOplInput(void);
void cryoGfxFrameStart(void);
/* start-up trace: on when SELECT is held as the ELF starts (see cryo_ps2.c) */
extern int gCryoTrace;
void cp_debug(const char *fmt, ...); /* trace log (cryo_ps2.c); safe from the I/O thread */
void cryoTrace(int stage);
int cryoMsgBox(const char *text, int addAccept);
void cryoTextScreen(const char *msg);
int cryoShowKeyb(char *text, int maxLen, int hide, const char *title);
/* draws one frame of whatever Cryo is showing, with a short message over it (NULL: none) */
/* (cryoTextScreen above) */
/* the cheat picker before a game starts (cheat mode "select"): toggles gCheats[].enabled */
void cryoManageCheats(int count);

#endif
