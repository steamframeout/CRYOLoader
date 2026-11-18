# PC test rig for the Cryo front end: runs the real core and OPL's real dialog definitions,
# draws frames the way the GS would (640x448 with -ps2) and writes PNGs and a sound log.
#   make && ./host [-ps2] tests/t1.txt out/
OPL ?= ../..
CORE = $(OPL)/src/cryo
SRC = host.c host_main.c host_data.c host_dia.c $(CORE)/cryo_base.c $(CORE)/cryo_font.c $(CORE)/cryo_icon.c $(CORE)/cryo_parse.c \
      $(CORE)/cryo_theme.c $(CORE)/cryo_nav.c $(CORE)/cryo_input.c $(CORE)/cryo_art.c $(CORE)/cryo_fx.c $(CORE)/cryo_ui.c $(CORE)/cryo_lib.c \
      $(CORE)/cryo_detail.c $(CORE)/cryo_boot.c $(CORE)/cryo_panel.c $(CORE)/cryo_settings.c $(CORE)/cryo_dia.c \
      $(OPL)/src/dialogs.c $(OPL)/src/lang_internal.c
CFLAGS = -D_GNU_SOURCE -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-format-truncation $(shell pkg-config --cflags freetype2) \
         -I$(CORE) -Ishim -Ishim/include -DHOST_ASSETS='"$(abspath $(OPL))/cryo_res"'
LIBS = $(shell pkg-config --libs freetype2) -lpng -lm
host: $(SRC) $(CORE)/*.h host.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LIBS)

# the sound simulator: the console's cryo_sound.c against a model of audsrv and the SPU2
sndsim/sim: sndsim/sim.c $(OPL)/src/cryo_sound.c
	$(CC) -O2 -Wall -Wno-unused-parameter -Dclock=sim_clock -Isndsim/shim -Isndsim/shim/include -I$(OPL) \
		-DCRYO_AUDIO='"$(abspath $(OPL))/cryo_res/audio"' -o $@ sndsim/sim.c $(OPL)/src/cryo_sound.c -lm
