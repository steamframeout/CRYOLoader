# Cryo Loader

A front end for Open PS2 Loader. Cover flow and list library, tags read from file names, a
detail page for each game, themes in plain text files. Game loading, device drivers, VMC, cheats,
GSM, PADEMU and the network code are upstream OPL; only the interface changes.

Fork of OPL 1.2.0 Beta, commit `b3a2199` (ps2homebrew/Open-PS2-Loader).

## Install

1. Copy `CRYOLDR.ELF` to a memory card or USB drive and start it from FreeMcBoot, OSDMenu,
   uLaunchELF or wLaunchELF, like any OPL build.
2. Your games, `ART`, `CFG`, `CHT` and `VMC` folders stay exactly as OPL uses them. OPL's own
   settings (`conf_opl.cfg` and the rest) are read and kept.

## Where things are kept

| What | Where |
|---|---|
| Cryo's settings, tag and name corrections, "last played" times, last device | `conf_cryo.cfg`, next to OPL's config: `mc0:OPL/` by default, or wherever OPL keeps its config |
| OPL's settings (video, devices, network, audio volumes, parental lock) | OPL's usual files, same place |
| Per-game settings (modes, VMC, GSM, cheats, pads) | OPL's usual `CFG` files on the game's device |
| Theme files | `mc0:OPL/CRYO/THEMES/` (or `mc1:`), or `CRYO/THEMES/` on a game device |
| Covers and backgrounds | OPL's `ART` folder: `<game ID>_COV.png`, `<game ID>_BG.jpg` |

Settings are written to the memory card when you close Settings and just before a game starts. A
game's own settings are written when you close its settings page.

## Good to know

- **Video is 480i NTSC or 480p, 60 Hz, 4:3** (Settings › Display › Video mode). 480p needs
  component cables or an HDMI adapter: nothing is interlaced, so text is sharper and nothing
  shimmers while it moves. A new mode has to be confirmed within 12 s or the old one comes back.
  Holding △ while the ELF starts forces 480i. On a PAL console the TV must accept 60 Hz. OPL's
  16:9 option is hidden; screen position and overscan still work.
- **Text entry uses the Cryo keyboard**, which replaces OPL's. It is what you type network
  addresses, the share name, user name, password and the parental lock password with.
  ✕ types, □ deletes, △ adds a space, START finishes, ○ cancels.
- **Boot sound**: Cryo's own, made by `cryo_res/src/make_boot.py` (synthesised, stereo, 6.4 s
  with its fade-out built in), or your own file (Settings › Audio › Boot sound file, below).
  Skipping the boot screen fades it out over 1.2 s; choosing a game fades it over 0.4 s. OPL's
  "Boot sound" switch and volume (Settings › Audio) apply. Background music starts after it.
- **Your own boot sound** (Settings › Audio › Boot sound file): the path of a mono `.adp` file,
  the format OPL themes use for `boot.adp` (make one with `adpenc` from the PS2SDK), up to 1.2 MB,
  such as `mc0:/CRYO/boot.adp`. Empty, missing or not readable: Cryo's own plays. The file is read
  when the sounds load at start-up, so keep it on the memory card. **Background music** is OPL's:
  an `.ogg` file set as "Music file" on the same page.
- **Auto start last played** (Settings › General, OPL's option): after the boot screen the game
  moves to the middle and a countdown shows under it. Any button stops it.
- **Parental lock**: as in OPL. Settings ask for the password once, until they are saved.
- Holding START while the ELF starts skips loading OPL's settings (OPL's own escape hatch).
- **Start-up trace**: hold SELECT while the ELF starts. A row of 8 squares at the bottom left
  turns white as start-up goes on: 1 graphics up, 2 sounds loaded, 3 themes read, 4 fonts,
  5 theme applied, 6 library built, 7 boot screen started. Once the main loop runs, a second row
  of small blue squares counts frames. If it stops, the picture stays: a photo tells where.

## Controls

| Button | Library | Game page | Settings |
|---|---|---|---|
| D-pad / left stick | move | move | move |
| ✕ (○ if Settings › Controls swaps them) | open the game | press the focused button | change, press |
| ○ (or ✕) | back | back to the library | back, close |
| L1 / R1 | device | — | page |
| L2 / R2 | 5 games at a time | — | — |
| Right stick | — | scroll the game info | scroll |
| SELECT | cover flow / list | — | — |
| START | Settings | Settings | — |

Outside the keyboard, □ and △ do nothing.

## Themes

A theme is a text file of `key=value` lines. Unknown keys and bad values are reported on the Theme
page; the only built-in theme is Cryo. A theme file can also set `background=wire` and `fx=plus`
(glow, bevels, reflections and a cursor glint).

```
name=Night Shift
author=you
bg=#000000
surface=#0b0b0b
fg=#e8ecff
dim=#7d86a8
accent=#3d6bff
accent_fg=#000000
line=#1d2440
font=Chakra Petch
font_display=Michroma
font_mono=Share Tech Mono
library=flow
```

| Key | Values |
|---|---|
| `name`, `author`, `wordmark` | text |
| `bg`, `surface`, `fg`, `dim`, `accent`, `accent_fg`, `line` | `#rrggbb` |
| `font`, `font_display`, `font_mono` | Chakra Petch, Michroma, Share Tech Mono, IBM Plex Sans, IBM Plex Mono |
| `text_scale` | 0.8 – 1.4 |
| `title_case` | normal, upper |
| `radius` | 0 – 16 |
| `stroke` | 1 – 3 |
| `library` | flow, list |
| `flow_style` | tilt, flat |
| `cover_size` | s, m, l |
| `detail_split` | 30 – 50 (cover column, percent) |
| `top_left`, `top_center`, `top_right`, `sub` | comma list of: view, device, sort, count, clock, brand, settings |
| `hints`, `keyart`, `fade` | on, off |
| `select_style` | brackets, outline, fill, underline |
| `background` | none, grid, dots, horizon (`image` is not supported on the console and shows none) |
| `background_dim` | 0 – 1 |
| `anim` | 0 – 2 (animation speed, 0 = off) |
| `transition` | slide, fade, zoom, none |
| `sounds` | ps2, soft, off |
| `sound_pitch` | 0.2 – 2 |

## Speed

- **Cover art** is scaled to the size it is shown at while it decodes, a row at a time: the full
  image is never in memory, so covers of any size up to 4096 px load (bigger than 1024 px used to
  be refused; interlaced PNGs that big still are). Decoding skips the PNG checksums. In PCSX2, a 480 × 680 cover loads in about three
  quarters of the time it took, a 140 × 200 one in about two thirds.
- **Cards drawn small** in the cover flow use a half-size copy of their art (`cg_tex_lod`): thin
  lines and lettering no longer break up as cards move, and the flow needs far less video memory.
- **Placeholder art** is drawn about 3 ms a frame instead of in one 15 to 20 ms go.
- **The boot logo** costs about 2 ms a frame to draw instead of 20.
- **Controllers**: OPL asked the IOP for the state of USB and Bluetooth pads four times a frame,
  and waited each time; it now asks twice a second (buttons of a pad that is there: every frame).
- **The boot screen** ends once every device has reported (about 4 s), the launch screen in under
  a second.
- **Trace mode** (SELECT at start) logs every frame over 20 ms with where the time went, and every
  cover load with its time, to `mc0:OPL/CRYOLOG.TXT`.

## What has been checked, and what has not

- **The interface** runs on a PC test rig (`cryo_tools/host`), which draws frames the way the GS
  does, at the console's 640 × 448 interlaced geometry. It runs against OPL's real settings
  dialogs. The main screens were compared with the prototype there: library (cover flow and
  list), game page, device menu, Settings, game settings, the name and tag editor, and the
  keyboard.
- **The sound code** (`src/cryo_sound.c`) was run against a simulation of audsrv and the SPU2,
  using the real ADPCM files (`cryo_tools/host/sndsim`). In every case tried, no voice is ever
  stopped while it can be heard, and fades have no audible steps.
- **The ELF** builds with the current PS2SDK and gsKit, without new warnings.
- **First run on a PS2**: the first build froze on a black screen as the boot screen began. Sound
  calls that do not wait for a reply need an end function, or the IOP never replies; the first
  boot sound then waited for ever. Fixed, every wait now has a time limit, and the sound
  simulator now behaves like PS2SDK here so it would catch it.
- **Not yet seen on hardware**: GS colour and alpha on real textures, video memory with a full
  cover flow, frame rate, memory card writes, real device lists and cover loading, the network.

## Build

With the ps2dev toolchain (PS2SDK + gsKit), in an OPL checkout at commit `b3a2199`:

```
git apply opl-cryo.patch
make
```

The patch changes 15 OPL files, each change guarded by `gCryo`, and adds:

- `src/cryo/`: the front end (`cryo_ps2.c` is the console side)
- `src/cryo_sound.c`: the menu sounds, the stereo boot sound and the fades
- `include/cryo_opl.h`: what OPL's files call
- `cryo_res/`: fonts (SIL Open Font License, texts included), sounds, and the script that
  synthesises the boot sound
- `cryo_tools/`: the PC test rig, the sound simulator and an ADPCM decoder

Rebuilding the boot sound (needs numpy and scipy):
`ADPENC=$PS2SDK/bin/adpenc python3 cryo_res/src/make_boot.py cryo_res/audio [preview.wav]`.

## Licences

OPL is under the Academic Free License 3.0, and so are these changes. The fonts are under the SIL
Open Font License 1.1.
