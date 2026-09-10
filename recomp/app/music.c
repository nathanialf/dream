/* music — see music.h. */
#include "music.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "snes.h"
#include "cart.h"
#include "cpu.h"

#include "ss_internal.h"

/* Frames to run from reset before the machine is ready to be asked for a sound.
 * spc_init uploads the loader and the driver during reset; a couple of seconds is
 * comfortably past that and past the ROM's own first song command. */
#ifndef MUSIC_BOOT_FRAMES
#define MUSIC_BOOT_FRAMES 180
#endif

/* Entry addresses in the disassembly's $C0:0000+offset form (out/symbols.txt). */
#define SPC_COMMAND_BANK 0xC1
#define SPC_COMMAND_ADDR 0x83CE
#define SFX_DISPATCH_ADDR 0x8415

/* The CPU-side command counter (spc_port0_counter, $00:0046) that write_spc_command
 * spins on, and the byte the SPC echoes back into $2140. Equal means the driver is
 * sitting in its command loop and a command can be handed over without the routine
 * spinning; unequal means something is mid-handshake and the request is dropped rather
 * than risking a wait loop with no way out. */
#define SPC_PORT0_COUNTER 0x0046

/* The song table (docs/data_formats.md): 16 x {song block ptr24, sample list ptr24}. */
#define SONG_TABLE_OFF 0x0210B9u

struct MusicPlayer {
  Snes* snes;
  SnesState ss;
  const uint8_t* rom;
  size_t romLen;
  bool started;   /* until the user picks something the page stays quiet */
};

/* Call one of the ROM's sound routines on the scratch machine, at a frame boundary,
 * and put the CPU back where it was. ss_call_long pushes a return frame and runs the
 * reference CPU over the routine, so the APU sees exactly the port handshake the game
 * performs — the uploads included. The registers are restored afterwards so the scratch
 * machine's own program carries on from where it was interrupted.
 *
 * NMI is masked for the duration. Not for speed: this ROM's NMI handler does not return
 * to what it interrupted, it resets the stack and jumps back into the main loop, which
 * swallows the call frame and leaves the routine half-done. Masking it means the call
 * runs to its own rtl. The scratch machine misses a frame of its own program, which
 * costs it nothing — it exists to hold the sound driver. */
static bool music_ready(const MusicPlayer* mp) {
  return mp->snes->apu->outPorts[0] == mp->snes->ram[SPC_PORT0_COUNTER];
}

static void music_call(MusicPlayer* mp, uint16_t addr, uint16_t a) {
  if(mp == NULL || mp->snes == NULL || !music_ready(mp)) return;
  Cpu* c = mp->snes->cpu;
  /* An interrupt latched at the frame boundary has to be taken first: this ROM's NMI
   * handler does not return to what it interrupted, it resets the stack and re-enters
   * the main loop, which would swallow the call frame whole. Letting the machine take
   * it puts the CPU inside the handler, where a jsl behaves like any other. */
  for(int guard = 0; guard < 64 && ss_int_pending(&mp->ss); guard++) cpu_runOpcode(c);
  Cpu saved = *c;
  bool nmiWas = mp->snes->nmiEnabled;
  mp->snes->nmiEnabled = false;
  SnesState* ss = &mp->ss;
  ss_enter_hook(ss);
  ss_set_p(ss, (uint8_t) (ss_p(ss) & ~0x30u));   /* rep #$30: the routines are m0x0 */
  ss_set_db(ss, 0x00);
  ss_set_dp(ss, 0x0000);
  ss_set_a(ss, a);
  ss_call_long(ss, SPC_COMMAND_BANK, addr);
  ss_leave_hook(ss);
  *c = saved;
  mp->snes->nmiEnabled = nmiWas;
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
  mp->rom = rom;
  mp->romLen = romLen;
  for(int i = 0; i < MUSIC_BOOT_FRAMES; i++) snes_runFrame(mp->snes);
  return mp;
}

void music_destroy(MusicPlayer* mp) {
  if(mp == NULL) return;
  if(mp->snes != NULL) snes_free(mp->snes);
  free(mp);
}

/* Word count of a song's upload block, straight out of the ROM: {dest, words, data}. */
static unsigned song_words(const uint8_t* rom, size_t romLen, int song) {
  size_t p = SONG_TABLE_OFF + (size_t) (6 * song);
  if(p + 3 > romLen) return 0;
  uint32_t ptr = (uint32_t) (rom[p] | (rom[p + 1] << 8) | (rom[p + 2] << 16));
  size_t off = (size_t) ((((ptr >> 16) & 0x3Fu) << 16) | (ptr & 0xFFFFu));
  if(off + 4 > romLen) return 0;
  return (unsigned) (rom[off + 2] | (rom[off + 3] << 8));
}

/* Songs 3-7 are empty 4-byte blocks and the ROM's upload path does not come back from
 * one: with no words to send, the driver never reaches the state the next handshake
 * waits for, and spc_command spins. The game never asks for them; neither does this. */
void music_play_song(MusicPlayer* mp, int song) {
  if(mp == NULL || song < 0 || song > 7) return;
  if(song_words(mp->rom, mp->romLen, song) == 0) return;
  music_call(mp, SPC_COMMAND_ADDR, (uint16_t) (song & 0xFF));
  mp->started = true;
}

void music_play_sfx(MusicPlayer* mp, uint16_t command) {
  music_call(mp, SFX_DISPATCH_ADDR, command);
  if(mp != NULL) mp->started = true;
}

/* The machine keeps running whether or not anything has been picked — that is what
 * keeps it at a frame boundary with the driver in its command loop, ready to be asked.
 * Its output only reaches the speakers once the user has asked for something: the
 * scratch machine is the game booted to its title screen, so before that it is playing
 * the title music to nobody. */
void music_frame(MusicPlayer* mp, int16_t* stereo, int samples) {
  if(mp == NULL || mp->snes == NULL) {
    memset(stereo, 0, (size_t) samples * 2 * sizeof(int16_t));
    return;
  }
  snes_runFrame(mp->snes);
  snes_setSamples(mp->snes, stereo, samples);
  if(!mp->started) memset(stereo, 0, (size_t) samples * 2 * sizeof(int16_t));
}
