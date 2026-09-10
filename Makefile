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

.PHONY: all check extract regen clean spc

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

regen: $(HALVES)
	python3 tools/emit_asar.py $(ROM) src

clean:
	rm -rf build data out
