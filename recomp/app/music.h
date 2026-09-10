/* music: a scratch SNES for the gallery's Music page.
 *
 * A song or a sound effect is a command to the sound driver running on the APU, and the
 * driver only exists once the 65816 has uploaded it. So the Music page gets a machine of
 * its own (a second core with the same ROM, booted far enough that spc_init has uploaded
 * loader and driver) and asks it for a song or an effect through the same routines the
 * game calls (spc_command, sfx_command_dispatch), over the same emulated APU ports. Its
 * DSP output is what the page plays.
 *
 * The game's own machine is never touched: it stays paused, byte for byte, while this
 * one plays. Closing the page destroys this machine.
 */
#ifndef MUSIC_H
#define MUSIC_H

#include <stddef.h>
#include <stdint.h>

typedef struct MusicPlayer MusicPlayer;

/* Boots a second core on `rom` (which must outlive the player). NULL if it cannot. */
MusicPlayer* music_create(const uint8_t* rom, size_t romLen);
void music_destroy(MusicPlayer* mp);

/* spc_command with A = song number: the ROM's own upload-and-play path. */
void music_play_song(MusicPlayer* mp, int song);
/* sfx_command_dispatch with A = the 16-bit command (high byte channel, low byte id). */
void music_play_sfx(MusicPlayer* mp, uint16_t command);

/* Run one frame and take its stereo DSP output (2 * samples int16s). */
void music_frame(MusicPlayer* mp, int16_t* stereo, int samples);

#endif
