/* music — see music.h. */
#include "music.h"

#include <stdlib.h>
#include <string.h>

#include "snes.h"
#include "cart.h"
#include "cpu.h"

#include "ss_internal.h"

/* Frames to run from reset before the machine is ready to be asked for a sound.
 * spc_init uploads the loader and the driver during reset; a couple of seconds is
 * comfortably past that and past the ROM's own first song command. */
#define MUSIC_BOOT_FRAMES 180

/* Entry addresses in the disassembly's $C0:0000+offset form (out/symbols.txt). */
#define SPC_COMMAND_BANK 0xC1
#define SPC_COMMAND_ADDR 0x83CE
#define SFX_DISPATCH_ADDR 0x8415

/* An empty song slot: uploading it is how the page starts silent without inventing a
 * command of its own (songs 3-7 are the ROM's own 4-byte empty blocks). */
#define SILENT_SONG 7

struct MusicPlayer {
  Snes* snes;
  SnesState ss;
};

/* Call one of the ROM's sound routines on the scratch machine, at a frame boundary,
 * and put the CPU back where it was. ss_call_long pushes a return frame and runs the
 * reference CPU over the routine, so the APU sees exactly the port handshake the game
 * performs — the uploads included. The registers are restored afterwards so the scratch
 * machine's own program carries on from where it was interrupted. */
static void music_call(MusicPlayer* mp, uint16_t addr, uint16_t a) {
  if(mp == NULL || mp->snes == NULL) return;
  Cpu* c = mp->snes->cpu;
  Cpu saved = *c;
  SnesState* ss = &mp->ss;
  ss_enter_hook(ss);
  ss_set_p(ss, (uint8_t) (ss_p(ss) & ~0x30u));   /* rep #$30: the routines are m0x0 */
  ss_set_db(ss, 0x00);
  ss_set_dp(ss, 0x0000);
  ss_set_a(ss, a);
  ss_call_long(ss, SPC_COMMAND_BANK, addr);
  ss_leave_hook(ss);
  *c = saved;
}

MusicPlayer* music_create(const uint8_t* rom, size_t romLen) {
  MusicPlayer* mp = calloc(1, sizeof(MusicPlayer));
  if(mp == NULL) return NULL;
  mp->snes = snes_init();
  if(mp->snes == NULL) { free(mp); return NULL; }
  /* Same forced HiROM/2 MiB/no-SRAM cart as the game's machine: this ROM's internal
   * header is overwritten by tilemap data, so there is nothing to score. */
  cart_load(mp->snes->cart, 2, (uint8_t*) rom, (int) romLen, 0);
  snes_reset(mp->snes, true);
  mp->snes->palTiming = false;
  mp->ss.snes = mp->snes;
  for(int i = 0; i < MUSIC_BOOT_FRAMES; i++) snes_runFrame(mp->snes);
  music_call(mp, SPC_COMMAND_ADDR, SILENT_SONG);
  return mp;
}

void music_destroy(MusicPlayer* mp) {
  if(mp == NULL) return;
  if(mp->snes != NULL) snes_free(mp->snes);
  free(mp);
}

void music_play_song(MusicPlayer* mp, int song) {
  music_call(mp, SPC_COMMAND_ADDR, (uint16_t) (song & 0xFF));
}

void music_play_sfx(MusicPlayer* mp, uint16_t command) {
  music_call(mp, SFX_DISPATCH_ADDR, command);
}

void music_frame(MusicPlayer* mp, int16_t* stereo, int samples) {
  if(mp == NULL || mp->snes == NULL) {
    memset(stereo, 0, (size_t) samples * 2 * sizeof(int16_t));
    return;
  }
  snes_runFrame(mp->snes);
  snes_setSamples(mp->snes, stereo, samples);
}
