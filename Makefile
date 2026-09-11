# Dream: Land of Giants (SNES prototype) matching disassembly
#
#   cp /path/to/DREAM.sfc baserom/      # your own ROM; never committed
#   make                                # extract -> assemble -> verify sha1 (+ SPC700 driver)
#   make regen                          # re-run the tracer and regenerate src/ from baserom/
#
ROM      := baserom/DREAM.sfc
ASAR     := tools/bin/asar
BUILD    := build/dream.sfc
SHA1     := 2675d7afe886f20462337aa1ee3aa5c3135fff3a
HALVES   := $(addprefix data/,$(shell python3 -c "print(' '.join('%02X.bin'%i for i in range(64)))"))

.PHONY: all check extract regen clean spc roundtrip harness recomp-check recomp-check-units recomp-check-nocpu recomp-profile app app-check sdl3 app-win sdl3-win win-dlls

all: check

# One extraction, not one per file. As a plain multi-target rule, `$(HALVES): ...` tells
# make the recipe builds *one* of those 64 files, so `make -j8` starts eight or nine
# extract.py processes at once and they truncate each other's output (tools/extract.py
# opens every file with 'wb'). A stamp target is the portable spelling of GNU make 4.3's
# grouped target `&:`. config/assets.txt is a prerequisite because extract.py writes the
# 1920 named assets from it as well as the 64 half-banks.
data/.extracted: $(ROM) tools/extract.py config/assets.txt
	python3 tools/extract.py $(ROM) data
	@touch $@

$(HALVES): data/.extracted ;

extract: data/.extracted

$(BUILD): src/*.asm $(HALVES) | build
	python3 -c "open('$(BUILD)','wb').write(b'\0'*0x200000)"
	$(ASAR) --no-title-check --fix-checksum=off src/main.asm $(BUILD)

build:
	mkdir -p build

check: $(BUILD) spc
	@echo "$(SHA1)  $(BUILD)" | sha1sum -c -

# SPC700 driver: assemble spc/driver.asm into a 64K SPC RAM image and compare the two
# uploaded blocks (loader at $04D8, driver at $0560) against the bytes in data/
spc: build/spc.bin
build/spc.bin: spc/driver.asm $(HALVES) | build
	python3 -c "open('build/spc.bin','wb').write(b'\0'*0x10000)"
	$(ASAR) --no-title-check --fix-checksum=off spc/driver.asm build/spc.bin
	python3 tools/check_spc.py build/spc.bin

# Verification harness: headless LakeSnes core + recomp hook/lockstep runner.
# Builds into build/recomp/dream_harness. No SDL, no X, no network.
harness:
	cmake -S recomp -B build/recomp -DCMAKE_BUILD_TYPE=Release
	cmake --build build/recomp -j --target dream_harness

# The game: an SDL3 window, sound and gamepad around the same core and the same
# recomp routines the harness verifies. Built into build/recomp/dream.
#
# A system SDL3 is used when there is one. Otherwise SDL3 is built from source
# into build/sdl3 first (static, no system headers required): SDL's own configure
# turns off whatever it cannot find, and the dummy video/audio drivers are enough
# for the headless check in recomp/app/README.md.
SDL3_PREFIX := $(CURDIR)/build/sdl3

app:
	@if cmake -S recomp -B build/recomp -DCMAKE_BUILD_TYPE=Release | grep -q '^-- SDL3 found'; then \
	    echo "make: using an SDL3 cmake already knows about"; \
	else \
	    $(MAKE) sdl3 && \
	    cmake -S recomp -B build/recomp -DCMAKE_BUILD_TYPE=Release \
	        -DCMAKE_PREFIX_PATH=$(SDL3_PREFIX); \
	fi
	cmake --build build/recomp -j --target dream

sdl3: build/sdl3/lib/cmake/SDL3/SDL3Config.cmake

build/sdl3/lib/cmake/SDL3/SDL3Config.cmake:
	test -d build/sdl3-src || \
	    git clone https://github.com/libsdl-org/SDL -b release-3.2.x --depth 1 build/sdl3-src
	cmake -S build/sdl3-src -B build/sdl3-build -DCMAKE_BUILD_TYPE=Release \
	    -DSDL_STATIC=ON -DSDL_SHARED=OFF \
	    -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL_TESTS=OFF \
	    -DSDL_UNIX_CONSOLE_BUILD=ON \
	    -DCMAKE_INSTALL_PREFIX=$(SDL3_PREFIX)
	cmake --build build/sdl3-build -j
	cmake --install build/sdl3-build

# --- Windows, cross-built here with mingw-w64 --------------------------------
# The same two executables for Windows x86_64, from this Linux box: there is no
# Wine here, so they can be built and linked but not run; .github/workflows/ci.yml
# runs them on a real Windows runner (dream_harness --test-coro exercises the
# fiber backend of recomp/harness/coro.h there).
#
#   make sdl3-win   SDL3 release-3.2.x for the target, static, into build/sdl3-win
#   make app-win    dream.exe and dream_harness.exe into build/win/
#
# The DLL import check at the end of app-win is the gate on the release zip: the
# zip carries the executables and nothing else, so every DLL they import has to
# be one Windows itself provides. -static in the toolchain file is what keeps
# libgcc and libwinpthread out; a static SDL3 is what keeps SDL3.dll out.
MINGW_TOOLCHAIN := $(CURDIR)/recomp/cmake/mingw-w64.cmake
SDL3_WIN_PREFIX := $(CURDIR)/build/sdl3-win
MINGW_OBJDUMP   := x86_64-w64-mingw32-objdump

app-win: sdl3-win
	cmake -S recomp -B build/win -DCMAKE_BUILD_TYPE=Release \
	    -DCMAKE_TOOLCHAIN_FILE=$(MINGW_TOOLCHAIN) \
	    -DCMAKE_PREFIX_PATH=$(SDL3_WIN_PREFIX)
	cmake --build build/win -j
	$(MAKE) win-dlls

# Every DLL the two executables import, and a refusal if one of them is not a
# Windows system DLL (the release zip ships no DLLs at all). The allowlist lives in
# tools/check_dlls.sh, which the ci.yml and release.yml windows jobs call as well, so
# the three places cannot drift apart.
win-dlls:
	@OBJDUMP=$(MINGW_OBJDUMP) tools/check_dlls.sh build/win/dream.exe build/win/dream_harness.exe

sdl3-win: build/sdl3-win/lib/cmake/SDL3/SDL3Config.cmake

build/sdl3-win/lib/cmake/SDL3/SDL3Config.cmake: $(MINGW_TOOLCHAIN)
	test -d build/sdl3-src || \
	    git clone https://github.com/libsdl-org/SDL -b release-3.2.x --depth 1 build/sdl3-src
	cmake -S build/sdl3-src -B build/sdl3-win-build -DCMAKE_BUILD_TYPE=Release \
	    -DCMAKE_TOOLCHAIN_FILE=$(MINGW_TOOLCHAIN) \
	    -DSDL_STATIC=ON -DSDL_SHARED=OFF \
	    -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL_TESTS=OFF \
	    -DCMAKE_INSTALL_PREFIX=$(SDL3_WIN_PREFIX)
	cmake --build build/sdl3-win-build -j
	cmake --install build/sdl3-win-build

# Recomp gate: build the harness, then run every input script under
# recomp/harness/inputs/ in lockstep with the C routines installed. Fails if any
# script mismatches; reports the per-routine hook call counts either way.
#
# --check also runs the routine-level gate and then refuses if the set of routines
# the two gates credit is not the set config/recomp.txt names. That file is written
# only by --update, so without this a routine that was deleted, renamed or made
# unreachable keeps its credit and its traced bytes in the `recomp` figure.
recomp-check: harness
	python3 tools/recomp_verify.py --check

# The routine-level gate: the routines no input script can reach, each run from
# the seeded states in config/recomp_units.txt with the ROM's own code and the C
# body side by side. See recomp/README.md, "The routine-level gate".
recomp-check-units: harness
	python3 tools/recomp_verify.py --units

# The same gate with the candidate machine executing no instructions at all: the
# C bodies are the program and the registry resolves every pc hand-off. A pc with
# no body stops the run, so a pass is also the port's dead-code check.
# See recomp/README.md, "Running without the CPUs".
recomp-check-nocpu: harness
	python3 tools/recomp_verify.py --no-cpu

# The gallery's own gates, and the proof that the app is the harness plus SDL.
# docs/RECOMP.md calls `dream --sprite-verify` the gate on the sprite pages and the
# `dream --frames` / `dream_harness --hooks on` parity the proof of the platform layer;
# neither had a target, so both were a manual check and nothing noticed when
# config/assets.txt or the hand-transcribed tables in recomp/app/gen_gallery_table.py
# drifted from the C they were read off.
#
# Three checks: every forced and live sprite render against the game that drew it, a run
# with the gallery walked through every page against the same run without it, and the
# app's frame line against the harness's. The dummy SDL drivers are what let all three
# run with no display and no sound card. Minutes, so it is its own target rather than
# part of recomp-check.
APP_CHECK_INPUT  := recomp/harness/inputs/level_walk_jump.txt
APP_CHECK_FRAMES := 400
app-check: app harness
	@mkdir -p build
	SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy ./build/recomp/dream --sprite-verify
	@echo "app-check: a walk through every gallery page changes no emulation state ..."
	@SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy ./build/recomp/dream \
	    --frames $(APP_CHECK_FRAMES) --input $(APP_CHECK_INPUT) \
	    | grep '^frame' | tail -1 > build/app-check-plain.txt
	@SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy ./build/recomp/dream \
	    --frames $(APP_CHECK_FRAMES) --input $(APP_CHECK_INPUT) --gallery-toggle 150,60 \
	    | grep '^frame' | tail -1 > build/app-check-toggled.txt
	@diff -u build/app-check-plain.txt build/app-check-toggled.txt
	@echo "app-check: dream --frames is dream_harness --hooks on ..."
	@./build/recomp/dream_harness --hooks on --spc-hooks on \
	    --frames $(APP_CHECK_FRAMES) --input $(APP_CHECK_INPUT) \
	    | grep '^frame' | tail -1 > build/app-check-harness.txt
	@diff -u build/app-check-plain.txt build/app-check-harness.txt
	@echo "app-check: OK"

# Re-measure config/recomp_cycles.txt (the per-routine cycle charge). Only needed
# when a routine is added that does not model its own timing; see recomp/README.md.
recomp-profile: harness
	./build/recomp/dream_harness --profile config/recomp_cycles.txt --cycles none \
	    --frames 900 --quiet --input recomp/harness/inputs/title_start_right.txt

# decode every asset with a codec into build/assets/ and re-encode; must be byte-exact
roundtrip: $(HALVES)
	python3 tools/roundtrip_check.py

regen: $(HALVES)
	python3 tools/emit_asar.py $(ROM) src

clean:
	rm -rf build data out
