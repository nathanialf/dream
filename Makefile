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

.PHONY: all check extract regen clean spc roundtrip harness recomp-check recomp-profile

all: check

$(HALVES): $(ROM) tools/extract.py
	python3 tools/extract.py $(ROM) data

extract: $(HALVES)

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
	cmake --build build/recomp -j

# Recomp gate: build the harness, then run every input script under
# recomp/harness/inputs/ in lockstep with the C routines installed. Fails if any
# script mismatches; reports the per-routine hook call counts either way.
recomp-check: harness
	python3 tools/recomp_verify.py

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
