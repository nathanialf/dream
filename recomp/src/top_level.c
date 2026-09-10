/* The top of the program, bank $C0: reset, the NMI vector, and the two NMI
 * handlers the whole game runs inside.
 *
 * `docs/NOTES.md` ("Program structure", "Dynamic findings") describes the shape
 * this file transliterates. There is no main loop at the outer level: `reset`
 * initialises the machine, installs an NMI handler pointer in `$0000` and then
 * parks the CPU on a `wai` at `loc_C0A4FD`. Every frame of the game is one
 * pass through the handler the NMI vector reaches with `jmp ($0000)` --
 * `nmi_handler_gameplay` for the three level scenes, `nmi_handler_title_fade`
 * for the title -- and each of them ends by jumping back to `loc_C0A4F5`,
 * which re-arms NMITIMEN and parks again.
 *
 * Three things in here need more than the usual instruction-for-instruction
 * treatment.
 *
 * The NMI vector. `nmi` is not called, it is *entered*: the core has already
 * pushed PB, PC and P and cleared D and set I by the time the hook's entry
 * address is offered (LakeSnes cpu.c `cpu_doInterrupt`, which the core runs
 * before it consults the hook table at all). So the frame on the stack is the
 * real one and this body has nothing to build; it just runs the handler's
 * instructions. `unused_vec` is the other half of that story -- the single
 * `rti` every other vector points at -- and it does pop that frame, in
 * LakeSnes' own order (two idles, P, PC, latch, PB).
 *
 * `reset` and the emulation flag. `clc ; xce` is the one instruction pair the
 * hook API cannot perform: nothing in snes_state.h clears the 65816's
 * emulation bit, and leaving it set would force m and x back to 1 on every
 * later `rep`. So the body models the `clc`, hands the `xce` back to the ROM
 * by pointing the pc at it, and picks the routine up again at `$8002` from a
 * second registered entry. One ROM instruction runs; the rest of the 244 bytes
 * are C. The routine has three more entry addresses. Two are there because the
 * ROM jumps into its middle: `loc_C08042` (the mode restart, from
 * `nmi_handler_title_fade`) and `loc_C0805E` (the per-mode re-init, from
 * `nmi_handler_gameplay`). The third, `loc_C08012`, is the head of the WRAM
 * clear, and it is there so the body can pick the loop up after handing it
 * back: the clear is fifteen frames long, so the hook always yields inside it,
 * and without an entry at the loop head everything after the loop would be the
 * ROM's forever. With it the clear takes 26 hand-backs and the rest of the
 * routine runs as C.
 *
 * The park loop. `wai` puts the CPU into a state the hook API has no way to
 * enter, so `loc_C0A4F5` stops one instruction short of it and hands the two
 * instruction loop at `loc_C0A4FD` back to the ROM. That is the only code in
 * this file the 65816 executes on its own by design, and it executes it
 * because a hook gave it the pc.
 *
 * Calls to converted callees go through the emulator with a real pushed frame
 * (the JSR / JSL / JSR_IAX macros below), so the callee's own hook fires at its
 * own entry address and is credited with the call, and the callee can be left
 * running when the machine moves on underneath -- which matters here more than
 * anywhere else, because `nmi_handler_gameplay` calls twenty routines and the
 * frame boundary lands inside one of them nearly every frame.
 *
 * On the name `ppu_regs_default` ($C0:8BDB): it is neither a table of PPU
 * register defaults nor a DMA queue processor, and it is not a routine at all.
 * Nothing calls it: it is the second half of `mode0_camera_zone_update`, which
 * `nmi_handler_gameplay` reaches through `jtbl_C0827A` in game mode 0, and the
 * only way in is falling through the `bne` at `$8BD9` (or the branches and
 * jumps from `$8BD4`, `$8AF1`, `$8AF9`, `$8B21`, `$8B29`, `$8B7D` and `$8B95`,
 * all of them inside `mode0_camera_zone_update`). What it does is finish that routine's job:
 * advance the camera-zone state machine ($0C0C / $0C11 / $0C13), pick the
 * frame's BG offsets out of the $C4:65xx-$C4:6Bxx tables, build the 196-entry
 * per-scanline BG1 horizontal-scroll ramp at $0DB9 that `nmi_handler_gameplay`'s
 * `mvn` copies to $0C31 for HDMA, and fall through into
 * `check_pending_player_attack`. `docs/naming_proposals.md` section 9 already
 * proposes `mode0_weather_zone_update` for `mode0_camera_zone_update`; this block belongs to
 * it, so the honest fix is to drop the name and let the label be `loc_C08BDB`
 * (or, if it wants one, `mode0_weather_zone_hdma_build`). The current name is
 * kept here so the C and `tools/names.txt` still agree.
 */
#include <stdint.h>
#include <stdbool.h>

#include "snes_state.h"
#include "dream_ram.h"
#include "dream_alu.h"
#include "dream_time.h"

/* ---- registers this file uses that dream_ram.h does not name ------------ */
#define INIDISP_  0x2100
#define OBSEL     0x2101
#define OAMADDL   0x2102
#define OAMADDH   0x2103
#define BGMODE    0x2105
#define BG1SC     0x2107
#define BG3SC     0x2109
#define BG12NBA   0x210B
#define BG4HOFS   0x2113
#define M7SEL     0x211A
#define M7A       0x211B
#define M7B       0x211C
#define M7C       0x211D
#define M7D       0x211E
#define M7X       0x211F
#define M7Y       0x2120
#define W12SEL    0x2123
#define WOBJSEL   0x2125
#define WH1       0x2127
#define WH3       0x2129
#define WOBJLOG   0x212B
#define TM        0x212C
#define TS        0x212D
#define TSW       0x212F
#define CGWSEL    0x2130
#define CGADSUB   0x2131
#define COLDATA   0x2132
#define SETINI    0x2133
#define NMITIMEN  0x4200
#define WRIO      0x4201
#define WRMPYA    0x4202
#define WRDIVL    0x4204
#define WRDIVB    0x4206
#define HTIMEH    0x4208
#define VTIMEH    0x420A
#define HDMAEN    0x420C
#define MEMSEL    0x420D
#define RDNMI     0x4210
#define TIMEUP    0x4211
#define DMAP2     0x4320
#define A1TL2     0x4322
#define A1B2      0x4324
#define DASL2     0x4325
#define DASH2     0x4326

/* ---- RAM this file uses that dream_ram.h does not name ------------------ */
#define scratch_0C           0x000C
#define scratch_1A           0x001A
#define scratch_20           0x0020
#define fade_level           0x0030   /* $30/$32: current INIDISP fade, its delta */
#define fade_delta           0x0032
#define inidisp_shadow       0x0031
#define nmitimen_shadow      0x0034
#define pause_flag           0x004A   /* non-zero freezes the simulation half */
#define frame_counter        0x005E

/* ---- 8-bit arithmetic, m = 1 (LakeSnes cpu_adc / cpu_sbc / cpu_cmp) ------ */
static inline uint16_t alu_adc8(SnesState* ss, uint16_t a, uint8_t v) {
  unsigned r = (a & 0xff) + v + (ss_c(ss) ? 1u : 0u);
  ss_set_v(ss, ((a & 0x80) == (v & 0x80)) && ((v & 0x80) != (r & 0x80)));
  ss_set_c(ss, r > 0xff);
  ss_set_nz8(ss, (uint8_t) r);
  return (uint16_t) ((a & 0xff00) | (r & 0xff));
}

static inline uint16_t alu_sbc8(SnesState* ss, uint16_t a, uint8_t v) {
  return alu_adc8(ss, a, (uint8_t) ~v);
}

static inline void alu_cmp8(SnesState* ss, uint16_t a, uint8_t v) {
  unsigned r = (a & 0xff) + (uint8_t) ~v + 1u;
  ss_set_c(ss, r > 0xff);
  ss_set_nz8(ss, (uint8_t) r);
}

static inline uint16_t alu_and8(SnesState* ss, uint16_t a, uint8_t v) {
  uint8_t r = (uint8_t) (a & v); ss_set_nz8(ss, r);
  return (uint16_t) ((a & 0xff00) | r);
}

static inline uint16_t alu_ora8(SnesState* ss, uint16_t a, uint8_t v) {
  uint8_t r = (uint8_t) (a | v); ss_set_nz8(ss, r);
  return (uint16_t) ((a & 0xff00) | r);
}

static inline uint16_t alu_inc8(SnesState* ss, uint16_t a) {
  uint8_t r = (uint8_t) (a + 1); ss_set_nz8(ss, r);
  return (uint16_t) ((a & 0xff00) | r);
}

static inline uint16_t alu_dec8(SnesState* ss, uint16_t a) {
  uint8_t r = (uint8_t) (a - 1); ss_set_nz8(ss, r);
  return (uint16_t) ((a & 0xff00) | r);
}

/* ---- instruction shapes dream_time.h does not cover --------------------- */

/* An immediate operand is not fetched by the addressing mode: the opcode's own
 * data read *is* the operand read, so the interrupt latch sits between the two
 * operand bytes rather than after them (cpu_adrImm + cpu_lda in the core). */
#define SIMM16(addr) do { if(t_step(ss, pb, (uint16_t) (addr), 2, a, x, y)) return; \
                          ss_check_int(ss); ss_fetch(ss, 1); } while(0)
#define SIMM8(addr)  do { if(t_step(ss, pb, (uint16_t) (addr), 1, a, x, y)) return; \
                          ss_check_int(ss); ss_fetch(ss, 1); } while(0)

/* jmp abs: opcode, low byte, latch, high byte (LakeSnes case 0x4c). The body
 * names the destination itself, so this only spends the instruction. */
#define SJMP(addr)   do { if(t_step(ss, pb, (uint16_t) (addr), 2, a, x, y)) return; \
                          ss_check_int(ss); ss_fetch(ss, 1); } while(0)

/* A 16-bit read-modify-write (inc/dec abs, inc/dec dp): the read takes no
 * interrupt latch between its bytes, an internal cycle sits between read and
 * write, and the write-back goes high byte first. */
static uint16_t t_rmw_r16(SnesState* ss, uint32_t adr) {
  uint8_t lo = ss_bus_r8(ss, adr);
  uint8_t hi = ss_bus_r8(ss, (adr + 1) & 0xffffff);
  return (uint16_t) (lo | (hi << 8));
}

static void t_rmw_w16(SnesState* ss, uint32_t adr, uint16_t v) {
  ss_bus_w8(ss, (adr + 1) & 0xffffff, (uint8_t) (v >> 8));
  ss_check_int(ss);
  ss_bus_w8(ss, adr, (uint8_t) v);
}

static uint16_t t_inc16(SnesState* ss, uint32_t adr, int delta) {
  uint16_t v = (uint16_t) (t_rmw_r16(ss, adr) + delta);
  ss_idle(ss);
  t_rmw_w16(ss, adr, v);
  ss_set_nz16(ss, v);
  return v;
}

/* xba: the flags come from the byte that moves into the low half, and the two
 * internal cycles straddle the interrupt latch (LakeSnes case 0xeb). */
static uint16_t t_xba(SnesState* ss, uint16_t a) {
  uint16_t r = (uint16_t) ((a >> 8) | (a << 8));
  ss_set_nz8(ss, (uint8_t) r);
  ss_idle(ss);
  ss_check_int(ss);
  ss_idle(ss);
  return r;
}

/* Did the callee actually come back?
 *
 * ss_run_callee stops when the stack pointer is back above the frame, and that
 * test alone is not enough here. particle_update_and_draw_mode0 ($C0:9331) uses
 * S as a scratch register -- it parks a slot index and later an OAM coordinate
 * there (txs/tcs at $9408, $949A, $94B6) before restoring S from $18 -- and
 * every call tree below reaches it: nmi_handler_gameplay through
 * jtbl_C0828A[0], nmi_handler_title_fade through mode1_reset_particles_and_oam.
 * Whenever the ROM is running that stretch at an instruction boundary the
 * parked value can read as "above the frame", and ss_run_callee comes back
 * false with the callee still mid-flight.
 *
 * The pc settles it: the callee returned only if it is standing on the
 * instruction after the call. Otherwise it is still running, and the right
 * thing is the same as for a yield -- return, and let the ROM finish it. The
 * frame that was pushed is the routine's real return address, so the callee's
 * own rts lands where the ROM expects and it carries on with the caller too.
 */
#define CALLEE_RETURNED(sp0_, ret_) \
  (!ss_run_callee(ss, (sp0_)) && ss_pc(ss) == (ret_) && ss_pb(ss) == pb)

/* jsr abs: the operand word is in the step; what is left is the internal cycle
 * and the return address, high byte first with the latch in between. The callee
 * runs on the reference CPU, so a converted callee's own hook fires and is
 * credited with the call; the frame pushed here is the routine's real one, so
 * the callee can be left running when the machine moves on underneath. */
#define JSR(addr, target) do {                                                \
    const uint16_t sp0_ = ss_sp(ss);                                          \
    S((addr), 3);                                                             \
    ss_idle(ss);                                                              \
    { const uint16_t ret_ = (uint16_t) (ss_pc(ss) - 1);                       \
      ss_push8(ss, (uint8_t) (ret_ >> 8));                                    \
      ss_check_int(ss);                                                       \
      ss_push8(ss, (uint8_t) ret_); }                                         \
    ss_set_pc(ss, pb, (uint16_t) (target));                                   \
    if(!CALLEE_RETURNED(sp0_, (uint16_t) ((addr) + 3))) return;               \
    a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);                                 \
  } while(0)

/* jsl long: the program bank goes on the stack between the address word and
 * the bank byte, and the return address after it (LakeSnes case 0x22). */
#define JSL(addr, bank, target) do {                                          \
    const uint16_t sp0_ = ss_sp(ss);                                          \
    S((addr), 3);                                                             \
    ss_push8(ss, pb);                                                         \
    ss_idle(ss);                                                              \
    ss_fetch(ss, 1);                                                          \
    { const uint16_t ret_ = (uint16_t) (ss_pc(ss) - 1);                       \
      ss_push8(ss, (uint8_t) (ret_ >> 8));                                    \
      ss_check_int(ss);                                                       \
      ss_push8(ss, (uint8_t) ret_); }                                         \
    ss_set_pc(ss, (uint8_t) (bank), (uint16_t) (target));                     \
    if(!CALLEE_RETURNED(sp0_, (uint16_t) ((addr) + 4))) return;               \
    a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);                                 \
  } while(0)

/* jsr (abs,X): the odd one. The return address is pushed after only the low
 * operand byte has been fetched, so it is the pc as it stands two bytes in --
 * still one short of the next instruction, which is what rts's +1 supplies --
 * and the high operand byte is fetched afterwards. Then an internal cycle and
 * the vector read out of the program bank (LakeSnes case 0xfc). */
#define JSR_IAX(addr, tbl) do {                                               \
    const uint16_t sp0_ = ss_sp(ss);                                          \
    S((addr), 2);                                                             \
    { const uint16_t ret_ = ss_pc(ss);                                        \
      ss_push8(ss, (uint8_t) (ret_ >> 8));                                    \
      ss_push8(ss, (uint8_t) ret_); }                                         \
    ss_fetch(ss, 1);                                                          \
    ss_idle(ss);                                                              \
    { const uint16_t adr_ = (uint16_t) ((tbl) + x);                           \
      const uint32_t bank_ = (uint32_t) pb << 16;                             \
      const uint8_t lo_ = ss_bus_r8(ss, bank_ | adr_);                        \
      ss_check_int(ss);                                                       \
      const uint8_t hi_ = ss_bus_r8(ss, bank_ | (uint16_t) (adr_ + 1));       \
      ss_set_pc(ss, pb, (uint16_t) (lo_ | (hi_ << 8))); }                     \
    if(!CALLEE_RETURNED(sp0_, (uint16_t) ((addr) + 3))) return;               \
    a = ss_a(ss); x = ss_x(ss); y = ss_y(ss);                                 \
  } while(0)

/* Publish the registers a body is holding in locals and hand the pc on. */
#define TAIL(bank, target) do {                                               \
    ss_set_a(ss, a); ss_set_x(ss, x); ss_set_y(ss, y);                        \
    ss_set_pc(ss, (uint8_t) (bank), (uint16_t) (target));                     \
    return;                                                                   \
  } while(0)

/* ---------------------------------------------------------------------------
 * ppu_init — $C0:A385
 *
 * Writes a known value into every PPU and CPU register the game cares about
 * and then clears the two interrupt latches on the way out. Called twice: once
 * from `reset` before the first frame ever runs, and once per mode change from
 * `loc_C08065`. The data bank is $00 on the first path and $80 on the second,
 * and both map $21xx/$42xx to the registers, so the absolute forms stay
 * data-bank relative exactly as the ROM writes them.
 * Exit: A = $0001 (low byte; the high byte is whatever the caller had), X and Y
 * narrowed to 8 bits by the sep and left there, m = x = 0, Z clear, N set from
 * the last TIMEUP read's open-bus high bit... whatever $4211 returned.
 * ------------------------------------------------------------------------- */
static void ppu_init(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  REP(0xA385, 0x30);                                   /* C0A385 rep #$30 */
  SIMM16(0xA387); a = 0x0000; ss_set_nz16(ss, a);      /* C0A387 lda #$0000 */
  SIMM16(0xA38A); x = 0x0000; ss_set_nz16(ss, x);      /* C0A38A ldx #$0000 */
  SIMM16(0xA38D); y = 0x0000; ss_set_nz16(ss, y);      /* C0A38D ldy #$0000 */

  /* Every one of these is a 16-bit stz, so each write-twice register gets both
   * halves from a single instruction and the pairs below are the ROM's own
   * repeats for the ones that need four writes. */
  S(0xA390, 3); t_write16(ss, ss_abs(ss, OBSEL), 0);    /* C0A390 stz OBSEL */
  S(0xA393, 3); t_write16(ss, ss_abs(ss, BGMODE), 0);   /* C0A393 stz BGMODE */
  S(0xA396, 3); t_write16(ss, ss_abs(ss, BG1SC), 0);    /* C0A396 stz BG1SC */
  S(0xA399, 3); t_write16(ss, ss_abs(ss, BG3SC), 0);    /* C0A399 stz BG3SC */
  S(0xA39C, 3); t_write16(ss, ss_abs(ss, BG12NBA), 0);  /* C0A39C stz BG12NBA */
  S(0xA39F, 3); t_write16(ss, ss_abs(ss, BG1HOFS), 0);  /* C0A39F stz BG1HOFS */
  S(0xA3A2, 3); t_write16(ss, ss_abs(ss, BG1HOFS), 0);  /* C0A3A2 stz BG1HOFS */
  S(0xA3A5, 3); t_write16(ss, ss_abs(ss, BG2HOFS), 0);  /* C0A3A5 stz BG2HOFS */
  S(0xA3A8, 3); t_write16(ss, ss_abs(ss, BG2HOFS), 0);  /* C0A3A8 stz BG2HOFS */
  S(0xA3AB, 3); t_write16(ss, ss_abs(ss, BG3HOFS), 0);  /* C0A3AB stz BG3HOFS */
  S(0xA3AE, 3); t_write16(ss, ss_abs(ss, BG3HOFS), 0);  /* C0A3AE stz BG3HOFS */
  S(0xA3B1, 3); t_write16(ss, ss_abs(ss, BG4HOFS), 0);  /* C0A3B1 stz BG4HOFS */
  S(0xA3B4, 3); t_write16(ss, ss_abs(ss, BG4HOFS), 0);  /* C0A3B4 stz BG4HOFS */
  S(0xA3B7, 3); t_write16(ss, ss_abs(ss, VMADDL), 0);   /* C0A3B7 stz VMADDL */
  S(0xA3BA, 3); t_write16(ss, ss_abs(ss, W12SEL), 0);   /* C0A3BA stz W12SEL */
  S(0xA3BD, 3); t_write16(ss, ss_abs(ss, WOBJSEL), 0);  /* C0A3BD stz WOBJSEL */
  S(0xA3C0, 3); t_write16(ss, ss_abs(ss, WH1), 0);      /* C0A3C0 stz WH1 */
  S(0xA3C3, 3); t_write16(ss, ss_abs(ss, WH3), 0);      /* C0A3C3 stz WH3 */
  S(0xA3C6, 3); t_write16(ss, ss_abs(ss, WOBJLOG), 0);  /* C0A3C6 stz WOBJLOG */
  S(0xA3C9, 3); t_write16(ss, ss_abs(ss, TS), 0);       /* C0A3C9 stz TS */
  S(0xA3CC, 3); t_write16(ss, ss_abs(ss, WRMPYA), 0);   /* C0A3CC stz WRMPYA */
  S(0xA3CF, 3); t_write16(ss, ss_abs(ss, WRDIVL), 0);   /* C0A3CF stz WRDIVL */
  S(0xA3D2, 3); t_write16(ss, ss_abs(ss, WRDIVB), 0);   /* C0A3D2 stz WRDIVB */
  S(0xA3D5, 3); t_write16(ss, ss_abs(ss, HTIMEH), 0);   /* C0A3D5 stz HTIMEH */
  S(0xA3D8, 3); t_write16(ss, ss_abs(ss, VTIMEH), 0);   /* C0A3D8 stz VTIMEH */

  SEP(0xA3DB, 0x30);                                   /* C0A3DB sep #$30 */
  x = (uint16_t) (x & 0xff);
  y = (uint16_t) (y & 0xff);

  SIMM8(0xA3DD); a = (uint16_t) ((a & 0xff00) | 0x8F); ss_set_nz8(ss, 0x8F);
  S(0xA3DF, 3); t_write8(ss, ss_abs(ss, INIDISP_), 0x8F);  /* C0A3DF sta INIDISP */
  S(0xA3E2, 3); t_write8(ss, ss_abs(ss, OAMADDH), 0);      /* C0A3E2 stz OAMADDH */
  SIMM8(0xA3E5); a = (uint16_t) ((a & 0xff00) | 0x80); ss_set_nz8(ss, 0x80);
  S(0xA3E7, 3); t_write8(ss, ss_abs(ss, VMAIN), 0x80);     /* C0A3E7 sta VMAIN */
  S(0xA3EA, 3); t_write8(ss, ss_abs(ss, M7SEL), 0);        /* C0A3EA stz M7SEL */
  S(0xA3ED, 3); t_write8(ss, ss_abs(ss, M7A), 0);          /* C0A3ED stz M7A */
  SIMM8(0xA3F0); a = (uint16_t) ((a & 0xff00) | 0x01); ss_set_nz8(ss, 0x01);
  S(0xA3F2, 3); t_write8(ss, ss_abs(ss, M7A), 0x01);       /* C0A3F2 sta M7A */
  S(0xA3F5, 3); t_write8(ss, ss_abs(ss, M7B), 0);          /* C0A3F5 stz M7B */
  S(0xA3F8, 3); t_write8(ss, ss_abs(ss, M7B), 0);          /* C0A3F8 stz M7B */
  S(0xA3FB, 3); t_write8(ss, ss_abs(ss, M7C), 0);          /* C0A3FB stz M7C */
  S(0xA3FE, 3); t_write8(ss, ss_abs(ss, M7C), 0);          /* C0A3FE stz M7C */
  S(0xA401, 3); t_write8(ss, ss_abs(ss, M7D), 0);          /* C0A401 stz M7D */
  S(0xA404, 3); t_write8(ss, ss_abs(ss, M7D), 0x01);       /* C0A404 sta M7D */
  S(0xA407, 3); t_write8(ss, ss_abs(ss, M7X), 0);          /* C0A407 stz M7X */
  S(0xA40A, 3); t_write8(ss, ss_abs(ss, M7X), 0);          /* C0A40A stz M7X */
  S(0xA40D, 3); t_write8(ss, ss_abs(ss, M7Y), 0);          /* C0A40D stz M7Y */
  S(0xA410, 3); t_write8(ss, ss_abs(ss, M7Y), 0);          /* C0A410 stz M7Y */
  S(0xA413, 3); t_write8(ss, ss_abs(ss, CGADD), 0);        /* C0A413 stz CGADD */
  S(0xA416, 3); t_write8(ss, ss_abs(ss, TSW), 0);          /* C0A416 stz TSW */
  SIMM8(0xA419); a = (uint16_t) ((a & 0xff00) | 0x30); ss_set_nz8(ss, 0x30);
  S(0xA41B, 3); t_write8(ss, ss_abs(ss, CGWSEL), 0x30);    /* C0A41B sta CGWSEL */
  S(0xA41E, 3); t_write8(ss, ss_abs(ss, CGADSUB), 0);      /* C0A41E stz CGADSUB */
  SIMM8(0xA421); a = (uint16_t) ((a & 0xff00) | 0xE0); ss_set_nz8(ss, 0xE0);
  S(0xA423, 3); t_write8(ss, ss_abs(ss, COLDATA), 0xE0);   /* C0A423 sta COLDATA */
  S(0xA426, 3); t_write8(ss, ss_abs(ss, SETINI), 0);       /* C0A426 stz SETINI */
  S(0xA429, 3); t_write8(ss, ss_abs(ss, NMITIMEN), 0);     /* C0A429 stz NMITIMEN */
  SIMM8(0xA42C); a = (uint16_t) ((a & 0xff00) | 0xFF); ss_set_nz8(ss, 0xFF);
  S(0xA42E, 3); t_write8(ss, ss_abs(ss, WRIO), 0xFF);      /* C0A42E sta WRIO */
  S(0xA431, 3); t_write8(ss, ss_abs(ss, HDMAEN), 0);       /* C0A431 stz HDMAEN */
  SIMM8(0xA434); a = (uint16_t) ((a & 0xff00) | 0x01); ss_set_nz8(ss, 0x01);
  S(0xA436, 3); t_write8(ss, ss_abs(ss, MEMSEL), 0x01);    /* C0A436 sta MEMSEL */

  /* the two acknowledging reads: RDNMI clears the vblank latch, TIMEUP the
   * IRQ one, so a pending interrupt cannot survive the init */
  S(0xA439, 3);                                            /* C0A439 lda RDNMI */
  { uint8_t v = t_read8(ss, ss_abs(ss, RDNMI));
    a = (uint16_t) ((a & 0xff00) | v); ss_set_nz8(ss, v); }
  S(0xA43C, 3);                                            /* C0A43C lda TIMEUP */
  { uint8_t v = t_read8(ss, ss_abs(ss, TIMEUP));
    a = (uint16_t) ((a & 0xff00) | v); ss_set_nz8(ss, v); }

  REP(0xA43F, 0x30);                                       /* C0A43F rep #$30 */

  ss_set_a(ss, a); ss_set_x(ss, x); ss_set_y(ss, y);
  S(0xA441, 1);                                            /* C0A441 rts */
  ss_rts(ss);
}

/* ---------------------------------------------------------------------------
 * unused_vec — $C0:A442
 *
 * The single `rti` the COP, BRK, ABORT and IRQ vectors all point at, in both
 * modes (docs/NOTES.md, "Mapping"). Nothing in the game enables an IRQ --
 * NMITIMEN is only ever $00, $01 or $81 -- and it executes no `cop` or `brk`,
 * so this is expected never to be entered; it is converted because it is a
 * routine the vector table names.
 *
 * The frame it pops is the one cpu_doInterrupt pushed: P, then PC, then PB,
 * with the latch between PC and PB (LakeSnes case 0x40, which pulls the bank
 * byte in emulation mode too).
 * ------------------------------------------------------------------------- */
static void unused_vec(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  S(0xA442, 1);                             /* C0A442 rti */
  ss_idle(ss);
  ss_idle(ss);
  ss_set_p(ss, ss_pull8(ss));
  { const uint8_t lo = ss_pull8(ss);
    const uint8_t hi = ss_pull8(ss);
    ss_check_int(ss);
    const uint8_t bank = ss_pull8(ss);
    ss_set_pc(ss, bank, (uint16_t) (lo | (hi << 8))); }
}

/* ---------------------------------------------------------------------------
 * nmi — $C0:A4D1, and the loc_C0A4E9 / loc_C0A4F5 tails
 *
 * The NMI vector. The hook is entered *after* the core has taken the interrupt
 * -- PB, PC and P are already on the stack, I is set, D is clear and the pc is
 * the vector's $00:A4D1 -- so there is no frame to build here and no `rti` to
 * match: the handler this hands control to resets the stack pointer and leaves
 * through `loc_C0A4F5` instead.
 *
 * The body saves A/X/Y 16-bit, acknowledges the NMI by reading RDNMI, forces
 * blanking at full brightness ($8F) and then jumps through the handler pointer
 * the install path left in $0000. That last `jmp ($0000)` is modelled by
 * setting the pc, so the registry dispatches whichever handler is installed --
 * `nmi_handler_gameplay` or `nmi_handler_title_fade`, both converted here.
 * ------------------------------------------------------------------------- */
static void nmi(SnesState* ss) {
  uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  /* C0A4D1 jml loc_C0A4D5 -- the vector's one job is to leave bank $00 for the
   * $80 mirror the rest of the game runs in. The latch sits between the address
   * word and the bank byte (LakeSnes case 0x5c). */
  S(0xA4D1, 3);
  ss_check_int(ss);
  ss_fetch(ss, 1);
  pb = 0x80;

  REP(0xA4D5, 0x30);                        /* C0A4D5 rep #$30 */

  S(0xA4D7, 1);                             /* C0A4D7 pha */
  ss_idle(ss);
  ss_push8(ss, (uint8_t) (a >> 8)); ss_check_int(ss); ss_push8(ss, (uint8_t) a);
  S(0xA4D8, 1);                             /* C0A4D8 phx */
  ss_idle(ss);
  ss_push8(ss, (uint8_t) (x >> 8)); ss_check_int(ss); ss_push8(ss, (uint8_t) x);
  S(0xA4D9, 1);                             /* C0A4D9 phy */
  ss_idle(ss);
  ss_push8(ss, (uint8_t) (y >> 8)); ss_check_int(ss); ss_push8(ss, (uint8_t) y);

  SEP(0xA4DA, 0x20);                        /* C0A4DA sep #$20 */
  S(0xA4DC, 3);                             /* C0A4DC lda RDNMI */
  { uint8_t v = t_read8(ss, ss_abs(ss, RDNMI));
    a = (uint16_t) ((a & 0xff00) | v); ss_set_nz8(ss, v); }
  SIMM8(0xA4DF); a = (uint16_t) ((a & 0xff00) | 0x8F); ss_set_nz8(ss, 0x8F);
  S(0xA4E1, 3); t_write8(ss, ss_abs(ss, INIDISP_), 0x8F);  /* C0A4E1 sta INIDISP */
  REP(0xA4E4, 0x20);                        /* C0A4E4 rep #$20 */

  /* C0A4E6 jmp ($0000) -- the pointer is read out of bank 0, never through the
   * data bank (LakeSnes case 0x6c), and the pc it yields is the handler the
   * registry dispatches next. */
  S(0xA4E6, 3);
  (void) dp;
  { const uint16_t target = t_read16(ss, 0x000000);
    TAIL(pb, target); }
}

/* loc_C0A4E9 — the handler install path.
 *
 * Reached with `lda #handler ; jmp $A4E9` from the end of an init routine
 * (`reset` at $80F1 with #$80F4, the title init at $BD1D with #$BD20), so A is
 * always 16-bit here. It stores the pointer, acknowledges any NMI already
 * latched, waits for the vblank flag to fall so the handler is not entered
 * half a frame late, and falls into loc_C0A4F5. */
static void nmi_install_handler(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  S(0xA4E9, 2);                             /* C0A4E9 sta nmi_handler_ptr */
  t_write16(ss, (uint16_t) (dp + nmi_handler_ptr), a);
  SEP(0xA4EB, 0x20);                        /* C0A4EB sep #$20 */
  S(0xA4ED, 3);                             /* C0A4ED lda RDNMI */
  { uint8_t v = t_read8(ss, ss_abs(ss, RDNMI));
    a = (uint16_t) ((a & 0xff00) | v); ss_set_nz8(ss, v); }

  for(;;) {                                 /* loc_C0A4F0 */
    S(0xA4F0, 3);                           /* C0A4F0 lda RDNMI */
    { uint8_t v = t_read8(ss, ss_abs(ss, RDNMI));
      a = (uint16_t) ((a & 0xff00) | v); ss_set_nz8(ss, v); }
    { const bool taken = (a & 0x80) != 0;
      S(0xA4F3, 1); t_branch(ss, taken);    /* C0A4F3 bmi loc_C0A4F0 */
      if(!taken) break; }
  }
  TAIL(pb, 0xA4F5);
}

/* loc_C0A4F5 — re-arm and park.
 *
 * Two flag states reach this. Falling out of the install path above it is
 * 8-bit (the sep at $A4EB); the tail `jmp` at the end of each NMI handler
 * ($8267 and $C00A) arrives 16-bit, which makes the store to NMITIMEN write
 * WRIO as well and the stz to JOYSER0 cover $4017. Both are modelled.
 *
 * The `wai` at loc_C0A4FD is the one instruction the hook API cannot perform,
 * so the body stops here and hands the two-instruction park loop back to the
 * ROM by pointing the pc at it. The next NMI lifts the CPU out of it and the
 * `nmi` hook above runs again. */
static void nmi_park(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);
  const bool m8 = ss_flag_m(ss);

  if(m8) {
    S(0xA4F5, 2);                           /* C0A4F5 lda nmitimen_shadow */
    { uint8_t v = t_read8(ss, (uint16_t) (dp + nmitimen_shadow));
      a = (uint16_t) ((a & 0xff00) | v); ss_set_nz8(ss, v); }
    S(0xA4F7, 3);                           /* C0A4F7 sta NMITIMEN */
    t_write8(ss, ss_abs(ss, NMITIMEN), (uint8_t) a);
    S(0xA4FA, 3);                           /* C0A4FA stz JOYSER0 */
    t_write8(ss, ss_abs(ss, JOYSER0), 0);
  } else {
    S(0xA4F5, 2);                           /* C0A4F5 lda nmitimen_shadow */
    a = t_read16(ss, (uint16_t) (dp + nmitimen_shadow));
    ss_set_nz16(ss, a);
    S(0xA4F7, 3);                           /* C0A4F7 sta NMITIMEN (and WRIO) */
    t_write16(ss, ss_abs(ss, NMITIMEN), a);
    S(0xA4FA, 3);                           /* C0A4FA stz JOYSER0 */
    t_write16(ss, ss_abs(ss, JOYSER0), 0);
  }
  TAIL(pb, 0xA4FD);                         /* loc_C0A4FD: wai / bra, the ROM's */
}

/* ---------------------------------------------------------------------------
 * nmi_handler_gameplay — $C0:80F4
 *
 * The game's main loop. It runs inside NMI, once per frame, for game modes 0-2
 * (the level scenes) and mode 3 (the title, once its fade-in handler has handed
 * over). Reading down it: reset the stack, blank OAM's address, run the mode's
 * vblank half through jtbl_C08272, flush the palette and tile upload queues,
 * blank the screen for the rest of the frame, step the INIDISP fade ($30 towards
 * $32), take Select as the debug mode-cycle when no fade is running, and -- if
 * $4A (pause) is clear -- run the simulation: camera, the two metatile columns,
 * the mode's per-frame handler, the walk-cycle timer and its footstep sound, the
 * weather and effect spawners, then every live entity. What is left is the fade
 * bookkeeping, the mode advance when both fade words are zero, and the draw
 * half: sort, clear the sprite table, the mode's two particle dispatchers either
 * side of entity_build_oam_frame, hide the unused sprites and DMA OAM. Mode 0
 * finishes by block-moving the 392-byte HDMA scroll ramp ppu_regs_default built
 * at $0DB9 into $0C31, where the channel set up for it reads from.
 *
 * Every call goes through the emulator with a real pushed frame, so the twenty
 * converted callees each fire their own hook, and the four `jsr (table,X)`
 * dispatches reach the mode routines the same way.
 * ------------------------------------------------------------------------- */
static void nmi_handler_gameplay(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);
  uint16_t v;

  SIMM16(0x80F4); x = 0x01FF; ss_set_nz16(ss, x);      /* C080F4 ldx #$01FF */
  SI(0x80F7); ss_set_sp(ss, x);                        /* C080F7 txs */
  S(0x80F8, 3); t_write16(ss, ss_abs(ss, OAMADDL), 0); /* C080F8 stz OAMADDL */
  S(0x80FB, 2);                                        /* C080FB lda game_mode */
  a = t_read16(ss, (uint16_t) (dp + game_mode)); ss_set_nz16(ss, a);
  SI(0x80FD); a = alu_asl16(ss, a);                    /* C080FD asl A */
  SI(0x80FE); x = a; ss_set_nz16(ss, x);               /* C080FE tax */
  JSR_IAX(0x80FF, 0x8272);                             /* C080FF jsr (jtbl_C08272,X) */
  JSR(0x8102, 0x9D32);                                 /* C08102 jsr cgram_upload_queue_flush */
  JSR(0x8105, 0xAE7E);                                 /* C08105 jsr entity_upload_pending_tiles */
  S(0x8108, 3); t_write16(ss, ss_abs(ss, 0x0A88), 0);  /* C08108 stz $0A88 */

  SEP(0x810B, 0x20);                                   /* C0810B sep #$20 */
  S(0x810D, 3); t_write8(ss, ss_abs(ss, NMITIMEN), 0); /* C0810D stz NMITIMEN */
  S(0x8110, 2);                                        /* C08110 lda pause_flag */
  { uint8_t b = t_read8(ss, (uint16_t) (dp + pause_flag));
    a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b); }
  { const bool taken = (a & 0xff) != 0;
    S(0x8112, 1); t_branch(ss, taken);                 /* C08112 bne loc_C08116 */
    if(!taken) {
      S(0x8114, 2);                                    /* C08114 lda inidisp_shadow */
      uint8_t b = t_read8(ss, (uint16_t) (dp + inidisp_shadow));
      a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b);
    } }
  /* loc_C08116 */
  S(0x8116, 3); t_write8(ss, ss_abs(ss, INIDISP_), (uint8_t) a);  /* sta INIDISP */
  REP(0x8119, 0x20);                                   /* C08119 rep #$20 */

  S(0x811B, 2);                                        /* C0811B lda fade_delta */
  a = t_read16(ss, (uint16_t) (dp + fade_delta)); ss_set_nz16(ss, a);
  { const bool taken = a == 0;
    S(0x811D, 1); t_branch(ss, taken);                 /* C0811D beq loc_C08133 */
    if(taken) goto loc_C08133; }
  { const bool taken = (a & 0x8000) == 0;
    S(0x811F, 1); t_branch(ss, taken);                 /* C0811F bpl loc_C08127 */
    if(taken) goto loc_C08127; }
  S(0x8121, 2);                                        /* C08121 adc fade_level */
  a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + fade_level)));
  { const bool taken = a != 0;
    S(0x8123, 1); t_branch(ss, taken);                 /* C08123 bne loc_C08131 */
    if(taken) goto loc_C08131; }
  S(0x8125, 1); t_branch(ss, true);                    /* C08125 bra loc_C0812F */
  goto loc_C0812F;

loc_C08127:
  SI(0x8127); ss_set_c(ss, false);                     /* C08127 clc */
  S(0x8128, 2);                                        /* C08128 adc fade_level */
  a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + fade_level)));
  SIMM16(0x812A); alu_cmp16(ss, a, 0x0F00);            /* C0812A cmp #$0F00 */
  { const bool taken = !ss_c(ss);
    S(0x812D, 1); t_branch(ss, taken);                 /* C0812D bcc loc_C08131 */
    if(taken) goto loc_C08131; }

loc_C0812F:
  S(0x812F, 2); t_write16(ss, (uint16_t) (dp + fade_delta), 0);  /* stz fade_delta */

loc_C08131:
  S(0x8131, 2); t_write16(ss, (uint16_t) (dp + fade_level), a);  /* sta fade_level */

loc_C08133:
  S(0x8133, 2);                                        /* C08133 lda fade_delta */
  a = t_read16(ss, (uint16_t) (dp + fade_delta)); ss_set_nz16(ss, a);
  { const bool taken = a != 0;
    S(0x8135, 1); t_branch(ss, taken);                 /* C08135 bne loc_C08147 */
    if(taken) goto loc_C08147; }
  S(0x8137, 2);                                        /* C08137 lda joy1_pressed */
  a = t_read16(ss, (uint16_t) (dp + joy1_pressed)); ss_set_nz16(ss, a);
  S(0x8139, 2);                                        /* C08139 ora joy2_pressed */
  a = alu_ora16(ss, a, t_read16(ss, (uint16_t) (dp + joy2_pressed)));
  SIMM16(0x813B); alu_bit_imm16(ss, a, 0x1000);        /* C0813B bit #$1000 (Select) */
  { const bool taken = ss_z(ss);
    S(0x813E, 1); t_branch(ss, taken);                 /* C0813E beq loc_C08147 */
    if(taken) goto loc_C08147; }
  S(0x8140, 2);                                        /* C08140 lda pause_flag */
  a = t_read16(ss, (uint16_t) (dp + pause_flag)); ss_set_nz16(ss, a);
  SIMM16(0x8142); a = alu_eor16(ss, a, 0x000A);        /* C08142 eor #$000A */
  S(0x8145, 2); t_write16(ss, (uint16_t) (dp + pause_flag), a);  /* sta pause_flag */

loc_C08147:
  S(0x8147, 2);                                        /* C08147 lda pause_flag */
  a = t_read16(ss, (uint16_t) (dp + pause_flag)); ss_set_nz16(ss, a);
  { const bool taken = a == 0;
    S(0x8149, 1); t_branch(ss, taken);                 /* C08149 beq loc_C0814E */
    if(!taken) { SJMP(0x814B); goto loc_C0822B; } }    /* C0814B jmp loc_C0822B */

  /* loc_C0814E: the simulation half */
  JSR(0x814E, 0xA1B0);                                 /* jsr camera_follow_player */
  JSR(0x8151, 0x9E83);                                 /* jsr build_metatile_column_500 */
  JSR(0x8154, 0x9FB7);                                 /* jsr build_metatile_column_580 */
  S(0x8157, 2);                                        /* C08157 lda game_mode */
  a = t_read16(ss, (uint16_t) (dp + game_mode)); ss_set_nz16(ss, a);
  SI(0x8159); a = alu_asl16(ss, a);                    /* C08159 asl A */
  SI(0x815A); x = a; ss_set_nz16(ss, x);               /* C0815A tax */
  JSR_IAX(0x815B, 0x827A);                             /* jsr (jtbl_C0827A,X) */

  S(0x815E, 2);                                        /* C0815E lda $78 */
  a = t_read16(ss, (uint16_t) (dp + 0x0078)); ss_set_nz16(ss, a);
  { const bool taken = a == 0;
    S(0x8160, 1); t_branch(ss, taken);                 /* C08160 beq loc_C08192 */
    if(taken) goto loc_C08192; }
  S(0x8162, 2); v = t_inc16(ss, (uint16_t) (dp + 0x0078), -1);   /* C08162 dec $78 */
  { const bool taken = v != 0;
    S(0x8164, 1); t_branch(ss, taken);                 /* C08164 bne loc_C08192 */
    if(taken) goto loc_C08192; }
  S(0x8166, 2);                                        /* C08166 lda walk_cycle_timer */
  a = t_read16(ss, (uint16_t) (dp + 0x0070)); ss_set_nz16(ss, a);
  { const bool taken = a != 0;
    S(0x8168, 1); t_branch(ss, taken);                 /* C08168 bne loc_C08192 */
    if(taken) goto loc_C08192; }
  SIMM16(0x816A); a = 0x003C; ss_set_nz16(ss, a);      /* C0816A lda #$003C */
  S(0x816D, 2); t_write16(ss, (uint16_t) (dp + 0x0070), a);      /* sta walk_cycle_timer */
  S(0x816F, 3); t_write16(ss, ss_abs(ss, 0x0C1F), 0);  /* C0816F stz $0C1F */
  SIMM16(0x8172); a = 0x0004; ss_set_nz16(ss, a);      /* C08172 lda #$0004 */
  S(0x8175, 3); t_write16(ss, ss_abs(ss, 0x0C21), a);  /* C08175 sta $0C21 */
  SIMM16(0x8178); a = 0x00FF; ss_set_nz16(ss, a);      /* C08178 lda #$00FF */
  S(0x817B, 3); t_write16(ss, ss_abs(ss, 0x0C23), a);  /* C0817B sta $0C23 */
  S(0x817E, 3); t_write16(ss, ss_abs(ss, 0x0C29), a);  /* C0817E sta $0C29 */
  SIMM16(0x8181); a = 0x0080; ss_set_nz16(ss, a);      /* C08181 lda #$0080 */
  S(0x8184, 3); t_write16(ss, ss_abs(ss, 0x0C25), a);  /* C08184 sta $0C25 */
  S(0x8187, 2);                                        /* C08187 lda camera_x */
  a = t_read16(ss, (uint16_t) (dp + camera_x)); ss_set_nz16(ss, a);
  S(0x8189, 3); t_write16(ss, ss_abs(ss, 0x0C27), a);  /* C08189 sta $0C27 */
  SIMM16(0x818C); a = 0x0001; ss_set_nz16(ss, a);      /* C0818C lda #$0001 */
  S(0x818F, 3); t_write16(ss, ss_abs(ss, 0x0C2B), a);  /* C0818F sta $0C2B */

loc_C08192:
  S(0x8192, 2);                                        /* C08192 ldx walk_cycle_timer */
  x = t_read16(ss, (uint16_t) (dp + 0x0070)); ss_set_nz16(ss, x);
  { const bool taken = x == 0;
    S(0x8194, 1); t_branch(ss, taken);                 /* C08194 beq loc_C081A6 */
    if(taken) goto loc_C081A6; }
  SIMM16(0x8196); alu_cpx16(ss, x, 0x003C);            /* C08196 cpx #$003C */
  { const bool taken = x != 0x003C;
    S(0x8199, 1); t_branch(ss, taken);                 /* C08199 bne loc_C0819E */
    if(!taken) JSR(0x819B, 0xB075); }                  /* jsr play_footstep_sound */
  /* loc_C0819E */
  S(0x819E, 2); t_inc16(ss, (uint16_t) (dp + 0x0070), -1);   /* C0819E dec walk_cycle_timer */
  S(0x81A0, 2); t_inc16(ss, (uint16_t) (dp + 0x0070), -1);   /* C081A0 dec walk_cycle_timer */
  SI(0x81A2); a = x; ss_set_nz16(ss, a);               /* C081A2 txa */
  S(0x81A3, 2);                                        /* C081A3 ora walk_cycle_parity */
  a = alu_ora16(ss, a, t_read16(ss, (uint16_t) (dp + 0x0072)));
  SI(0x81A5); x = a; ss_set_nz16(ss, x);               /* C081A5 tax */

loc_C081A6:
  S(0x81A6, 4);                                        /* C081A6 lda data_C46A88,X */
  a = t_read16(ss, (0xC46A88u + x) & 0xffffff); ss_set_nz16(ss, a);
  S(0x81AA, 2); t_write16(ss, (uint16_t) (dp + 0x0074), a);  /* C081AA sta $74 */
  SIMM16(0x81AC); alu_cmp16(ss, a, 0x8000);            /* C081AC cmp #$8000 */
  SI(0x81AF); a = alu_ror16(ss, a);                    /* C081AF ror A */
  S(0x81B0, 2); t_write16(ss, (uint16_t) (dp + 0x0076), a);  /* C081B0 sta $76 */
  S(0x81B2, 3);                                        /* C081B2 lda $0C1B */
  a = t_read16(ss, ss_abs(ss, 0x0C1B)); ss_set_nz16(ss, a);
  { const bool taken = a == 0;
    S(0x81B5, 1); t_branch(ss, taken);                 /* C081B5 beq loc_C081BA */
    if(!taken) JSR(0x81B7, 0x91BB); }                  /* jsr cgram_palette_ramp_step */
  /* loc_C081BA */
  S(0x81BA, 3);                                        /* C081BA lda $0C15 */
  a = t_read16(ss, ss_abs(ss, 0x0C15)); ss_set_nz16(ss, a);
  { const bool taken = a == 0;
    S(0x81BD, 1); t_branch(ss, taken);                 /* C081BD beq loc_C081C2 */
    if(!taken) JSR(0x81BF, 0x9253); }                  /* jsr particle_spawn_mode0_weather */

  /* loc_C081C2: one entity_update_tick per live slot */
  SIMM16(0x81C2); x = 0x0000; ss_set_nz16(ss, x);      /* C081C2 ldx #$0000 */
  for(;;) {                                            /* loc_C081C5 */
    JSR(0x81C5, 0x98DA);                               /* jsr entity_update_tick */
    SI(0x81C8); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);   /* inx */
    SI(0x81C9); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);   /* inx */
    S(0x81CA, 2);                                      /* C081CA cpx $A6 */
    alu_cpx16(ss, x, t_read16(ss, (uint16_t) (dp + 0x00A6)));
    { const bool taken = !ss_c(ss);
      S(0x81CC, 1); t_branch(ss, taken);               /* C081CC bcc loc_C081C5 */
      if(!taken) break; }
  }

  S(0x81CE, 2);                                        /* C081CE lda fade_level */
  a = t_read16(ss, (uint16_t) (dp + fade_level)); ss_set_nz16(ss, a);
  S(0x81D0, 2);                                        /* C081D0 ora fade_delta */
  a = alu_ora16(ss, a, t_read16(ss, (uint16_t) (dp + fade_delta)));
  { const bool taken = a == 0;
    S(0x81D2, 1); t_branch(ss, taken);                 /* C081D2 beq loc_C08209 */
    if(taken) goto loc_C08209; }
  S(0x81D4, 2);                                        /* C081D4 lda joy1_pressed */
  a = t_read16(ss, (uint16_t) (dp + joy1_pressed)); ss_set_nz16(ss, a);
  SIMM16(0x81D6); alu_bit_imm16(ss, a, 0x0040);        /* C081D6 bit #$0040 */
  { const bool taken = ss_z(ss);
    S(0x81D9, 1); t_branch(ss, taken);                 /* C081D9 beq loc_C081F5 */
    if(taken) goto loc_C081F5; }
  S(0x81DB, 3);                                        /* C081DB lda $0BB6 */
  a = t_read16(ss, ss_abs(ss, 0x0BB6)); ss_set_nz16(ss, a);
  SI(0x81DE); a = alu_inc16(ss, a);                    /* C081DE inc A */
  SI(0x81DF); a = alu_inc16(ss, a);                    /* C081DF inc A */
  SIMM16(0x81E0); alu_cmp16(ss, a, 0x0006);            /* C081E0 cmp #$0006 */
  { const bool taken = !ss_c(ss);
    S(0x81E3, 1); t_branch(ss, taken);                 /* C081E3 bcc loc_C081E6 */
    if(!taken) { SI(0x81E5); a = ss_dp(ss); ss_set_nz16(ss, a); } }  /* tdc */
  /* loc_C081E6 */
  S(0x81E6, 3); t_write16(ss, ss_abs(ss, 0x0BB6), a);  /* C081E6 sta $0BB6 */
  S(0x81E9, 3); t_write16(ss, ss_abs(ss, 0x0A48), 0);  /* C081E9 stz $0A48 */
  S(0x81EC, 3); t_write16(ss, ss_abs(ss, 0x0A4A), 0);  /* C081EC stz $0A4A */
  S(0x81EF, 3); t_write16(ss, ss_abs(ss, 0x0A4C), 0);  /* C081EF stz $0A4C */
  S(0x81F2, 3); t_write16(ss, ss_abs(ss, 0x0A4E), 0);  /* C081F2 stz $0A4E */

loc_C081F5:
  S(0x81F5, 2);                                        /* C081F5 lda joy1_pressed */
  a = t_read16(ss, (uint16_t) (dp + joy1_pressed)); ss_set_nz16(ss, a);
  S(0x81F7, 2);                                        /* C081F7 ora joy2_pressed */
  a = alu_ora16(ss, a, t_read16(ss, (uint16_t) (dp + joy2_pressed)));
  SIMM16(0x81F9); alu_bit_imm16(ss, a, 0x2000);        /* C081F9 bit #$2000 (Start) */
  { const bool taken = ss_z(ss);
    S(0x81FC, 1); t_branch(ss, taken);                 /* C081FC beq loc_C08229 */
    if(taken) goto loc_C08229; }
  S(0x81FE, 2);                                        /* C081FE lda fade_delta */
  a = t_read16(ss, (uint16_t) (dp + fade_delta)); ss_set_nz16(ss, a);
  { const bool taken = a != 0;
    S(0x8200, 1); t_branch(ss, taken);                 /* C08200 bne loc_C08229 */
    if(taken) goto loc_C08229; }
  SIMM16(0x8202); a = 0xFF00; ss_set_nz16(ss, a);      /* C08202 lda #$FF00 */
  S(0x8205, 2); t_write16(ss, (uint16_t) (dp + fade_delta), a);  /* sta fade_delta */
  S(0x8207, 1); t_branch(ss, true);                    /* C08207 bra loc_C08229 */
  goto loc_C08229;

loc_C08209:
  S(0x8209, 2);                                        /* C08209 lda game_mode */
  a = t_read16(ss, (uint16_t) (dp + game_mode)); ss_set_nz16(ss, a);
  SI(0x820B); a = alu_inc16(ss, a);                    /* C0820B inc A */
  SIMM16(0x820C); alu_cmp16(ss, a, 0x0004);            /* C0820C cmp #$0004 */
  { const bool taken = !ss_c(ss);
    S(0x820F, 1); t_branch(ss, taken);                 /* C0820F bcc loc_C08212 */
    if(!taken) { SI(0x8211); a = ss_dp(ss); ss_set_nz16(ss, a); } }  /* tdc */
  /* loc_C08212 */
  S(0x8212, 2); t_write16(ss, (uint16_t) (dp + game_mode), a);   /* sta game_mode */
  S(0x8214, 2); t_write16(ss, (uint16_t) (dp + joy1_pressed), 0);/* stz $8C */
  S(0x8216, 2); t_write16(ss, (uint16_t) (dp + joy2_pressed), 0);/* stz $90 */
  SEP(0x8218, 0x20);                                   /* C08218 sep #$20 */
  SIMM8(0x821A); a = (uint16_t) ((a & 0xff00) | 0x01); ss_set_nz8(ss, 0x01);
  S(0x821C, 3); t_write8(ss, ss_abs(ss, NMITIMEN), 0x01);   /* sta NMITIMEN */
  SIMM8(0x821F); a = (uint16_t) ((a & 0xff00) | 0x80); ss_set_nz8(ss, 0x80);
  S(0x8221, 3); t_write8(ss, ss_abs(ss, INIDISP_), 0x80);   /* sta INIDISP */
  REP(0x8224, 0x20);                                   /* C08224 rep #$20 */
  SJMP(0x8226);                                        /* C08226 jmp loc_C0805E */
  TAIL(pb, 0x805E);

loc_C08229:
  S(0x8229, 2); t_inc16(ss, (uint16_t) (dp + frame_counter), 1);  /* C08229 inc $5E */

loc_C0822B:
  JSR(0x822B, 0xA2DE);                                 /* jsr read_joypads */
  S(0x822E, 2);                                        /* C0822E lda pause_flag */
  a = t_read16(ss, (uint16_t) (dp + pause_flag)); ss_set_nz16(ss, a);
  { const bool taken = a != 0;
    S(0x8230, 1); t_branch(ss, taken);                 /* C08230 bne loc_C08267 */
    if(taken) goto loc_C08267; }
  JSR(0x8232, 0xAE1F);                                 /* jsr entity_sort_draw_order */
  JSR(0x8235, 0xA500);                                 /* jsr clear_sprite_table */
  S(0x8238, 2);                                        /* C08238 lda game_mode */
  a = t_read16(ss, (uint16_t) (dp + game_mode)); ss_set_nz16(ss, a);
  SI(0x823A); a = alu_asl16(ss, a);                    /* C0823A asl A */
  SI(0x823B); x = a; ss_set_nz16(ss, x);               /* C0823B tax */
  JSR_IAX(0x823C, 0x828A);                             /* jsr (jtbl_C0828A,X) */
  JSL(0x823F, 0x80, 0xA538);                           /* jsl entity_build_oam_frame */
  S(0x8243, 2);                                        /* C08243 lda game_mode */
  a = t_read16(ss, (uint16_t) (dp + game_mode)); ss_set_nz16(ss, a);
  SI(0x8245); a = alu_asl16(ss, a);                    /* C08245 asl A */
  SI(0x8246); x = a; ss_set_nz16(ss, x);               /* C08246 tax */
  JSR_IAX(0x8247, 0x8282);                             /* jsr (jtbl_C08282,X) */
  JSR(0x824A, 0xADE7);                                 /* jsr oam_hide_unused_sprites */
  JSR(0x824D, 0xADFD);                                 /* jsr oam_dma_upload */
  S(0x8250, 2);                                        /* C08250 lda game_mode */
  a = t_read16(ss, (uint16_t) (dp + game_mode)); ss_set_nz16(ss, a);
  { const bool taken = a != 0;
    S(0x8252, 1); t_branch(ss, taken);                 /* C08252 bne loc_C08267 */
    if(taken) goto loc_C08267; }
  S(0x8254, 3);                                        /* C08254 lda $0C04 */
  a = t_read16(ss, ss_abs(ss, 0x0C04)); ss_set_nz16(ss, a);
  { const bool taken = a == 0;
    S(0x8257, 1); t_branch(ss, taken);                 /* C08257 beq loc_C08267 */
    if(taken) goto loc_C08267; }

  /* The scroll ramp ppu_regs_default built at $0DB9 into the HDMA table at
   * $0C31: 392 bytes, both banks $00. LakeSnes re-runs the whole `mvn`
   * instruction once per byte (case 0x54), backing the pc up three, so the
   * body does the same and the step macro's yield check lands between two
   * bytes of the move exactly where the 65816's would. */
  SIMM16(0x8259); x = 0x0DB9; ss_set_nz16(ss, x);      /* C08259 ldx #$0DB9 */
  SIMM16(0x825C); y = 0x0C31; ss_set_nz16(ss, y);      /* C0825C ldy #$0C31 */
  SIMM16(0x825F); a = 0x0187; ss_set_nz16(ss, a);      /* C0825F lda #$0187 */
  S(0x8262, 1);                                        /* C08262 phb */
  ss_idle(ss); ss_check_int(ss); ss_push8(ss, ss_db(ss));
  for(;;) {                                            /* C08263 mvn $00,$00 */
    S(0x8263, 3);
    ss_set_db(ss, 0x00);
    ss_bus_w8(ss, y, ss_bus_r8(ss, x));
    a = (uint16_t) (a - 1);
    x = (uint16_t) (x + 1);
    y = (uint16_t) (y + 1);
    if(ss_flag_x(ss)) { x = (uint16_t) (x & 0xff); y = (uint16_t) (y & 0xff); }
    ss_idle(ss); ss_check_int(ss); ss_idle(ss);
    if(a == 0xffff) break;
  }
  S(0x8266, 1);                                        /* C08266 plb */
  ss_idle(ss); ss_idle(ss); ss_check_int(ss);
  { const uint8_t b = ss_pull8(ss); ss_set_db(ss, b); ss_set_nz8(ss, b); }

loc_C08267:
  SJMP(0x8267);                                        /* C08267 jmp loc_C0A4F5 */
  TAIL(pb, 0xA4F5);
}

/* ---------------------------------------------------------------------------
 * ppu_regs_default — $C0:8BDB, and the loc_C08BE4 / loc_C08E2B / loc_C08E39 /
 * loc_C08E7F entries
 *
 * The mode-0 per-frame scroll and camera-zone update (see the file header on
 * the name). Nothing calls it: `mode0_camera_zone_update` runs into $8BDB by falling through
 * its own `bne` at $8BD9, and branches or jumps into the four labels above, so
 * all five are registered entry addresses of this one body.
 *
 * The shape, top to bottom: measure the camera against the zone origin $0C0F
 * and either arm the zone shift ($0C11 = $0097) or reset it; pick the horizon
 * scanline out of data_C46788 and publish it at $0BF4; step the zone state
 * machine in $0C0C through its $0100 / $2C00 / $2E00 / $3000 / $3F00 / $4000
 * codes, firing play_zone_transition_sound and the effect-table set-up at the
 * $0100 boundary and the walk-cycle reset at $4000; set the player's facing
 * bits in entity_flags; derive the vertical offsets from data_C46888 /
 * data_C46808 / data_C46B88 and the per-frame ramp step from data_C46988;
 * fill $0DB9 with 196 words -- constant down to index $E0, then a ramp adding
 * $0C2F -- and flag it ready in $0C04. The loc_C08E2B / loc_C08E39 entries are
 * the two shorter camera-only variants, and everything converges on
 * loc_C08E7F, which publishes $0BFC/$0BE4/$0BE6 and calls the VRAM stream
 * dispatcher. It then falls through into check_pending_player_attack, which is
 * converted in entities.c, so the body hands the pc to $8E8E rather than
 * returning.
 * ------------------------------------------------------------------------- */
static void ppu_regs_default_at(SnesState* ss, uint16_t entry) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  switch(entry) {
    case 0x8BE4: goto loc_C08BE4;
    case 0x8E2B: goto loc_C08E2B;
    case 0x8E39: goto loc_C08E39;
    case 0x8E7F: goto loc_C08E7F;
    default: break;
  }

  SIMM16(0x8BDB); a = 0x0097; ss_set_nz16(ss, a);      /* C08BDB lda #$0097 */
  S(0x8BDE, 3); t_write16(ss, ss_abs(ss, 0x0C11), a);  /* C08BDE sta $0C11 */
  S(0x8BE1, 3); t_write16(ss, ss_abs(ss, 0x0C13), 0);  /* C08BE1 stz $0C13 */

loc_C08BE4:
  S(0x8BE4, 3);                                        /* C08BE4 lda $0C0D */
  a = t_read16(ss, ss_abs(ss, 0x0C0D)); ss_set_nz16(ss, a);
  SI(0x8BE7); a = alu_asl16(ss, a);                    /* C08BE7 asl A */
  SI(0x8BE8); x = a; ss_set_nz16(ss, x);               /* C08BE8 tax */
  S(0x8BE9, 2);                                        /* C08BE9 lda camera_x */
  a = t_read16(ss, (uint16_t) (dp + camera_x)); ss_set_nz16(ss, a);
  SI(0x8BEB); ss_set_c(ss, true);                      /* C08BEB sec */
  S(0x8BEC, 3);                                        /* C08BEC sbc $0C0F */
  a = alu_sbc16(ss, a, t_read16(ss, ss_abs(ss, 0x0C0F)));
  { const bool taken = (a & 0x8000) == 0;
    S(0x8BEF, 1); t_branch(ss, taken);                 /* C08BEF bpl loc_C08BF8 */
    if(taken) goto loc_C08BF8; }
  SIMM16(0x8BF1); alu_cmp16(ss, a, 0xFF20);            /* C08BF1 cmp #$FF20 */
  { const bool taken = !ss_c(ss);
    S(0x8BF4, 1); t_branch(ss, taken);                 /* C08BF4 bcc loc_C08BFD */
    if(taken) goto loc_C08BFD; }
  S(0x8BF6, 1); t_branch(ss, true);                    /* C08BF6 bra loc_C08C09 */
  goto loc_C08C09;

loc_C08BF8:
  SIMM16(0x8BF8); alu_cmp16(ss, a, 0x0100);            /* C08BF8 cmp #$0100 */
  { const bool taken = !ss_c(ss);
    S(0x8BFB, 1); t_branch(ss, taken);                 /* C08BFB bcc loc_C08C09 */
    if(taken) goto loc_C08C09; }

loc_C08BFD:
  SIMM16(0x8BFD); a = 0x0100; ss_set_nz16(ss, a);      /* C08BFD lda #$0100 */
  S(0x8C00, 3); t_write16(ss, ss_abs(ss, 0x0C0C), 0);  /* C08C00 stz $0C0C */
  S(0x8C03, 3); t_write16(ss, ss_abs(ss, 0x0C11), 0);  /* C08C03 stz $0C11 */
  S(0x8C06, 3); t_write16(ss, ss_abs(ss, 0x0C13), 0);  /* C08C06 stz $0C13 */

loc_C08C09:
  S(0x8C09, 2); t_write16(ss, (uint16_t) (dp + ptr_04), a);   /* C08C09 sta ptr_04 */
  S(0x8C0B, 3);                                        /* C08C0B ldy $0C11 */
  y = t_read16(ss, ss_abs(ss, 0x0C11)); ss_set_nz16(ss, y);
  { const bool taken = y == 0;
    S(0x8C0E, 1); t_branch(ss, taken);                 /* C08C0E beq loc_C08C21 */
    if(taken) goto loc_C08C21; }
  S(0x8C10, 3);                                        /* C08C10 ldy $0C0C */
  y = t_read16(ss, ss_abs(ss, 0x0C0C)); ss_set_nz16(ss, y);
  { const bool taken = y == 0;
    S(0x8C13, 1); t_branch(ss, taken);                 /* C08C13 beq loc_C08C21 */
    if(taken) goto loc_C08C21; }
  SI(0x8C15); ss_set_c(ss, false);                     /* C08C15 clc */
  S(0x8C16, 4);                                        /* C08C16 adc data_C46788,X */
  a = alu_adc16(ss, a, t_read16(ss, (0xC46788u + x) & 0xffffff));
  { const bool taken = (a & 0x8000) != 0;
    S(0x8C1A, 1); t_branch(ss, taken);                 /* C08C1A bmi loc_C08C24 */
    if(taken) goto loc_C08C24; }
  SIMM16(0x8C1C); alu_cmp16(ss, a, 0x00D9);            /* C08C1C cmp #$00D9 */
  { const bool taken = !ss_c(ss);
    S(0x8C1F, 1); t_branch(ss, taken);                 /* C08C1F bcc loc_C08C24 */
    if(taken) goto loc_C08C24; }

loc_C08C21:
  SIMM16(0x8C21); a = 0x00D8; ss_set_nz16(ss, a);      /* C08C21 lda #$00D8 */

loc_C08C24:
  SI(0x8C24); ss_set_c(ss, false);                     /* C08C24 clc */
  SIMM16(0x8C25); a = alu_adc16(ss, a, 0x0028);        /* C08C25 adc #$0028 */
  S(0x8C28, 3); t_write16(ss, ss_abs(ss, 0x0BF4), a);  /* C08C28 sta $0BF4 */
  S(0x8C2B, 3);                                        /* C08C2B lda $0C0C */
  a = t_read16(ss, ss_abs(ss, 0x0C0C)); ss_set_nz16(ss, a);
  S(0x8C2E, 3);                                        /* C08C2E ldy $0C11 */
  y = t_read16(ss, ss_abs(ss, 0x0C11)); ss_set_nz16(ss, y);
  { const bool taken = y == 0;
    S(0x8C31, 1); t_branch(ss, taken);                 /* C08C31 beq loc_C08C56 */
    if(taken) goto loc_C08C56; }
  { const bool taken = (y & 0x8000) == 0;
    S(0x8C33, 1); t_branch(ss, taken);                 /* C08C33 bpl loc_C08C4B */
    if(taken) goto loc_C08C4B; }
  S(0x8C35, 3); t_inc16(ss, ss_abs(ss, 0x0C11), 1);    /* C08C35 inc $0C11 */
  SI(0x8C38); y = a; ss_set_nz16(ss, y);               /* C08C38 tay */
  { const bool taken = y == 0;
    S(0x8C39, 1); t_branch(ss, taken);                 /* C08C39 beq loc_C08C47 */
    if(taken) goto loc_C08C47; }
  SIMM16(0x8C3B); y = 0xFF88; ss_set_nz16(ss, y);      /* C08C3B ldy #$FF88 */
  S(0x8C3E, 3); t_write16(ss, ss_abs(ss, 0x0C11), y);  /* C08C3E sty $0C11 */
  SI(0x8C41); ss_set_c(ss, true);                      /* C08C41 sec */
  SIMM16(0x8C42); a = alu_sbc16(ss, a, 0x0100);        /* C08C42 sbc #$0100 */
  { const bool taken = (a & 0x8000) == 0;
    S(0x8C45, 1); t_branch(ss, taken);                 /* C08C45 bpl loc_C08C48 */
    if(taken) goto loc_C08C48; }

loc_C08C47:
  SI(0x8C47); a = ss_dp(ss); ss_set_nz16(ss, a);       /* C08C47 tdc */

loc_C08C48:
  SJMP(0x8C48); goto loc_C08CF7;                       /* C08C48 jmp loc_C08CF7 */

loc_C08C4B:
  S(0x8C4B, 3); t_inc16(ss, ss_abs(ss, 0x0C11), -1);   /* C08C4B dec $0C11 */
  S(0x8C4E, 3); t_inc16(ss, ss_abs(ss, 0x0C11), -1);   /* C08C4E dec $0C11 */
  SIMM16(0x8C51); alu_cmp16(ss, a, 0x3F00);            /* C08C51 cmp #$3F00 */
  { const bool taken = !ss_c(ss);
    S(0x8C54, 1); t_branch(ss, taken);                 /* C08C54 bcc loc_C08C59 */
    if(taken) goto loc_C08C59; }

loc_C08C56:
  SJMP(0x8C56); goto loc_C08CFA;                       /* C08C56 jmp loc_C08CFA */

loc_C08C59:
  SIMM16(0x8C59); alu_cmp16(ss, a, 0x2000);            /* C08C59 cmp #$2000 */
  { const bool taken = !ss_c(ss);
    S(0x8C5C, 1); t_branch(ss, taken);                 /* C08C5C bcc loc_C08C62 */
    if(!taken) {
      SI(0x8C5E); ss_set_c(ss, false);                 /* C08C5E clc */
      SIMM16(0x8C5F); a = alu_adc16(ss, a, 0x0100);    /* C08C5F adc #$0100 */
    } }

  /* loc_C08C62 */
  SIMM16(0x8C62); a = alu_adc16(ss, a, 0x0100);        /* C08C62 adc #$0100 */
  SIMM16(0x8C65); alu_cmp16(ss, a, 0x3F00);            /* C08C65 cmp #$3F00 */
  { const bool taken = !ss_c(ss);
    S(0x8C68, 1); t_branch(ss, taken);                 /* C08C68 bcc loc_C08C80 */
    if(taken) goto loc_C08C80; }
  S(0x8C6A, 3);                                        /* C08C6A ldy $0C15 */
  y = t_read16(ss, ss_abs(ss, 0x0C15)); ss_set_nz16(ss, y);
  { const bool taken = y != 0;
    S(0x8C6D, 1); t_branch(ss, taken);                 /* C08C6D bne loc_C08C80 */
    if(taken) goto loc_C08C80; }
  SI(0x8C6F); y = a; ss_set_nz16(ss, y);               /* C08C6F tay */
  SIMM16(0x8C70); a = 0x000A; ss_set_nz16(ss, a);      /* C08C70 lda #$000A */
  S(0x8C73, 3); t_write16(ss, ss_abs(ss, 0x0C15), a);  /* C08C73 sta $0C15 */
  S(0x8C76, 3);                                        /* C08C76 lda $0C0F */
  a = t_read16(ss, ss_abs(ss, 0x0C0F)); ss_set_nz16(ss, a);
  SIMM16(0x8C79); a = alu_adc16(ss, a, 0x007F);        /* C08C79 adc #$007F */
  S(0x8C7C, 3); t_write16(ss, ss_abs(ss, 0x0C17), a);  /* C08C7C sta $0C17 */
  SI(0x8C7F); a = y; ss_set_nz16(ss, a);               /* C08C7F tya */

loc_C08C80:
  SIMM16(0x8C80); alu_cmp16(ss, a, 0x4000);            /* C08C80 cmp #$4000 */
  { const bool taken = ss_c(ss);
    S(0x8C83, 1); t_branch(ss, taken);                 /* C08C83 bcs loc_C08CE6 */
    if(taken) goto loc_C08CE6; }
  SIMM16(0x8C85); alu_cmp16(ss, a, 0x2000);            /* C08C85 cmp #$2000 */
  { const bool taken = ss_c(ss);
    S(0x8C88, 1); t_branch(ss, taken);                 /* C08C88 bcs loc_C08CC1 */
    if(taken) goto loc_C08CC1; }
  SIMM16(0x8C8A); alu_cmp16(ss, a, 0x0100);            /* C08C8A cmp #$0100 */
  { const bool taken = a != 0x0100;
    S(0x8C8D, 1); t_branch(ss, taken);                 /* C08C8D bne loc_C08CC1 */
    if(taken) goto loc_C08CC1; }
  SI(0x8C8F); y = a; ss_set_nz16(ss, y);               /* C08C8F tay */
  JSR(0x8C90, 0xB0BC);                                 /* jsr play_zone_transition_sound */
  SI(0x8C93); a = y; ss_set_nz16(ss, a);               /* C08C93 tya */
  S(0x8C94, 3); t_write16(ss, ss_abs(ss, 0x0C1F), 0);  /* C08C94 stz $0C1F */
  SIMM16(0x8C97); y = 0x0010; ss_set_nz16(ss, y);      /* C08C97 ldy #$0010 */
  S(0x8C9A, 3); t_write16(ss, ss_abs(ss, 0x0C21), y);  /* C08C9A sty $0C21 */
  SIMM16(0x8C9D); y = 0x01FF; ss_set_nz16(ss, y);      /* C08C9D ldy #$01FF */
  S(0x8CA0, 3); t_write16(ss, ss_abs(ss, 0x0C23), y);  /* C08CA0 sty $0C23 */
  SIMM16(0x8CA3); y = 0x0100; ss_set_nz16(ss, y);      /* C08CA3 ldy #$0100 */
  S(0x8CA6, 3); t_write16(ss, ss_abs(ss, 0x0C25), y);  /* C08CA6 sty $0C25 */
  SI(0x8CA9); y = a; ss_set_nz16(ss, y);               /* C08CA9 tay */
  S(0x8CAA, 3);                                        /* C08CAA lda $0C0F */
  a = t_read16(ss, ss_abs(ss, 0x0C0F)); ss_set_nz16(ss, a);
  SI(0x8CAD); ss_set_c(ss, false);                     /* C08CAD clc */
  SIMM16(0x8CAE); a = alu_adc16(ss, a, 0x0040);        /* C08CAE adc #$0040 */
  S(0x8CB1, 3); t_write16(ss, ss_abs(ss, 0x0C27), a);  /* C08CB1 sta $0C27 */
  SIMM16(0x8CB4); a = 0x007F; ss_set_nz16(ss, a);      /* C08CB4 lda #$007F */
  S(0x8CB7, 3); t_write16(ss, ss_abs(ss, 0x0C29), a);  /* C08CB7 sta $0C29 */
  SIMM16(0x8CBA); a = 0x0003; ss_set_nz16(ss, a);      /* C08CBA lda #$0003 */
  S(0x8CBD, 3); t_write16(ss, ss_abs(ss, 0x0C2B), a);  /* C08CBD sta $0C2B */
  SI(0x8CC0); a = y; ss_set_nz16(ss, a);               /* C08CC0 tya */

loc_C08CC1:
  SIMM16(0x8CC1); alu_cmp16(ss, a, 0x2C00);            /* C08CC1 cmp #$2C00 */
  { const bool taken = !ss_c(ss);
    S(0x8CC4, 1); t_branch(ss, taken);                 /* C08CC4 bcc loc_C08CF7 */
    if(taken) goto loc_C08CF7; }
  S(0x8CC6, 3); t_write16(ss, ss_abs(ss, 0x0C0C), a);  /* C08CC6 sta $0C0C */
  S(0x8CC9, 2);                                        /* C08CC9 ldy $1A */
  y = t_read16(ss, (uint16_t) (dp + scratch_1A)); ss_set_nz16(ss, y);
  SIMM16(0x8CCB); alu_cmp16(ss, y, 0x0030);            /* C08CCB cpy #$0030 */
  { const bool taken = ss_c(ss);
    S(0x8CCE, 1); t_branch(ss, taken);                 /* C08CCE bcs loc_C08CFA */
    if(taken) goto loc_C08CFA; }
  S(0x8CD0, 3);                                        /* C08CD0 lda entity_state */
  a = t_read16(ss, ss_abs(ss, entity_state)); ss_set_nz16(ss, a);
  SIMM16(0x8CD3); a = alu_and16(ss, a, 0xFFFC);        /* C08CD3 and #$FFFC */
  SIMM16(0x8CD6); alu_cmp16(ss, a, 0x0014);            /* C08CD6 cmp #$0014 */
  { const bool taken = a == 0x0014;
    S(0x8CD9, 1); t_branch(ss, taken);                 /* C08CD9 beq loc_C08CFA */
    if(taken) goto loc_C08CFA; }
  SIMM16(0x8CDB); a = alu_and16(ss, a, 0x0002);        /* C08CDB and #$0002 */
  SIMM16(0x8CDE); a = alu_ora16(ss, a, 0x0014);        /* C08CDE ora #$0014 */
  S(0x8CE1, 3); t_write16(ss, ss_abs(ss, entity_state), a);  /* C08CE1 sta entity_state */
  S(0x8CE4, 1); t_branch(ss, true);                    /* C08CE4 bra loc_C08CFA */
  goto loc_C08CFA;

loc_C08CE6:
  SIMM16(0x8CE6); a = 0x003C; ss_set_nz16(ss, a);      /* C08CE6 lda #$003C */
  S(0x8CE9, 2); t_write16(ss, (uint16_t) (dp + 0x0070), a);  /* sta walk_cycle_timer */
  SIMM16(0x8CEB); a = 0x0080; ss_set_nz16(ss, a);      /* C08CEB lda #$0080 */
  S(0x8CEE, 2); t_write16(ss, (uint16_t) (dp + 0x0072), a);  /* sta walk_cycle_parity */
  S(0x8CF0, 2); t_write16(ss, (uint16_t) (dp + 0x0074), 0);  /* C08CF0 stz $74 */
  S(0x8CF2, 2); t_write16(ss, (uint16_t) (dp + 0x0076), 0);  /* C08CF2 stz $76 */
  SIMM16(0x8CF4); a = 0x3F00; ss_set_nz16(ss, a);      /* C08CF4 lda #$3F00 */

loc_C08CF7:
  S(0x8CF7, 3); t_write16(ss, ss_abs(ss, 0x0C0C), a);  /* C08CF7 sta $0C0C */

loc_C08CFA:
  S(0x8CFA, 2);                                        /* C08CFA ldy $1A */
  y = t_read16(ss, (uint16_t) (dp + scratch_1A)); ss_set_nz16(ss, y);
  SIMM16(0x8CFC); alu_cmp16(ss, y, 0x0030);            /* C08CFC cpy #$0030 */
  { const bool taken = !ss_c(ss);
    S(0x8CFF, 1); t_branch(ss, taken);                 /* C08CFF bcc loc_C08D27 */
    if(taken) goto loc_C08D27; }
  S(0x8D01, 3);                                        /* C08D01 lda entity_flags */
  a = t_read16(ss, ss_abs(ss, entity_flags)); ss_set_nz16(ss, a);
  SIMM16(0x8D04); a = alu_and16(ss, a, 0xCFFF);        /* C08D04 and #$CFFF */
  SIMM16(0x8D07); a = alu_ora16(ss, a, 0x2000);        /* C08D07 ora #$2000 */
  S(0x8D0A, 2);                                        /* C08D0A ldy $20 */
  y = t_read16(ss, (uint16_t) (dp + scratch_20)); ss_set_nz16(ss, y);
  { const bool taken = (y & 0x8000) == 0;
    S(0x8D0C, 1); t_branch(ss, taken);                 /* C08D0C bpl loc_C08D11 */
    if(!taken) { SIMM16(0x8D0E); a = alu_ora16(ss, a, 0x3000); } }  /* ora #$3000 */
  /* loc_C08D11 */
  S(0x8D11, 3); t_write16(ss, ss_abs(ss, entity_flags), a);  /* sta entity_flags */
  S(0x8D14, 3);                                        /* C08D14 lda $0C1B */
  a = t_read16(ss, ss_abs(ss, 0x0C1B)); ss_set_nz16(ss, a);
  { const bool taken = a == 0;
    S(0x8D17, 1); t_branch(ss, taken);                 /* C08D17 beq loc_C08D48 */
    if(taken) goto loc_C08D48; }
  SIMM16(0x8D19); a = 0xFFFF; ss_set_nz16(ss, a);      /* C08D19 lda #$FFFF */
  S(0x8D1C, 3); t_write16(ss, ss_abs(ss, 0x0C1B), a);  /* C08D1C sta $0C1B */
  SIMM16(0x8D1F); a = 0x0080; ss_set_nz16(ss, a);      /* C08D1F lda #$0080 */
  S(0x8D22, 3); t_write16(ss, ss_abs(ss, 0x0C1D), a);  /* C08D22 sta $0C1D */
  S(0x8D25, 1); t_branch(ss, true);                    /* C08D25 bra loc_C08D48 */
  goto loc_C08D48;

loc_C08D27:
  S(0x8D27, 3);                                        /* C08D27 lda entity_flags */
  a = t_read16(ss, ss_abs(ss, entity_flags)); ss_set_nz16(ss, a);
  SIMM16(0x8D2A); a = alu_and16(ss, a, 0xCFFF);        /* C08D2A and #$CFFF */
  SIMM16(0x8D2D); a = alu_ora16(ss, a, 0x2000);        /* C08D2D ora #$2000 */
  S(0x8D30, 3); t_write16(ss, ss_abs(ss, entity_flags), a);  /* sta entity_flags */
  S(0x8D33, 3);                                        /* C08D33 lda $0C0C */
  a = t_read16(ss, ss_abs(ss, 0x0C0C)); ss_set_nz16(ss, a);
  SIMM16(0x8D36); alu_cmp16(ss, a, 0x2E00);            /* C08D36 cmp #$2E00 */
  { const bool taken = a != 0x2E00;
    S(0x8D39, 1); t_branch(ss, taken);                 /* C08D39 bne loc_C08D48 */
    if(taken) goto loc_C08D48; }
  S(0x8D3B, 3);                                        /* C08D3B lda $0C11 */
  a = t_read16(ss, ss_abs(ss, 0x0C11)); ss_set_nz16(ss, a);
  { const bool taken = (a & 0x8000) == 0;
    S(0x8D3E, 1); t_branch(ss, taken);                 /* C08D3E bpl loc_C08D45 */
    if(!taken) {
      S(0x8D40, 3);                                    /* C08D40 ldy $0C1D */
      y = t_read16(ss, ss_abs(ss, 0x0C1D)); ss_set_nz16(ss, y);
      const bool t2 = y == 0;
      S(0x8D43, 1); t_branch(ss, t2);                  /* C08D43 beq loc_C08D48 */
      if(t2) goto loc_C08D48;
    } }
  /* loc_C08D45 */
  S(0x8D45, 3); t_write16(ss, ss_abs(ss, 0x0C1B), a);  /* C08D45 sta $0C1B */

loc_C08D48:
  S(0x8D48, 2);                                        /* C08D48 lda camera_y */
  a = t_read16(ss, (uint16_t) (dp + camera_y)); ss_set_nz16(ss, a);
  SI(0x8D4A); ss_set_c(ss, true);                      /* C08D4A sec */
  SIMM16(0x8D4B); a = alu_sbc16(ss, a, 0x0076);        /* C08D4B sbc #$0076 */
  SI(0x8D4E); ss_set_c(ss, false);                     /* C08D4E clc */
  S(0x8D4F, 4);                                        /* C08D4F adc data_C46888,X */
  a = alu_adc16(ss, a, t_read16(ss, (0xC46888u + x) & 0xffffff));
  { const bool taken = (a & 0x8000) == 0;
    S(0x8D53, 1); t_branch(ss, taken);                 /* C08D53 bpl loc_C08D5F */
    if(taken) goto loc_C08D5F; }
  SIMM16(0x8D55); alu_cmp16(ss, a, 0xFFFF);            /* C08D55 cmp #$FFFF */
  { const bool taken = a == 0xFFFF;
    S(0x8D58, 1); t_branch(ss, taken);                 /* C08D58 beq loc_C08D67 */
    if(taken) goto loc_C08D67; }
  SIMM16(0x8D5A); a = 0xFFFF; ss_set_nz16(ss, a);      /* C08D5A lda #$FFFF */
  S(0x8D5D, 1); t_branch(ss, true);                    /* C08D5D bra loc_C08D67 */
  goto loc_C08D67;

loc_C08D5F:
  SIMM16(0x8D5F); alu_cmp16(ss, a, 0x00C2);            /* C08D5F cmp #$00C2 */
  { const bool taken = !ss_c(ss);
    S(0x8D62, 1); t_branch(ss, taken);                 /* C08D62 bcc loc_C08D67 */
    if(!taken) { SIMM16(0x8D64); a = 0x00C1; ss_set_nz16(ss, a); } }  /* lda #$00C1 */

loc_C08D67:
  SI(0x8D67); y = a; ss_set_nz16(ss, y);               /* C08D67 tay */
  SI(0x8D68); a = alu_inc16(ss, a);                    /* C08D68 inc A */
  S(0x8D69, 2); t_write16(ss, (uint16_t) (dp + scratch_18), a);  /* C08D69 sta $18 */
  S(0x8D6B, 4);                                        /* C08D6B lda data_C46B88,X */
  a = t_read16(ss, (0xC46B88u + x) & 0xffffff); ss_set_nz16(ss, a);
  S(0x8D6F, 3); t_write16(ss, ss_abs(ss, 0x0C0A), a);  /* C08D6F sta $0C0A */
  SIMM16(0x8D72); a = 0x0012; ss_set_nz16(ss, a);      /* C08D72 lda #$0012 */
  S(0x8D75, 3); t_write16(ss, ss_abs(ss, 0x0C08), a);  /* C08D75 sta $0C08 */
  S(0x8D78, 2);                                        /* C08D78 lda camera_y */
  a = t_read16(ss, (uint16_t) (dp + camera_y)); ss_set_nz16(ss, a);
  SI(0x8D7A); ss_set_c(ss, true);                      /* C08D7A sec */
  SIMM16(0x8D7B); a = alu_sbc16(ss, a, 0x007F);        /* C08D7B sbc #$007F */
  SI(0x8D7E); ss_set_c(ss, false);                     /* C08D7E clc */
  S(0x8D7F, 4);                                        /* C08D7F adc data_C46808,X */
  a = alu_adc16(ss, a, t_read16(ss, (0xC46808u + x) & 0xffffff));
  S(0x8D83, 3); t_write16(ss, ss_abs(ss, 0x0BF6), a);  /* C08D83 sta $0BF6 */
  S(0x8D86, 4);                                        /* C08D86 lda data_C46908,X */
  a = t_read16(ss, (0xC46908u + x) & 0xffffff); ss_set_nz16(ss, a);
  SI(0x8D8A); a = alu_asl16(ss, a);                    /* C08D8A asl A */
  S(0x8D8B, 3);                                        /* C08D8B bit $0C11 */
  alu_bit16(ss, a, t_read16(ss, ss_abs(ss, 0x0C11)));
  { const bool taken = ss_n(ss);
    S(0x8D8E, 1); t_branch(ss, taken);                 /* C08D8E bmi loc_C08D94 */
    if(!taken) {
      SIMM16(0x8D90); a = alu_eor16(ss, a, 0xFFFF);    /* C08D90 eor #$FFFF */
      SI(0x8D93); a = alu_inc16(ss, a);                /* C08D93 inc A */
    } }
  /* loc_C08D94 */
  S(0x8D94, 2); t_write16(ss, (uint16_t) (dp + scratch_0C), a);  /* C08D94 sta $0C */
  SI(0x8D96); ss_set_c(ss, false);                     /* C08D96 clc */
  S(0x8D97, 2);                                        /* C08D97 adc ptr_04 */
  a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + ptr_04)));
  S(0x8D99, 2); t_write16(ss, (uint16_t) (dp + ptr_04), a);  /* C08D99 sta ptr_04 */
  SIMM16(0x8D9B); x = 0x003E; ss_set_nz16(ss, x);      /* C08D9B ldx #$003E */
  SIMM16(0x8D9E); a = 0x007F; ss_set_nz16(ss, a);      /* C08D9E lda #$007F */
  SI(0x8DA1); ss_set_c(ss, true);                      /* C08DA1 sec */
  S(0x8DA2, 2);                                        /* C08DA2 sbc $18 */
  a = alu_sbc16(ss, a, t_read16(ss, (uint16_t) (dp + scratch_18)));
  { const bool taken = a == 0;
    S(0x8DA4, 1); t_branch(ss, taken);                 /* C08DA4 beq loc_C08DAB */
    if(taken) goto loc_C08DAB; }
  { const bool taken = (a & 0x8000) == 0;
    S(0x8DA6, 1); t_branch(ss, taken);                 /* C08DA6 bpl loc_C08DAE */
    if(taken) goto loc_C08DAE; }
  SIMM16(0x8DA8); x = 0x003C; ss_set_nz16(ss, x);      /* C08DA8 ldx #$003C */

loc_C08DAB:
  SIMM16(0x8DAB); a = 0x0001; ss_set_nz16(ss, a);      /* C08DAB lda #$0001 */

loc_C08DAE:
  SIMM16(0x8DAE); alu_cmp16(ss, a, 0x0080);            /* C08DAE cmp #$0080 */
  { const bool taken = !ss_c(ss);
    S(0x8DB1, 1); t_branch(ss, taken);                 /* C08DB1 bcc loc_C08DB6 */
    if(!taken) { SIMM16(0x8DB3); a = 0x007F; ss_set_nz16(ss, a); } }  /* lda #$007F */
  /* loc_C08DB6 */
  S(0x8DB6, 1); a = t_xba(ss, a);                        /* C08DB6 xba */
  S(0x8DB7, 3); t_write16(ss, ss_abs(ss, 0x0C00), a);  /* C08DB7 sta $0C00 */
  S(0x8DBA, 2); t_write16(ss, (uint16_t) (dp + scratch_1A), x);  /* C08DBA stx $1A */
  S(0x8DBC, 4);                                        /* C08DBC lda $7F00D3 */
  a = t_read16(ss, 0x7F00D3); ss_set_nz16(ss, a);
  SIMM16(0x8DC0); a = alu_and16(ss, a, 0xFF00);        /* C08DC0 and #$FF00 */
  S(0x8DC3, 2);                                        /* C08DC3 ora $1A */
  a = alu_ora16(ss, a, t_read16(ss, (uint16_t) (dp + scratch_1A)));
  S(0x8DC5, 3); t_write16(ss, ss_abs(ss, 0x0C02), a);  /* C08DC5 sta $0C02 */
  SI(0x8DC8); a = y; ss_set_nz16(ss, a);               /* C08DC8 tya */
  SI(0x8DC9); a = alu_inc16(ss, a);                    /* C08DC9 inc A */
  SI(0x8DCA); a = alu_asl16(ss, a);                    /* C08DCA asl A */
  SIMM16(0x8DCB); a = alu_adc16(ss, a, 0x0C31);        /* C08DCB adc #$0C31 */
  S(0x8DCE, 3); t_write16(ss, ss_abs(ss, 0x0C06), a);  /* C08DCE sta $0C06 */
  S(0x8DD1, 3);                                        /* C08DD1 lda $0C11 */
  a = t_read16(ss, ss_abs(ss, 0x0C11)); ss_set_nz16(ss, a);
  { const bool taken = (a & 0x8000) != 0;
    S(0x8DD4, 1); t_branch(ss, taken);                 /* C08DD4 bmi loc_C08DDE */
    if(!taken) {
      S(0x8DD6, 3);                                    /* C08DD6 lda $0C0C */
      a = t_read16(ss, ss_abs(ss, 0x0C0C)); ss_set_nz16(ss, a);
      SIMM16(0x8DD9); alu_cmp16(ss, a, 0x3000);        /* C08DD9 cmp #$3000 */
      const bool t2 = !ss_c(ss);
      S(0x8DDC, 1); t_branch(ss, t2);                  /* C08DDC bcc loc_C08DE8 */
      if(t2) goto loc_C08DE8;
    } }
  /* loc_C08DDE */
  S(0x8DDE, 3);                                        /* C08DDE lda $0C13 */
  a = t_read16(ss, ss_abs(ss, 0x0C13)); ss_set_nz16(ss, a);
  SI(0x8DE1); ss_set_c(ss, false);                     /* C08DE1 clc */
  SIMM16(0x8DE2); a = alu_adc16(ss, a, 0x0200);        /* C08DE2 adc #$0200 */
  S(0x8DE5, 3); t_write16(ss, ss_abs(ss, 0x0C13), a);  /* C08DE5 sta $0C13 */

loc_C08DE8:
  S(0x8DE8, 3);                                        /* C08DE8 lda $0C14 */
  a = t_read16(ss, ss_abs(ss, 0x0C14)); ss_set_nz16(ss, a);
  SIMM16(0x8DEB); a = alu_and16(ss, a, 0x00FF);        /* C08DEB and #$00FF */
  SI(0x8DEE); a = alu_asl16(ss, a);                    /* C08DEE asl A */
  SI(0x8DEF); x = a; ss_set_nz16(ss, x);               /* C08DEF tax */
  S(0x8DF0, 4);                                        /* C08DF0 lda data_C46988,X */
  a = t_read16(ss, (0xC46988u + x) & 0xffffff); ss_set_nz16(ss, a);
  S(0x8DF4, 1); a = t_xba(ss, a);                        /* C08DF4 xba */
  SI(0x8DF5); ss_set_c(ss, false);                     /* C08DF5 clc */
  SI(0x8DF6); a = alu_rol16(ss, a);                    /* C08DF6 rol A */
  SIMM16(0x8DF7); a = alu_adc16(ss, a, 0x0000);        /* C08DF7 adc #$0000 */
  S(0x8DFA, 3); t_write16(ss, ss_abs(ss, 0x0C2F), a);  /* C08DFA sta $0C2F */
  S(0x8DFD, 2);                                        /* C08DFD lda ptr_04 */
  a = t_read16(ss, (uint16_t) (dp + ptr_04)); ss_set_nz16(ss, a);
  SIMM16(0x8DFF); a = alu_and16(ss, a, 0x01FF);        /* C08DFF and #$01FF */
  SIMM16(0x8E02); x = 0x0186; ss_set_nz16(ss, x);      /* C08E02 ldx #$0186 */

  for(;;) {                                            /* loc_C08E05 */
    S(0x8E05, 3); t_index(ss);                         /* C08E05 sta $0DB9,X */
    t_write16(ss, ss_abs(ss, (uint16_t) (0x0DB9 + x)), a);
    SI(0x8E08); x = (uint16_t) (x - 1); ss_set_nz16(ss, x);   /* C08E08 dex */
    SI(0x8E09); x = (uint16_t) (x - 1); ss_set_nz16(ss, x);   /* C08E09 dex */
    SIMM16(0x8E0A); alu_cpx16(ss, x, 0x00DE);          /* C08E0A cpx #$00DE */
    { const bool taken = x != 0x00DE;
      S(0x8E0D, 1); t_branch(ss, taken);               /* C08E0D bne loc_C08E05 */
      if(!taken) break; }
  }
  SI(0x8E0F); ss_set_c(ss, false);                     /* C08E0F clc */
  for(;;) {                                            /* loc_C08E10 */
    S(0x8E10, 3); t_index(ss);                         /* C08E10 sta $0DB9,X */
    t_write16(ss, ss_abs(ss, (uint16_t) (0x0DB9 + x)), a);
    S(0x8E13, 3);                                      /* C08E13 adc $0C2F */
    a = alu_adc16(ss, a, t_read16(ss, ss_abs(ss, 0x0C2F)));
    SIMM16(0x8E16); a = alu_adc16(ss, a, 0x0000);      /* C08E16 adc #$0000 */
    SI(0x8E19); x = (uint16_t) (x - 1); ss_set_nz16(ss, x);   /* C08E19 dex */
    SI(0x8E1A); x = (uint16_t) (x - 1); ss_set_nz16(ss, x);   /* C08E1A dex */
    { const bool taken = (x & 0x8000) == 0;
      S(0x8E1B, 1); t_branch(ss, taken);               /* C08E1B bpl loc_C08E10 */
      if(!taken) break; }
  }
  SIMM16(0x8E1D); a = 0xFF00; ss_set_nz16(ss, a);      /* C08E1D lda #$FF00 */
  S(0x8E20, 3); t_write16(ss, ss_abs(ss, 0x0C04), a);  /* C08E20 sta $0C04 */
  S(0x8E23, 2);                                        /* C08E23 ldx ptr_04 */
  x = t_read16(ss, (uint16_t) (dp + ptr_04)); ss_set_nz16(ss, x);
  SIMM16(0x8E25); a = 0x6C69; ss_set_nz16(ss, a);      /* C08E25 lda #$6C69 */
  SJMP(0x8E28); goto loc_C08E7F;                       /* C08E28 jmp loc_C08E7F */

loc_C08E2B:
  S(0x8E2B, 2);                                        /* C08E2B lda camera_y */
  a = t_read16(ss, (uint16_t) (dp + camera_y)); ss_set_nz16(ss, a);
  SI(0x8E2D); a = alu_lsr16(ss, a);                    /* C08E2D lsr A */
  SI(0x8E2E); ss_set_c(ss, false);                     /* C08E2E clc */
  S(0x8E2F, 2);                                        /* C08E2F adc camera_y */
  a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + camera_y)));
  SI(0x8E31); ss_set_c(ss, true);                      /* C08E31 sec */
  SIMM16(0x8E32); a = alu_sbc16(ss, a, 0x0048);        /* C08E32 sbc #$0048 */
  { const bool taken = (a & 0x8000) != 0;
    S(0x8E35, 1); t_branch(ss, taken);                 /* C08E35 bmi loc_C08E4A */
    if(taken) goto loc_C08E4A; }
  S(0x8E37, 1); t_branch(ss, true);                    /* C08E37 bra loc_C08E45 */
  goto loc_C08E45;

loc_C08E39:
  S(0x8E39, 2);                                        /* C08E39 lda camera_y */
  a = t_read16(ss, (uint16_t) (dp + camera_y)); ss_set_nz16(ss, a);
  SI(0x8E3B); a = alu_lsr16(ss, a);                    /* C08E3B lsr A */
  SI(0x8E3C); ss_set_c(ss, false);                     /* C08E3C clc */
  S(0x8E3D, 2);                                        /* C08E3D adc camera_y */
  a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + camera_y)));
  SI(0x8E3F); ss_set_c(ss, true);                      /* C08E3F sec */
  SIMM16(0x8E40); a = alu_sbc16(ss, a, 0x0048);        /* C08E40 sbc #$0048 */
  { const bool taken = (a & 0x8000) != 0;
    S(0x8E43, 1); t_branch(ss, taken);                 /* C08E43 bmi loc_C08E4A */
    if(taken) goto loc_C08E4A; }

loc_C08E45:
  SIMM16(0x8E45); alu_cmp16(ss, a, 0x00A0);            /* C08E45 cmp #$00A0 */
  { const bool taken = ss_c(ss);
    S(0x8E48, 1); t_branch(ss, taken);                 /* C08E48 bcs loc_C08E4D */
    if(taken) goto loc_C08E4D; }

loc_C08E4A:
  SIMM16(0x8E4A); a = 0x00A0; ss_set_nz16(ss, a);      /* C08E4A lda #$00A0 */

loc_C08E4D:
  SI(0x8E4D); y = a; ss_set_nz16(ss, y);               /* C08E4D tay */
  SIMM16(0x8E4E); a = 0x1016; ss_set_nz16(ss, a);      /* C08E4E lda #$1016 */
  S(0x8E51, 3); t_write16(ss, ss_abs(ss, 0x0BD8), a);  /* C08E51 sta $0BD8 */
  SIMM16(0x8E54); a = 0x1017; ss_set_nz16(ss, a);      /* C08E54 lda #$1017 */
  S(0x8E57, 3); t_write16(ss, ss_abs(ss, 0x0BDA), a);  /* C08E57 sta $0BDA */
  S(0x8E5A, 2);                                        /* C08E5A lda $5E */
  a = t_read16(ss, (uint16_t) (dp + frame_counter)); ss_set_nz16(ss, a);
  SIMM16(0x8E5C); a = alu_and16(ss, a, 0x00FF);        /* C08E5C and #$00FF */
  SI(0x8E5F); a = alu_asl16(ss, a);                    /* C08E5F asl A */
  SI(0x8E60); x = a; ss_set_nz16(ss, x);               /* C08E60 tax */
  S(0x8E61, 4);                                        /* C08E61 lda data_C46588,X */
  a = t_read16(ss, (0xC46588u + x) & 0xffffff); ss_set_nz16(ss, a);
  SI(0x8E65); a = alu_lsr16(ss, a);                    /* C08E65 lsr A */
  SI(0x8E66); a = alu_lsr16(ss, a);                    /* C08E66 lsr A */
  SI(0x8E67); a = alu_lsr16(ss, a);                    /* C08E67 lsr A */
  SI(0x8E68); a = alu_lsr16(ss, a);                    /* C08E68 lsr A */
  SI(0x8E69); a = alu_lsr16(ss, a);                    /* C08E69 lsr A */
  SI(0x8E6A); a = alu_lsr16(ss, a);                    /* C08E6A lsr A */
  S(0x8E6B, 2); t_write16(ss, (uint16_t) (dp + ptr_04), a);   /* C08E6B sta ptr_04 */
  SI(0x8E6D); a = alu_lsr16(ss, a);                    /* C08E6D lsr A */
  S(0x8E6E, 2); t_write16(ss, (uint16_t) (dp + 0x0006), a);   /* C08E6E sta $06 */
  SI(0x8E70); a = y; ss_set_nz16(ss, a);               /* C08E70 tya */
  SI(0x8E71); ss_set_c(ss, false);                     /* C08E71 clc */
  S(0x8E72, 2);                                        /* C08E72 adc $06 */
  a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + 0x0006)));
  SI(0x8E74); y = a; ss_set_nz16(ss, y);               /* C08E74 tay */
  S(0x8E75, 2);                                        /* C08E75 lda camera_x */
  a = t_read16(ss, (uint16_t) (dp + camera_x)); ss_set_nz16(ss, a);
  SI(0x8E77); a = alu_asl16(ss, a);                    /* C08E77 asl A */
  SI(0x8E78); ss_set_c(ss, false);                     /* C08E78 clc */
  S(0x8E79, 2);                                        /* C08E79 adc ptr_04 */
  a = alu_adc16(ss, a, t_read16(ss, (uint16_t) (dp + ptr_04)));
  SI(0x8E7B); x = a; ss_set_nz16(ss, x);               /* C08E7B tax */
  SIMM16(0x8E7C); a = 0x7171; ss_set_nz16(ss, a);      /* C08E7C lda #$7171 */

loc_C08E7F:
  S(0x8E7F, 3); t_write16(ss, ss_abs(ss, 0x0BFC), a);  /* C08E7F sta $0BFC */
  S(0x8E82, 3); t_write16(ss, ss_abs(ss, 0x0BE4), x);  /* C08E82 stx $0BE4 */
  S(0x8E85, 3); t_write16(ss, ss_abs(ss, 0x0BE6), y);  /* C08E85 sty $0BE6 */
  SIMM16(0x8E88); a = 0x0000; ss_set_nz16(ss, a);      /* C08E88 lda #$0000 */
  JSR(0x8E8B, 0x9C62);                                 /* jsr vram_stream_descriptor_dispatch */
  /* falls through into check_pending_player_attack ($8E8E), converted in
   * recomp/src/entities.c: the callee's rts already left the pc there, so
   * returning lets the registry dispatch it. */
}

static void ppu_regs_default(SnesState* ss)      { ppu_regs_default_at(ss, 0x8BDB); }
static void ppu_regs_default_8BE4(SnesState* ss) { ppu_regs_default_at(ss, 0x8BE4); }
static void ppu_regs_default_8E2B(SnesState* ss) { ppu_regs_default_at(ss, 0x8E2B); }
static void ppu_regs_default_8E39(SnesState* ss) { ppu_regs_default_at(ss, 0x8E39); }
static void ppu_regs_default_8E7F(SnesState* ss) { ppu_regs_default_at(ss, 0x8E7F); }

/* ---------------------------------------------------------------------------
 * nmi_handler_title_fade — $C0:BD20
 *
 * The title screen's own NMI handler, installed by the init at $BD1D. It runs
 * the whole title sequence out of a small state machine in $0F43, one step per
 * frame: fade the 8bpp palette up ($0F44/$0F45 stepping INIDISP), hold, swap
 * the BG1 tilemap base between $64/$68/$6C, fade back down, and count the two
 * long holds in $0F50 and $0F58. Two of its states are colour walks over the
 * 512-byte CGRAM image in $7F0F91: $BEC0 brightens every component of every
 * colour towards white one step a frame and stops when all 256 are $7FFF,
 * $BF57 walks them back down towards the real palette at $C6:A36B and stops
 * when nothing differs. $7F1195 is the phase those two share, $7F1193/$7F1194
 * the "still moving" accumulators.
 *
 * Frame one of every pass DMAs $7F0F91 into CGRAM on channel 2 and re-arms
 * whatever channels the frame left pending in $02. Start (bit $1000 of the
 * joypad words) leaves through $BEA0, which blanks the screen and restarts the
 * game at loc_C08042; every other path ends at $C008 and parks.
 *
 * The m and x flags move constantly here -- the routine spends most of its
 * time 8-bit and drops into 16-bit for the two-byte counters -- so the body
 * narrows its own X and Y locals wherever a sep touches the index width, as
 * the CPU does.
 * ------------------------------------------------------------------------- */
static void nmi_handler_title_fade(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  const uint16_t dp = ss_dp(ss);

  REP(0xBD20, 0x10);                                   /* C0BD20 rep #$10 */
  SIMM16(0xBD22); x = 0x01FF; ss_set_nz16(ss, x);      /* C0BD22 ldx #$01FF */
  SI(0xBD25); ss_set_sp(ss, x);                        /* C0BD25 txs */
  SIMM16(0xBD26); x = 0x0000; ss_set_nz16(ss, x);      /* C0BD26 ldx #$0000 */
  S(0xBD29, 3); t_write16(ss, ss_abs(ss, OAMADDL), x); /* C0BD29 stx OAMADDL */
  SEP(0xBD2C, 0x10);                                   /* C0BD2C sep #$10 */
  x = (uint16_t) (x & 0xff); y = (uint16_t) (y & 0xff);
  REP(0xBD2E, 0x20);                                   /* C0BD2E rep #$20 */
  SIMM16(0xBD30); a = 0x2200; ss_set_nz16(ss, a);      /* C0BD30 lda #$2200 */
  S(0xBD33, 3); t_write16(ss, ss_abs(ss, DMAP2), a);   /* C0BD33 sta DMAP2 */
  SIMM16(0xBD36); a = 0x0F91; ss_set_nz16(ss, a);      /* C0BD36 lda #$0F91 */
  S(0xBD39, 3); t_write16(ss, ss_abs(ss, A1TL2), a);   /* C0BD39 sta A1TL2 */
  SEP(0xBD3C, 0x20);                                   /* C0BD3C sep #$20 */
  S(0xBD3E, 3); t_write8(ss, ss_abs(ss, CGADD), 0);    /* C0BD3E stz CGADD */
  SIMM8(0xBD41); a = (uint16_t) ((a & 0xff00) | 0x7F); ss_set_nz8(ss, 0x7F);
  S(0xBD43, 3); t_write8(ss, ss_abs(ss, A1B2), 0x7F);  /* C0BD43 sta A1B2 */
  S(0xBD46, 3); t_write8(ss, ss_abs(ss, DASL2), 0);    /* C0BD46 stz DASL2 */
  SIMM8(0xBD49); a = (uint16_t) ((a & 0xff00) | 0x02); ss_set_nz8(ss, 0x02);
  S(0xBD4B, 3); t_write8(ss, ss_abs(ss, DASH2), 0x02); /* C0BD4B sta DASH2 */
  SIMM8(0xBD4E); a = (uint16_t) ((a & 0xff00) | 0x04); ss_set_nz8(ss, 0x04);
  S(0xBD50, 2);                                        /* C0BD50 ora dma_pending_mask */
  a = alu_ora8(ss, a, t_read8(ss, (uint16_t) (dp + dma_pending_mask)));
  S(0xBD52, 3); t_write8(ss, ss_abs(ss, MDMAEN), (uint8_t) a);  /* sta MDMAEN */
  S(0xBD55, 3);                                        /* C0BD55 lda $0B8A */
  { uint8_t b = t_read8(ss, ss_abs(ss, 0x0B8A));
    a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b); }
  { const bool taken = (a & 0xff) == 0;
    S(0xBD58, 1); t_branch(ss, taken);                 /* C0BD58 beq loc_C0BD67 */
    if(!taken) {
      S(0xBD5A, 3);                                    /* C0BD5A cmp $0B8B */
      alu_cmp8(ss, a, t_read8(ss, ss_abs(ss, 0x0B8B)));
      const bool t2 = ss_z(ss);
      S(0xBD5D, 1); t_branch(ss, t2);                  /* C0BD5D beq loc_C0BD67 */
      if(!t2) {
        S(0xBD5F, 3); t_write8(ss, ss_abs(ss, 0x0B8B), (uint8_t) a);  /* sta $0B8B */
        SIMM8(0xBD62); a = (uint16_t) ((a & 0xff00) | 0x11); ss_set_nz8(ss, 0x11);
        S(0xBD64, 3); t_write8(ss, ss_abs(ss, TM), 0x11);             /* sta TM */
      }
    } }

  /* loc_C0BD67 */
  SEP(0xBD67, 0x20);                                   /* C0BD67 sep #$20 */
  S(0xBD69, 3); t_write8(ss, ss_abs(ss, NMITIMEN), 0); /* C0BD69 stz NMITIMEN */
  REP(0xBD6C, 0x30);                                   /* C0BD6C rep #$30 */
  JSR(0xBD6E, 0xA2DE);                                 /* jsr read_joypads */
  S(0xBD71, 2);                                        /* C0BD71 lda joy1_held */
  a = t_read16(ss, (uint16_t) (dp + joy1_held)); ss_set_nz16(ss, a);
  S(0xBD73, 2);                                        /* C0BD73 ora joy2_held */
  a = alu_ora16(ss, a, t_read16(ss, (uint16_t) (dp + joy2_held)));
  SIMM16(0xBD75); a = alu_and16(ss, a, 0x1000);        /* C0BD75 and #$1000 */
  { const bool taken = a == 0;
    S(0xBD78, 1); t_branch(ss, taken);                 /* C0BD78 beq loc_C0BD7D */
    if(!taken) { SJMP(0xBD7A); goto loc_C0BEA0; } }    /* C0BD7A jmp loc_C0BEA0 */

  /* loc_C0BD7D */
  SEP(0xBD7D, 0x30);                                   /* C0BD7D sep #$30 */
  x = (uint16_t) (x & 0xff); y = (uint16_t) (y & 0xff);
  S(0xBD7F, 3);                                        /* C0BD7F lda $0F43 */
  { uint8_t b = t_read8(ss, ss_abs(ss, 0x0F43));
    a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b); }
  { const bool taken = (a & 0xff) != 0;
    S(0xBD82, 1); t_branch(ss, taken);                 /* C0BD82 bne loc_C0BD92 */
    if(taken) goto loc_C0BD92; }
  S(0xBD84, 3);                                        /* C0BD84 lda $0F4D */
  { uint8_t b = t_read8(ss, ss_abs(ss, 0x0F4D));
    a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b); }
  SI(0xBD87); a = alu_inc8(ss, a);                     /* C0BD87 inc A */
  S(0xBD88, 3); t_write8(ss, ss_abs(ss, 0x0F4D), (uint8_t) a);  /* sta $0F4D */
  SIMM8(0xBD8B); alu_cmp8(ss, a, 0xD7);                /* C0BD8B cmp #$D7 */
  { const bool taken = ss_z(ss);
    S(0xBD8D, 1); t_branch(ss, taken);                 /* C0BD8D beq loc_C0BDB0 */
    if(taken) goto loc_C0BDB0; }
  SJMP(0xBD8F); goto loc_C0C008;                       /* C0BD8F jmp loc_C0C008 */

loc_C0BD92:
  SI(0xBD92); a = alu_dec8(ss, a);                     /* C0BD92 dec A */
  { const bool taken = (a & 0xff) != 0;
    S(0xBD93, 1); t_branch(ss, taken);                 /* C0BD93 bne loc_C0BDBC */
    if(taken) goto loc_C0BDBC; }

loc_C0BD95:
  REP(0xBD95, 0x20);                                   /* C0BD95 rep #$20 */
  S(0xBD97, 3);                                        /* C0BD97 lda $0F44 */
  a = t_read16(ss, ss_abs(ss, 0x0F44)); ss_set_nz16(ss, a);
  SI(0xBD9A); ss_set_c(ss, false);                     /* C0BD9A clc */
  SIMM16(0xBD9B); a = alu_adc16(ss, a, 0x0080);        /* C0BD9B adc #$0080 */
  S(0xBD9E, 3); t_write16(ss, ss_abs(ss, 0x0F44), a);  /* C0BD9E sta $0F44 */
  SEP(0xBDA1, 0x20);                                   /* C0BDA1 sep #$20 */
  S(0xBDA3, 3);                                        /* C0BDA3 lda $0F45 */
  { uint8_t b = t_read8(ss, ss_abs(ss, 0x0F45));
    a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b); }
  S(0xBDA6, 3); t_write8(ss, ss_abs(ss, INIDISP_), (uint8_t) a);  /* sta INIDISP */
  SIMM8(0xBDA9); alu_cmp8(ss, a, 0x0F);                /* C0BDA9 cmp #$0F */
  { const bool taken = ss_z(ss);
    S(0xBDAB, 1); t_branch(ss, taken);                 /* C0BDAB beq loc_C0BDB0 */
    if(taken) goto loc_C0BDB0; }
  SJMP(0xBDAD); goto loc_C0C008;                       /* C0BDAD jmp loc_C0C008 */

loc_C0BDB0:
  SEP(0xBDB0, 0x20);                                   /* C0BDB0 sep #$20 */
  S(0xBDB2, 3);                                        /* C0BDB2 lda $0F43 */
  { uint8_t b = t_read8(ss, ss_abs(ss, 0x0F43));
    a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b); }
  SI(0xBDB5); a = alu_inc8(ss, a);                     /* C0BDB5 inc A */
  S(0xBDB6, 3); t_write8(ss, ss_abs(ss, 0x0F43), (uint8_t) a);  /* sta $0F43 */
  SJMP(0xBDB9); goto loc_C0C008;                       /* C0BDB9 jmp loc_C0C008 */

loc_C0BDBC:
  SI(0xBDBC); a = alu_dec8(ss, a);                     /* C0BDBC dec A */
  { const bool taken = (a & 0xff) != 0;
    S(0xBDBD, 1); t_branch(ss, taken);                 /* C0BDBD bne loc_C0BDD3 */
    if(taken) goto loc_C0BDD3; }

loc_C0BDBF:
  S(0xBDBF, 3);                                        /* C0BDBF lda $0F45 */
  { uint8_t b = t_read8(ss, ss_abs(ss, 0x0F45));
    a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b); }
  S(0xBDC2, 3); t_write8(ss, ss_abs(ss, INIDISP_), (uint8_t) a);  /* sta INIDISP */
  S(0xBDC5, 3);                                        /* C0BDC5 lda $0F46 */
  { uint8_t b = t_read8(ss, ss_abs(ss, 0x0F46));
    a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b); }
  SI(0xBDC8); a = alu_inc8(ss, a);                     /* C0BDC8 inc A */
  S(0xBDC9, 3); t_write8(ss, ss_abs(ss, 0x0F46), (uint8_t) a);  /* sta $0F46 */
  SIMM8(0xBDCC); alu_cmp8(ss, a, 0xC0);                /* C0BDCC cmp #$C0 */
  { const bool taken = ss_z(ss);
    S(0xBDCE, 1); t_branch(ss, taken);                 /* C0BDCE beq loc_C0BDB0 */
    if(taken) goto loc_C0BDB0; }
  SJMP(0xBDD0); goto loc_C0C008;                       /* C0BDD0 jmp loc_C0C008 */

loc_C0BDD3:
  SI(0xBDD3); a = alu_dec8(ss, a);                     /* C0BDD3 dec A */
  { const bool taken = (a & 0xff) != 0;
    S(0xBDD4, 1); t_branch(ss, taken);                 /* C0BDD4 bne loc_C0BDF1 */
    if(taken) goto loc_C0BDF1; }

loc_C0BDD6:
  REP(0xBDD6, 0x20);                                   /* C0BDD6 rep #$20 */
  S(0xBDD8, 3);                                        /* C0BDD8 lda $0F44 */
  a = t_read16(ss, ss_abs(ss, 0x0F44)); ss_set_nz16(ss, a);
  SI(0xBDDB); ss_set_c(ss, true);                      /* C0BDDB sec */
  SIMM16(0xBDDC); a = alu_sbc16(ss, a, 0x0080);        /* C0BDDC sbc #$0080 */
  S(0xBDDF, 3); t_write16(ss, ss_abs(ss, 0x0F44), a);  /* C0BDDF sta $0F44 */
  SEP(0xBDE2, 0x20);                                   /* C0BDE2 sep #$20 */
  S(0xBDE4, 3);                                        /* C0BDE4 lda $0F45 */
  { uint8_t b = t_read8(ss, ss_abs(ss, 0x0F45));
    a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b); }
  S(0xBDE7, 3); t_write8(ss, ss_abs(ss, INIDISP_), (uint8_t) a);  /* sta INIDISP */
  SIMM8(0xBDEA); alu_cmp8(ss, a, 0x00);                /* C0BDEA cmp #$00 */
  { const bool taken = ss_z(ss);
    S(0xBDEC, 1); t_branch(ss, taken);                 /* C0BDEC beq loc_C0BDB0 */
    if(taken) goto loc_C0BDB0; }
  SJMP(0xBDEE); goto loc_C0C008;                       /* C0BDEE jmp loc_C0C008 */

loc_C0BDF1:
  SI(0xBDF1); a = alu_dec8(ss, a);                     /* C0BDF1 dec A */
  { const bool taken = (a & 0xff) != 0;
    S(0xBDF2, 1); t_branch(ss, taken);                 /* C0BDF2 bne loc_C0BE07 */
    if(taken) goto loc_C0BE07; }
  REP(0xBDF4, 0x20);                                   /* C0BDF4 rep #$20 */
  S(0xBDF6, 3);                                        /* C0BDF6 lda $0F50 */
  a = t_read16(ss, ss_abs(ss, 0x0F50)); ss_set_nz16(ss, a);
  SI(0xBDF9); a = alu_inc16(ss, a);                    /* C0BDF9 inc A */
  S(0xBDFA, 3); t_write16(ss, ss_abs(ss, 0x0F50), a);  /* C0BDFA sta $0F50 */
  SIMM16(0xBDFD); alu_cmp16(ss, a, 0x0055);            /* C0BDFD cmp #$0055 */
  { const bool taken = ss_z(ss);
    S(0xBE00, 1); t_branch(ss, taken);                 /* C0BE00 beq loc_C0BDB0 */
    if(taken) goto loc_C0BDB0; }
  SEP(0xBE02, 0x20);                                   /* C0BE02 sep #$20 */
  SJMP(0xBE04); goto loc_C0C008;                       /* C0BE04 jmp loc_C0C008 */

loc_C0BE07:
  SI(0xBE07); a = alu_dec8(ss, a);                     /* C0BE07 dec A */
  { const bool taken = (a & 0xff) != 0;
    S(0xBE08, 1); t_branch(ss, taken);                 /* C0BE08 bne loc_C0BE14 */
    if(taken) goto loc_C0BE14; }
  SIMM8(0xBE0A); a = (uint16_t) ((a & 0xff00) | 0x6C); ss_set_nz8(ss, 0x6C);
  S(0xBE0C, 3); t_write8(ss, ss_abs(ss, BG1SC), 0x6C); /* C0BE0C sta BG1SC */
  S(0xBE0F, 3); t_write8(ss, ss_abs(ss, 0x0F46), 0);   /* C0BE0F stz $0F46 */
  S(0xBE12, 1); t_branch(ss, true);                    /* C0BE12 bra loc_C0BD95 */
  goto loc_C0BD95;

loc_C0BE14:
  SI(0xBE14); a = alu_dec8(ss, a);                     /* C0BE14 dec A */
  { const bool taken = (a & 0xff) == 0;
    S(0xBE15, 1); t_branch(ss, taken);                 /* C0BE15 beq loc_C0BDBF */
    if(taken) goto loc_C0BDBF; }
  SI(0xBE17); a = alu_dec8(ss, a);                     /* C0BE17 dec A */
  { const bool taken = (a & 0xff) == 0;
    S(0xBE18, 1); t_branch(ss, taken);                 /* C0BE18 beq loc_C0BDD6 */
    if(taken) goto loc_C0BDD6; }
  SI(0xBE1A); a = alu_dec8(ss, a);                     /* C0BE1A dec A */
  { const bool taken = (a & 0xff) != 0;
    S(0xBE1B, 1); t_branch(ss, taken);                 /* C0BE1B bne loc_C0BE2B */
    if(taken) goto loc_C0BE2B; }
  S(0xBE1D, 3);                                        /* C0BE1D lda $0F54 */
  { uint8_t b = t_read8(ss, ss_abs(ss, 0x0F54));
    a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b); }
  SI(0xBE20); a = alu_inc8(ss, a);                     /* C0BE20 inc A */
  S(0xBE21, 3); t_write8(ss, ss_abs(ss, 0x0F54), (uint8_t) a);  /* sta $0F54 */
  SIMM8(0xBE24); alu_cmp8(ss, a, 0x28);                /* C0BE24 cmp #$28 */
  { const bool taken = ss_z(ss);
    S(0xBE26, 1); t_branch(ss, taken);                 /* C0BE26 beq loc_C0BDB0 */
    if(taken) goto loc_C0BDB0; }
  SJMP(0xBE28); goto loc_C0C008;                       /* C0BE28 jmp loc_C0C008 */

loc_C0BE2B:
  SI(0xBE2B); a = alu_dec8(ss, a);                     /* C0BE2B dec A */
  { const bool taken = (a & 0xff) != 0;
    S(0xBE2C, 1); t_branch(ss, taken);                 /* C0BE2C bne loc_C0BE39 */
    if(taken) goto loc_C0BE39; }
  SIMM8(0xBE2E); a = (uint16_t) ((a & 0xff00) | 0x68); ss_set_nz8(ss, 0x68);
  S(0xBE30, 3); t_write8(ss, ss_abs(ss, BG1SC), 0x68); /* C0BE30 sta BG1SC */
  S(0xBE33, 3); t_write8(ss, ss_abs(ss, 0x0F46), 0);   /* C0BE33 stz $0F46 */
  SJMP(0xBE36); goto loc_C0BD95;                       /* C0BE36 jmp loc_C0BD95 */

loc_C0BE39:
  SI(0xBE39); a = alu_dec8(ss, a);                     /* C0BE39 dec A */
  { const bool taken = (a & 0xff) != 0;
    S(0xBE3A, 1); t_branch(ss, taken);                 /* C0BE3A bne loc_C0BE58 */
    if(taken) goto loc_C0BE58; }
  S(0xBE3C, 3);                                        /* C0BE3C lda $0F45 */
  { uint8_t b = t_read8(ss, ss_abs(ss, 0x0F45));
    a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b); }
  S(0xBE3F, 3); t_write8(ss, ss_abs(ss, INIDISP_), (uint8_t) a);  /* sta INIDISP */
  REP(0xBE42, 0x20);                                   /* C0BE42 rep #$20 */
  S(0xBE44, 3);                                        /* C0BE44 lda $0F58 */
  a = t_read16(ss, ss_abs(ss, 0x0F58)); ss_set_nz16(ss, a);
  SI(0xBE47); a = alu_inc16(ss, a);                    /* C0BE47 inc A */
  S(0xBE48, 3); t_write16(ss, ss_abs(ss, 0x0F58), a);  /* C0BE48 sta $0F58 */
  SIMM16(0xBE4B); alu_cmp16(ss, a, 0x0564);            /* C0BE4B cmp #$0564 */
  SEP(0xBE4E, 0x20);                                   /* C0BE4E sep #$20 */
  { const bool taken = ss_z(ss);
    S(0xBE50, 1); t_branch(ss, taken);                 /* C0BE50 beq loc_C0BE55 */
    if(!taken) { SJMP(0xBE52); goto loc_C0C008; } }    /* C0BE52 jmp loc_C0C008 */
  /* loc_C0BE55 */
  SJMP(0xBE55); goto loc_C0BDB0;                       /* C0BE55 jmp loc_C0BDB0 */

loc_C0BE58:
  S(0xBE58, 3); t_write8(ss, ss_abs(ss, 0x0F4B), (uint8_t) a);  /* C0BE58 sta $0F4B */
  SEP(0xBE5B, 0x20);                                   /* C0BE5B sep #$20 */
  S(0xBE5D, 3);                                        /* C0BE5D lda $0F45 */
  { uint8_t b = t_read8(ss, ss_abs(ss, 0x0F45));
    a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b); }
  S(0xBE60, 3); t_write8(ss, ss_abs(ss, INIDISP_), (uint8_t) a);  /* sta INIDISP */
  S(0xBE63, 4);                                        /* C0BE63 lda $7F1195 */
  { uint8_t b = t_read8(ss, 0x7F1195);
    a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b); }
  { const bool taken = (a & 0xff) == 0;
    S(0xBE67, 1); t_branch(ss, taken);                 /* C0BE67 beq loc_C0BEC0 */
    if(taken) goto loc_C0BEC0; }
  SIMM8(0xBE69); alu_cmp8(ss, a, 0x01);                /* C0BE69 cmp #$01 */
  { const bool taken = !ss_z(ss);
    S(0xBE6B, 1); t_branch(ss, taken);                 /* C0BE6B bne loc_C0BE70 */
    if(!taken) { SJMP(0xBE6D); goto loc_C0BF57; } }    /* C0BE6D jmp loc_C0BF57 */

  /* loc_C0BE70 */
  SEP(0xBE70, 0x20);                                   /* C0BE70 sep #$20 */
  SIMM8(0xBE72); a = (uint16_t) ((a & 0xff00) | 0x01); ss_set_nz8(ss, 0x01);
  S(0xBE74, 3); t_write8(ss, ss_abs(ss, 0x0B8A), 0x01);/* C0BE74 sta $0B8A */
  REP(0xBE77, 0x20);                                   /* C0BE77 rep #$20 */
  S(0xBE79, 3);                                        /* C0BE79 lda $0F55 */
  a = t_read16(ss, ss_abs(ss, 0x0F55)); ss_set_nz16(ss, a);
  SIMM16(0xBE7C); alu_cmp16(ss, a, 0x0F00);            /* C0BE7C cmp #$0F00 */
  { const bool taken = ss_z(ss);
    S(0xBE7F, 1); t_branch(ss, taken);                 /* C0BE7F beq loc_C0BE9B */
    if(taken) goto loc_C0BE9B; }
  SI(0xBE81); a = alu_inc16(ss, a);                    /* C0BE81 inc A */
  S(0xBE82, 3); t_write16(ss, ss_abs(ss, 0x0F55), a);  /* C0BE82 sta $0F55 */
  SIMM16(0xBE85); alu_cmp16(ss, a, 0x0F00);            /* C0BE85 cmp #$0F00 */
  { const bool taken = !ss_z(ss);
    S(0xBE88, 1); t_branch(ss, taken);                 /* C0BE88 bne loc_C0BE91 */
    if(!taken) {
      SEP(0xBE8A, 0x20);                               /* C0BE8A sep #$20 */
      SIMM8(0xBE8C); a = (uint16_t) ((a & 0xff00) | 0x0C); ss_set_nz8(ss, 0x0C);
      S(0xBE8E, 3); t_write8(ss, ss_abs(ss, 0x0F43), 0x0C);  /* sta $0F43 */
    } }
  /* loc_C0BE91 */
  REP(0xBE91, 0x30);                                   /* C0BE91 rep #$30 */
  JSR(0xBE93, 0x92F3);                                 /* jsr mode1_reset_particles_and_oam */
  SEP(0xBE96, 0x20);                                   /* C0BE96 sep #$20 */
  SJMP(0xBE98); goto loc_C0C008;                       /* C0BE98 jmp loc_C0C008 */

loc_C0BE9B:
  SEP(0xBE9B, 0x20);                                   /* C0BE9B sep #$20 */
  S(0xBE9D, 3);                                        /* C0BE9D lda $0F4B */
  { uint8_t b = t_read8(ss, ss_abs(ss, 0x0F4B));
    a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b); }

loc_C0BEA0:
  SEP(0xBEA0, 0x20);                                   /* C0BEA0 sep #$20 */
  SIMM8(0xBEA2); a = (uint16_t) ((a & 0xff00) | 0x01); ss_set_nz8(ss, 0x01);
  S(0xBEA4, 3); t_write8(ss, ss_abs(ss, NMITIMEN), 0x01);   /* sta NMITIMEN */
  SIMM8(0xBEA7); a = (uint16_t) ((a & 0xff00) | 0x80); ss_set_nz8(ss, 0x80);
  S(0xBEA9, 3); t_write8(ss, ss_abs(ss, INIDISP_), 0x80);   /* sta INIDISP */
  REP(0xBEAC, 0x30);                                   /* C0BEAC rep #$30 */
  S(0xBEAE, 2); t_write16(ss, (uint16_t) (dp + joy1_pressed), 0);   /* stz $8C */
  S(0xBEB0, 2); t_write16(ss, (uint16_t) (dp + joy2_pressed), 0);   /* stz $90 */
  S(0xBEB2, 2); t_write16(ss, (uint16_t) (dp + dma_pending_mask), 0);/* stz $02 */
  SJMP(0xBEB4);                                        /* C0BEB4 jmp loc_C08042 */
  TAIL(pb, 0x8042);

loc_C0BEC0:
  SEP(0xBEC0, 0x20);                                   /* C0BEC0 sep #$20 */
  SIMM8(0xBEC2); a = (uint16_t) ((a & 0xff00) | 0x68); ss_set_nz8(ss, 0x68);
  S(0xBEC4, 3); t_write8(ss, ss_abs(ss, BG1SC), 0x68); /* C0BEC4 sta BG1SC */
  REP(0xBEC7, 0x30);                                   /* C0BEC7 rep #$30 */
  SEP(0xBEC9, 0x20);                                   /* C0BEC9 sep #$20 */
  SIMM8(0xBECB); a = (uint16_t) ((a & 0xff00) | 0xFF); ss_set_nz8(ss, 0xFF);
  S(0xBECD, 4); t_write8(ss, 0x7F1193, 0xFF);          /* C0BECD sta $7F1193 */
  SIMM8(0xBED1); a = (uint16_t) ((a & 0xff00) | 0x7F); ss_set_nz8(ss, 0x7F);
  S(0xBED3, 4); t_write8(ss, 0x7F1194, 0x7F);          /* C0BED3 sta $7F1194 */
  SIMM16(0xBED7); x = 0x0000; ss_set_nz16(ss, x);      /* C0BED7 ldx #$0000 */

  for(;;) {                                            /* loc_C0BEDA */
    S(0xBEDA, 4);                                      /* C0BEDA lda $7F0F91,X */
    { uint8_t b = t_read8(ss, (0x7F0F91u + x) & 0xffffff);
      a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b); }
    SIMM8(0xBEDE); a = alu_and8(ss, a, 0x1F);          /* C0BEDE and #$1F */
    SIMM8(0xBEE0); alu_cmp8(ss, a, 0x1F);              /* C0BEE0 cmp #$1F */
    { const bool taken = ss_z(ss);
      S(0xBEE2, 1); t_branch(ss, taken);               /* C0BEE2 beq loc_C0BEE5 */
      if(!taken) { SI(0xBEE4); a = alu_inc8(ss, a); } }/* C0BEE4 inc A */
    SIMM8(0xBEE5); alu_cmp8(ss, a, 0x1F);              /* C0BEE5 cmp #$1F */
    { const bool taken = ss_z(ss);
      S(0xBEE7, 1); t_branch(ss, taken);               /* C0BEE7 beq loc_C0BEEA */
      if(!taken) { SI(0xBEE9); a = alu_inc8(ss, a); } }/* C0BEE9 inc A */
    S(0xBEEA, 4); t_write8(ss, 0x7F1191, (uint8_t) a); /* C0BEEA sta $7F1191 */
    S(0xBEEE, 4);                                      /* C0BEEE lda $7F0F92,X */
    { uint8_t b = t_read8(ss, (0x7F0F92u + x) & 0xffffff);
      a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b); }
    SIMM8(0xBEF2); a = alu_and8(ss, a, 0x7C);          /* C0BEF2 and #$7C */
    SIMM8(0xBEF4); alu_cmp8(ss, a, 0x7C);              /* C0BEF4 cmp #$7C */
    { const bool taken = ss_z(ss);
      S(0xBEF6, 1); t_branch(ss, taken);               /* C0BEF6 beq loc_C0BEFB */
      if(!taken) {
        SI(0xBEF8); ss_set_c(ss, false);               /* C0BEF8 clc */
        SIMM8(0xBEF9); a = alu_adc8(ss, a, 0x04);      /* C0BEF9 adc #$04 */
      } }
    SIMM8(0xBEFB); alu_cmp8(ss, a, 0x7C);              /* C0BEFB cmp #$7C */
    { const bool taken = ss_z(ss);
      S(0xBEFD, 1); t_branch(ss, taken);               /* C0BEFD beq loc_C0BF02 */
      if(!taken) {
        SI(0xBEFF); ss_set_c(ss, false);               /* C0BEFF clc */
        SIMM8(0xBF00); a = alu_adc8(ss, a, 0x04);      /* C0BF00 adc #$04 */
      } }
    S(0xBF02, 4); t_write8(ss, 0x7F1192, (uint8_t) a); /* C0BF02 sta $7F1192 */
    REP(0xBF06, 0x20);                                 /* C0BF06 rep #$20 */
    S(0xBF08, 4);                                      /* C0BF08 lda $7F0F91,X */
    a = t_read16(ss, (0x7F0F91u + x) & 0xffffff); ss_set_nz16(ss, a);
    SIMM16(0xBF0C); a = alu_and16(ss, a, 0x03E0);      /* C0BF0C and #$03E0 */
    SIMM16(0xBF0F); alu_cmp16(ss, a, 0x03E0);          /* C0BF0F cmp #$03E0 */
    { const bool taken = ss_z(ss);
      S(0xBF12, 1); t_branch(ss, taken);               /* C0BF12 beq loc_C0BF18 */
      if(!taken) {
        SI(0xBF14); ss_set_c(ss, false);               /* C0BF14 clc */
        SIMM16(0xBF15); a = alu_adc16(ss, a, 0x0020);  /* C0BF15 adc #$0020 */
      } }
    SIMM16(0xBF18); alu_cmp16(ss, a, 0x03E0);          /* C0BF18 cmp #$03E0 */
    { const bool taken = ss_z(ss);
      S(0xBF1B, 1); t_branch(ss, taken);               /* C0BF1B beq loc_C0BF21 */
      if(!taken) {
        SI(0xBF1D); ss_set_c(ss, false);               /* C0BF1D clc */
        SIMM16(0xBF1E); a = alu_adc16(ss, a, 0x0020);  /* C0BF1E adc #$0020 */
      } }
    S(0xBF21, 4);                                      /* C0BF21 ora $7F1191 */
    a = alu_ora16(ss, a, t_read16(ss, 0x7F1191));
    S(0xBF25, 4); t_write16(ss, (0x7F0F91u + x) & 0xffffff, a);  /* sta $7F0F91,X */
    S(0xBF29, 4);                                      /* C0BF29 and $7F1193 */
    a = alu_and16(ss, a, t_read16(ss, 0x7F1193));
    S(0xBF2D, 4); t_write16(ss, 0x7F1193, a);          /* C0BF2D sta $7F1193 */
    SEP(0xBF31, 0x20);                                 /* C0BF31 sep #$20 */
    SI(0xBF33); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);   /* C0BF33 inx */
    SI(0xBF34); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);   /* C0BF34 inx */
    SIMM16(0xBF35); alu_cpx16(ss, x, 0x0200);          /* C0BF35 cpx #$0200 */
    { const bool taken = x != 0x0200;
      S(0xBF38, 1); t_branch(ss, taken);               /* C0BF38 bne loc_C0BEDA */
      if(!taken) break; }
  }
  REP(0xBF3A, 0x20);                                   /* C0BF3A rep #$20 */
  S(0xBF3C, 4);                                        /* C0BF3C lda $7F1193 */
  a = t_read16(ss, 0x7F1193); ss_set_nz16(ss, a);
  SIMM16(0xBF40); alu_cmp16(ss, a, 0x7FFF);            /* C0BF40 cmp #$7FFF */
  { const bool taken = !ss_z(ss);
    S(0xBF43, 1); t_branch(ss, taken);                 /* C0BF43 bne loc_C0BF52 */
    if(!taken) {
      SEP(0xBF45, 0x20);                               /* C0BF45 sep #$20 */
      SIMM8(0xBF47); a = (uint16_t) ((a & 0xff00) | 0x64); ss_set_nz8(ss, 0x64);
      S(0xBF49, 3); t_write8(ss, ss_abs(ss, BG1SC), 0x64);   /* sta BG1SC */
      SIMM8(0xBF4C); a = (uint16_t) ((a & 0xff00) | 0x01); ss_set_nz8(ss, 0x01);
      S(0xBF4E, 4); t_write8(ss, 0x7F1195, 0x01);      /* sta $7F1195 */
    } }
  /* loc_C0BF52 */
  SEP(0xBF52, 0x30);                                   /* C0BF52 sep #$30 */
  x = (uint16_t) (x & 0xff); y = (uint16_t) (y & 0xff);
  SJMP(0xBF54); goto loc_C0C008;                       /* C0BF54 jmp loc_C0C008 */

loc_C0BF57:
  REP(0xBF57, 0x10);                                   /* C0BF57 rep #$10 */
  SIMM16(0xBF59); x = 0x0000; ss_set_nz16(ss, x);      /* C0BF59 ldx #$0000 */
  SIMM8(0xBF5C); a = (uint16_t) ((a & 0xff00) | 0x00); ss_set_nz8(ss, 0x00);
  S(0xBF5E, 4); t_write8(ss, 0x7F1193, 0x00);          /* C0BF5E sta $7F1193 */
  S(0xBF62, 4); t_write8(ss, 0x7F1194, 0x00);          /* C0BF62 sta $7F1194 */

  for(;;) {                                            /* loc_C0BF66 */
    S(0xBF66, 4);                                      /* C0BF66 lda data_C6A36B,X */
    { uint8_t b = t_read8(ss, (0xC6A36Bu + x) & 0xffffff);
      a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b); }
    SIMM8(0xBF6A); a = alu_and8(ss, a, 0x1F);          /* C0BF6A and #$1F */
    S(0xBF6C, 4); t_write8(ss, 0x7F1196, (uint8_t) a); /* C0BF6C sta $7F1196 */
    S(0xBF70, 4);                                      /* C0BF70 lda $7F0F91,X */
    { uint8_t b = t_read8(ss, (0x7F0F91u + x) & 0xffffff);
      a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b); }
    SIMM8(0xBF74); a = alu_and8(ss, a, 0x1F);          /* C0BF74 and #$1F */
    S(0xBF76, 4);                                      /* C0BF76 cmp $7F1196 */
    alu_cmp8(ss, a, t_read8(ss, 0x7F1196));
    { const bool taken = ss_z(ss);
      S(0xBF7A, 1); t_branch(ss, taken);               /* C0BF7A beq loc_C0BF7D */
      if(!taken) { SI(0xBF7C); a = alu_dec8(ss, a); } }/* C0BF7C dec A */
    S(0xBF7D, 4);                                      /* C0BF7D cmp $7F1196 */
    alu_cmp8(ss, a, t_read8(ss, 0x7F1196));
    { const bool taken = ss_z(ss);
      S(0xBF81, 1); t_branch(ss, taken);               /* C0BF81 beq loc_C0BF84 */
      if(!taken) { SI(0xBF83); a = alu_dec8(ss, a); } }/* C0BF83 dec A */
    S(0xBF84, 4); t_write8(ss, 0x7F1191, (uint8_t) a); /* C0BF84 sta $7F1191 */
    S(0xBF88, 4);                                      /* C0BF88 lda data_C6A36C,X */
    { uint8_t b = t_read8(ss, (0xC6A36Cu + x) & 0xffffff);
      a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b); }
    SIMM8(0xBF8C); a = alu_and8(ss, a, 0x7C);          /* C0BF8C and #$7C */
    S(0xBF8E, 4); t_write8(ss, 0x7F1196, (uint8_t) a); /* C0BF8E sta $7F1196 */
    S(0xBF92, 4);                                      /* C0BF92 lda $7F0F92,X */
    { uint8_t b = t_read8(ss, (0x7F0F92u + x) & 0xffffff);
      a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b); }
    SIMM8(0xBF96); a = alu_and8(ss, a, 0x7C);          /* C0BF96 and #$7C */
    S(0xBF98, 4);                                      /* C0BF98 cmp $7F1196 */
    alu_cmp8(ss, a, t_read8(ss, 0x7F1196));
    { const bool taken = ss_z(ss);
      S(0xBF9C, 1); t_branch(ss, taken);               /* C0BF9C beq loc_C0BFA1 */
      if(!taken) {
        SI(0xBF9E); ss_set_c(ss, true);                /* C0BF9E sec */
        SIMM8(0xBF9F); a = alu_sbc8(ss, a, 0x04);      /* C0BF9F sbc #$04 */
      } }
    S(0xBFA1, 4);                                      /* C0BFA1 cmp $7F1196 */
    alu_cmp8(ss, a, t_read8(ss, 0x7F1196));
    { const bool taken = ss_z(ss);
      S(0xBFA5, 1); t_branch(ss, taken);               /* C0BFA5 beq loc_C0BFAA */
      if(!taken) {
        SI(0xBFA7); ss_set_c(ss, true);                /* C0BFA7 sec */
        SIMM8(0xBFA8); a = alu_sbc8(ss, a, 0x04);      /* C0BFA8 sbc #$04 */
      } }
    S(0xBFAA, 4); t_write8(ss, 0x7F1192, (uint8_t) a); /* C0BFAA sta $7F1192 */
    REP(0xBFAE, 0x20);                                 /* C0BFAE rep #$20 */
    S(0xBFB0, 4);                                      /* C0BFB0 lda data_C6A36B,X */
    a = t_read16(ss, (0xC6A36Bu + x) & 0xffffff); ss_set_nz16(ss, a);
    SIMM16(0xBFB4); a = alu_and16(ss, a, 0x03E0);      /* C0BFB4 and #$03E0 */
    S(0xBFB7, 4); t_write16(ss, 0x7F1196, a);          /* C0BFB7 sta $7F1196 */
    S(0xBFBB, 4);                                      /* C0BFBB lda $7F0F91,X */
    a = t_read16(ss, (0x7F0F91u + x) & 0xffffff); ss_set_nz16(ss, a);
    SIMM16(0xBFBF); a = alu_and16(ss, a, 0x03E0);      /* C0BFBF and #$03E0 */
    S(0xBFC2, 4);                                      /* C0BFC2 cmp $7F1196 */
    alu_cmp16(ss, a, t_read16(ss, 0x7F1196));
    { const bool taken = ss_z(ss);
      S(0xBFC6, 1); t_branch(ss, taken);               /* C0BFC6 beq loc_C0BFCC */
      if(!taken) {
        SI(0xBFC8); ss_set_c(ss, true);                /* C0BFC8 sec */
        SIMM16(0xBFC9); a = alu_sbc16(ss, a, 0x0020);  /* C0BFC9 sbc #$0020 */
      } }
    S(0xBFCC, 4);                                      /* C0BFCC cmp $7F1196 */
    alu_cmp16(ss, a, t_read16(ss, 0x7F1196));
    { const bool taken = ss_z(ss);
      S(0xBFD0, 1); t_branch(ss, taken);               /* C0BFD0 beq loc_C0BFD6 */
      if(!taken) {
        SI(0xBFD2); ss_set_c(ss, true);                /* C0BFD2 sec */
        SIMM16(0xBFD3); a = alu_sbc16(ss, a, 0x0020);  /* C0BFD3 sbc #$0020 */
      } }
    S(0xBFD6, 4);                                      /* C0BFD6 ora $7F1191 */
    a = alu_ora16(ss, a, t_read16(ss, 0x7F1191));
    S(0xBFDA, 4); t_write16(ss, (0x7F0F91u + x) & 0xffffff, a);  /* sta $7F0F91,X */
    S(0xBFDE, 4);                                      /* C0BFDE eor data_C6A36B,X */
    a = alu_eor16(ss, a, t_read16(ss, (0xC6A36Bu + x) & 0xffffff));
    S(0xBFE2, 4);                                      /* C0BFE2 ora $7F1193 */
    a = alu_ora16(ss, a, t_read16(ss, 0x7F1193));
    S(0xBFE6, 4); t_write16(ss, 0x7F1193, a);          /* C0BFE6 sta $7F1193 */
    SEP(0xBFEA, 0x20);                                 /* C0BFEA sep #$20 */
    SI(0xBFEC); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);   /* C0BFEC inx */
    SI(0xBFED); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);   /* C0BFED inx */
    SIMM16(0xBFEE); alu_cpx16(ss, x, 0x0200);          /* C0BFEE cpx #$0200 */
    { const bool taken = x == 0x0200;
      S(0xBFF1, 1); t_branch(ss, taken);               /* C0BFF1 beq loc_C0BFF6 */
      if(taken) break; }
    SJMP(0xBFF3);                                      /* C0BFF3 jmp loc_C0BF66 */
  }

  /* loc_C0BFF6 */
  SEP(0xBFF6, 0x10);                                   /* C0BFF6 sep #$10 */
  x = (uint16_t) (x & 0xff); y = (uint16_t) (y & 0xff);
  S(0xBFF8, 4);                                        /* C0BFF8 lda $7F1193 */
  { uint8_t b = t_read8(ss, 0x7F1193);
    a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b); }
  S(0xBFFC, 4);                                        /* C0BFFC ora $7F1194 */
  a = alu_ora8(ss, a, t_read8(ss, 0x7F1194));
  { const bool taken = (a & 0xff) != 0;
    S(0xC000, 1); t_branch(ss, taken);                 /* C0C000 bne loc_C0C008 */
    if(!taken) {
      SIMM8(0xC002); a = (uint16_t) ((a & 0xff00) | 0x02); ss_set_nz8(ss, 0x02);
      S(0xC004, 4); t_write8(ss, 0x7F1195, 0x02);      /* C0C004 sta $7F1195 */
    } }

loc_C0C008:
  REP(0xC008, 0x30);                                   /* C0C008 rep #$30 */
  SJMP(0xC00A);                                        /* C0C00A jmp loc_C0A4F5 */
  TAIL(pb, 0xA4F5);
}

/* ---------------------------------------------------------------------------
 * reset — $C0:8000, and the loc_C08042 / loc_C0805E entries
 *
 * Power-on. Native mode, interrupts off, direct page and data bank zero, stack
 * at $01FF, then both 64 KB WRAM banks written to zero a word at a time, the
 * SPC700 brought up (spc_init uploads the loader and the driver, spc_command
 * #$0001 starts the title music), the screen forced blank, ppu_init, and the
 * two magic words the rest of the game seeds its RNG from. It leaves through
 * `jmp loc_C0BB81`, the title screen's init, which installs
 * nmi_handler_title_fade and parks.
 *
 * loc_C08042 is the restart: nmi_handler_title_fade jumps here when Start is
 * pressed, and it redoes the direct page / data bank / stack setup without the
 * WRAM clear, then falls into loc_C0805E.
 *
 * loc_C0805E is the per-mode entry, reached again from nmi_handler_gameplay
 * ($8226) every time the mode advances: tell the SPC which music to play, run
 * ppu_init, dispatch the mode's init through game_mode_table, reset the render
 * order and spawn the mode's entities, find the first entity whose type is in
 * $0E..$11 and park its slot index in $0BB4, arm NMI and auto-joypad in the
 * NMITIMEN shadow, clear the frame's scratch and install nmi_handler_gameplay.
 *
 * `clc ; xce` is handed back to the ROM (see the file header): the first entry
 * models the `clc` and points the pc at the `xce`, and the second picks the
 * routine up at $8002.
 * ------------------------------------------------------------------------- */

/* sei / cld: only the one flag moves, but ss_set_p is the only way to reach I
 * and D and it re-applies the emulation-mode and index-width masking on the
 * way, so the index registers go back afterwards. */
static void t_set_flag(SnesState* ss, uint8_t bit, bool on) {
  const uint16_t xs = ss_x(ss), ys = ss_y(ss);
  const uint8_t p = ss_p(ss);
  ss_set_p(ss, on ? (uint8_t) (p | bit) : (uint8_t) (p & (uint8_t) ~bit));
  ss_set_x(ss, xs); ss_set_y(ss, ys);
}

static void reset(SnesState* ss) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);

  SI(0x8000); ss_set_c(ss, false);          /* C08000 clc */
  /* C08001 xce -- handed to the ROM: no entry point in snes_state.h clears the
   * emulation flag, and the routine cannot go on until it is clear. The next
   * registered entry is $8002, so exactly this one instruction runs as 65816
   * code and the hook takes the routine straight back. */
  TAIL(pb, 0x8001);
}

/* The body picks the routine up at $8002, and again at the head of the WRAM
 * clear. That second entry is what keeps the rest of the routine from being
 * the ROM's: the clear is 32768 iterations of about 130 master cycles, some
 * fifteen frames' worth, so the first vblank always lands inside it and the
 * body hands it back. Without an entry at the loop head the hook could never
 * be offered the routine again -- $8002 is only ever reached once -- and
 * everything after the loop (spc_init, spc_command, ppu_init, the two seed
 * words) would run as 65816 code for the life of the program. With it, the
 * ROM's own `bne` puts the pc back on $8012 one iteration later and the C
 * body carries on. */
static void reset_native_at(SnesState* ss, uint16_t entry) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  uint16_t dp = ss_dp(ss);

  if(entry == 0x8012) goto loc_C08012;

  SI(0x8002); t_set_flag(ss, 0x04, true);   /* C08002 sei */
  REP(0x8003, 0x30);                        /* C08003 rep #$30 */
  SI(0x8005); t_set_flag(ss, 0x08, false);  /* C08005 cld */
  SIMM16(0x8006); a = 0x0000; ss_set_nz16(ss, a);      /* C08006 lda #$0000 */
  SI(0x8009); ss_set_dp(ss, a); dp = a; ss_set_nz16(ss, a);   /* C08009 tcd */
  S(0x800A, 1);                             /* C0800A phk */
  ss_idle(ss); ss_check_int(ss); ss_push8(ss, pb);
  S(0x800B, 1);                             /* C0800B plb */
  ss_idle(ss); ss_idle(ss); ss_check_int(ss);
  { const uint8_t b = ss_pull8(ss); ss_set_db(ss, b); ss_set_nz8(ss, b); }
  SIMM16(0x800C); x = 0x01FF; ss_set_nz16(ss, x);      /* C0800C ldx #$01FF */
  SI(0x800F); ss_set_sp(ss, x);                        /* C0800F txs */
  SI(0x8010); a = ss_dp(ss); ss_set_nz16(ss, a);       /* C08010 tdc */
  SI(0x8011); x = a; ss_set_nz16(ss, x);               /* C08011 tax */

loc_C08012:
  for(;;) {
    S(0x8012, 4); t_write16(ss, (0x7E0000u + x) & 0xffffff, a);  /* sta $7E0000,X */
    S(0x8016, 4); t_write16(ss, (0x7F0000u + x) & 0xffffff, a);  /* sta $7F0000,X */
    SI(0x801A); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);      /* C0801A inx */
    SI(0x801B); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);      /* C0801B inx */
    { const bool taken = x != 0;
      S(0x801C, 1); t_branch(ss, taken);               /* C0801C bne loc_C08012 */
      if(!taken) break; }
  }

  JSL(0x801E, 0x81, 0x8000);                           /* jsl spc_init */
  SIMM16(0x8022); a = 0x0001; ss_set_nz16(ss, a);      /* C08022 lda #$0001 */
  JSL(0x8025, 0x81, 0x83CE);                           /* jsl spc_command */
  SEP(0x8029, 0x20);                                   /* C08029 sep #$20 */
  SIMM8(0x802B); a = (uint16_t) ((a & 0xff00) | 0x80); ss_set_nz8(ss, 0x80);
  S(0x802D, 3); t_write8(ss, ss_abs(ss, INIDISP_), 0x80);   /* sta INIDISP */
  REP(0x8030, 0x20);                                   /* C08030 rep #$20 */
  JSR(0x8032, 0xA385);                                 /* jsr ppu_init */
  SIMM16(0x8035); a = 0xAA55; ss_set_nz16(ss, a);      /* C08035 lda #$AA55 */
  S(0x8038, 2); t_write16(ss, (uint16_t) (dp + init_magic_AA55), a);
  SIMM16(0x803A); a = 0xFFFF; ss_set_nz16(ss, a);      /* C0803A lda #$FFFF */
  S(0x803D, 2); t_write16(ss, (uint16_t) (dp + init_magic_FFFF), a);
  SJMP(0x803F);                                        /* C0803F jmp loc_C0BB81 */
  TAIL(pb, 0xBB81);
}

static void reset_mode_restart(SnesState* ss, uint16_t entry) {
  const uint8_t pb = ss_pb(ss);
  uint16_t a = ss_a(ss), x = ss_x(ss), y = ss_y(ss);
  uint16_t dp = ss_dp(ss);

  if(entry == 0x805E) goto loc_C0805E;

  REP(0x8042, 0x30);                                   /* C08042 rep #$30 */
  SI(0x8044); t_set_flag(ss, 0x08, false);             /* C08044 cld */
  SIMM16(0x8045); a = 0x0000; ss_set_nz16(ss, a);      /* C08045 lda #$0000 */
  SI(0x8048); ss_set_dp(ss, a); dp = a; ss_set_nz16(ss, a);   /* C08048 tcd */
  S(0x8049, 1);                                        /* C08049 phk */
  ss_idle(ss); ss_check_int(ss); ss_push8(ss, pb);
  S(0x804A, 1);                                        /* C0804A plb */
  ss_idle(ss); ss_idle(ss); ss_check_int(ss);
  { const uint8_t b = ss_pull8(ss); ss_set_db(ss, b); ss_set_nz8(ss, b); }
  SIMM16(0x804B); x = 0x01FF; ss_set_nz16(ss, x);      /* C0804B ldx #$01FF */
  SI(0x804E); ss_set_sp(ss, x);                        /* C0804E txs */
  SIMM16(0x804F); a = 0x0000; ss_set_nz16(ss, a);      /* C0804F lda #$0000 */
  S(0x8052, 2); t_write16(ss, (uint16_t) (dp + game_mode), a);   /* sta game_mode */
  SIMM16(0x8054); a = 0xAA55; ss_set_nz16(ss, a);      /* C08054 lda #$AA55 */
  S(0x8057, 2); t_write16(ss, (uint16_t) (dp + init_magic_AA55), a);
  SIMM16(0x8059); a = 0xFFFF; ss_set_nz16(ss, a);      /* C08059 lda #$FFFF */
  S(0x805C, 2); t_write16(ss, (uint16_t) (dp + init_magic_FFFF), a);

loc_C0805E:
  S(0x805E, 2);                                        /* C0805E lda game_mode */
  a = t_read16(ss, (uint16_t) (dp + game_mode)); ss_set_nz16(ss, a);
  { const bool taken = a == 0;
    S(0x8060, 1); t_branch(ss, taken);                 /* C08060 beq loc_C08065 */
    if(!taken) { SIMM16(0x8062); a = 0x0002; ss_set_nz16(ss, a); } }  /* lda #$0002 */
  /* loc_C08065 */
  JSL(0x8065, 0x81, 0x83CE);                           /* jsl spc_command */
  JSR(0x8069, 0xA385);                                 /* jsr ppu_init */
  S(0x806C, 2);                                        /* C0806C lda game_mode */
  a = t_read16(ss, (uint16_t) (dp + game_mode)); ss_set_nz16(ss, a);
  SI(0x806E); a = alu_asl16(ss, a);                    /* C0806E asl A */
  SI(0x806F); x = a; ss_set_nz16(ss, x);               /* C0806F tax */
  JSR_IAX(0x8070, 0x826A);                             /* jsr (game_mode_table,X) */
  JSR(0x8073, 0xAEB9);                                 /* jsr entity_render_order_reset */
  JSR(0x8076, 0x9D6A);                                 /* jsr entity_init_from_table */
  SIMM16(0x8079); x = 0x0004; ss_set_nz16(ss, x);      /* C08079 ldx #$0004 */

  for(;;) {                                            /* loc_C0807C */
    S(0x807C, 3); t_index(ss);                         /* C0807C lda entity_type,X */
    a = t_read16(ss, ss_abs(ss, (uint16_t) (entity_type + x))); ss_set_nz16(ss, a);
    SIMM16(0x807F); alu_cmp16(ss, a, 0x000E);          /* C0807F cmp #$000E */
    bool below = !ss_c(ss);
    S(0x8082, 1); t_branch(ss, below);                 /* C08082 bcc loc_C08089 */
    if(!below) {
      SIMM16(0x8084); alu_cmp16(ss, a, 0x0012);        /* C08084 cmp #$0012 */
      const bool t2 = !ss_c(ss);
      S(0x8087, 1); t_branch(ss, t2);                  /* C08087 bcc loc_C08092 */
      if(t2) goto loc_C08092;
    }
    /* loc_C08089 */
    SI(0x8089); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);   /* C08089 inx */
    SI(0x808A); x = (uint16_t) (x + 1); ss_set_nz16(ss, x);   /* C0808A inx */
    S(0x808B, 2);                                      /* C0808B cpx $A6 */
    alu_cpx16(ss, x, t_read16(ss, (uint16_t) (dp + 0x00A6)));
    { const bool taken = !ss_c(ss);
      S(0x808D, 1); t_branch(ss, taken);               /* C0808D bcc loc_C0807C */
      if(!taken) break; }
  }
  SIMM16(0x808F); x = 0x0000; ss_set_nz16(ss, x);      /* C0808F ldx #$0000 */

loc_C08092:
  S(0x8092, 3); t_write16(ss, ss_abs(ss, player_attack_flag), x);  /* stx $0BB4 */
  SIMM16(0x8095); a = 0x0002; ss_set_nz16(ss, a);      /* C08095 lda #$0002 */
  S(0x8098, 3); t_write16(ss, ss_abs(ss, 0x0BB6), a);  /* C08098 sta $0BB6 */
  SEP(0x809B, 0x20);                                   /* C0809B sep #$20 */
  S(0x809D, 3);                                        /* C0809D lda TIMEUP */
  { uint8_t b = t_read8(ss, ss_abs(ss, TIMEUP));
    a = (uint16_t) ((a & 0xff00) | b); ss_set_nz8(ss, b); }
  SIMM8(0x80A0); a = (uint16_t) ((a & 0xff00) | 0x81); ss_set_nz8(ss, 0x81);
  S(0x80A2, 2); t_write8(ss, (uint16_t) (dp + nmitimen_shadow), 0x81);
  SIMM8(0x80A4); a = (uint16_t) ((a & 0xff00) | 0x80); ss_set_nz8(ss, 0x80);
  S(0x80A6, 3); t_write8(ss, ss_abs(ss, OAMADDH), 0x80);    /* sta OAMADDH */
  SIMM8(0x80A9); a = (uint16_t) ((a & 0xff00) | 0x01); ss_set_nz8(ss, 0x01);
  S(0x80AB, 3); t_write8(ss, ss_abs(ss, MEMSEL), 0x01);     /* sta MEMSEL */
  REP(0x80AE, 0x20);                                   /* C080AE rep #$20 */
  S(0x80B0, 2); t_write16(ss, (uint16_t) (dp + fade_level), 0);   /* C080B0 stz $30 */
  SIMM16(0x80B2); a = 0x0080; ss_set_nz16(ss, a);      /* C080B2 lda #$0080 */
  S(0x80B5, 2); t_write16(ss, (uint16_t) (dp + fade_delta), a);   /* sta $32 */
  JSR(0x80B7, 0xADFD);                                 /* jsr oam_dma_upload */
  SIMM16(0x80BA); a = 0xFFFF; ss_set_nz16(ss, a);      /* C080BA lda #$FFFF */
  S(0x80BD, 3); t_write16(ss, ss_abs(ss, 0x0BC6), a);  /* C080BD sta $0BC6 */
  S(0x80C0, 3); t_write16(ss, ss_abs(ss, 0x0BC4), a);  /* C080C0 sta $0BC4 */
  S(0x80C3, 3); t_write16(ss, ss_abs(ss, 0x0BC8), 0);  /* C080C3 stz $0BC8 */
  S(0x80C6, 3); t_write16(ss, ss_abs(ss, 0x0BCA), 0);  /* C080C6 stz $0BCA */
  S(0x80C9, 3); t_write16(ss, ss_abs(ss, 0x0A88), 0);  /* C080C9 stz $0A88 */
  S(0x80CC, 3); t_write16(ss, ss_abs(ss, cgram_queue_index), 0);  /* stz $0B8A */
  S(0x80CF, 3); t_write16(ss, ss_abs(ss, 0x0BB8), 0);  /* C080CF stz $0BB8 */
  S(0x80D2, 3); t_write16(ss, ss_abs(ss, 0x0BBA), 0);  /* C080D2 stz $0BBA */
  S(0x80D5, 2); t_write16(ss, (uint16_t) (dp + 0x0070), 0);  /* stz walk_cycle_timer */
  S(0x80D7, 2); t_write16(ss, (uint16_t) (dp + 0x0072), 0);  /* stz walk_cycle_parity */
  S(0x80D9, 2); t_write16(ss, (uint16_t) (dp + 0x0074), 0);  /* C080D9 stz $74 */
  S(0x80DB, 2); t_write16(ss, (uint16_t) (dp + 0x0076), 0);  /* C080DB stz $76 */
  S(0x80DD, 2); t_write16(ss, (uint16_t) (dp + 0x0078), 0);  /* C080DD stz $78 */
  S(0x80DF, 3); t_write16(ss, ss_abs(ss, 0x0C15), 0);  /* C080DF stz $0C15 */
  S(0x80E2, 3); t_write16(ss, ss_abs(ss, 0x0C1B), 0);  /* C080E2 stz $0C1B */
  S(0x80E5, 3); t_write16(ss, ss_abs(ss, 0x0C1D), 0);  /* C080E5 stz $0C1D */
  SIMM16(0x80E8); a = 0x3F60; ss_set_nz16(ss, a);      /* C080E8 lda #$3F60 */
  S(0x80EB, 3); t_write16(ss, ss_abs(ss, 0x0C2D), a);  /* C080EB sta $0C2D */
  SIMM16(0x80EE); a = 0x80F4; ss_set_nz16(ss, a);      /* C080EE lda #$80F4 */
  SJMP(0x80F1);                                        /* C080F1 jmp loc_C0A4E9 */
  TAIL(pb, 0xA4E9);
}

static void reset_native(SnesState* ss)    { reset_native_at(ss, 0x8002); }
static void reset_wram_clear(SnesState* ss){ reset_native_at(ss, 0x8012); }
static void reset_restart(SnesState* ss)  { reset_mode_restart(ss, 0x8042); }
static void reset_mode_init(SnesState* ss){ reset_mode_restart(ss, 0x805E); }

/* ---- registry ----------------------------------------------------------- */
/* Ten of the sixteen rows are secondary entry addresses of the routine listed
 * above them. Seven exist because the ROM enters the routine's middle from
 * outside it ($8042, $805E, $8BE4, $8E2B, $8E39, $8E7F, $A4E9/$A4F5), one
 * because the ROM has to run the routine's `xce` ($8002), one because the body
 * hands the WRAM clear back and needs the loop head to pick it up again
 * ($8012). Each needs its own row for the registry to dispatch it, and each
 * carries the disassembly's own label name, so the harness prints one call
 * count per entry rather than folding them together. Only the seven names that
 * `out/symbols.txt` gives a routine -- reset, nmi_handler_gameplay,
 * ppu_regs_default, ppu_init, unused_vec, nmi, nmi_handler_title_fade -- carry
 * traced bytes for tools/progress.py; the loc_* rows credit nothing and are
 * there to be read and to be bisectable with --only. */
static const RecompEntry kTopLevel[] = {
  { 0xc08000, "reset",                  reset },
  { 0xc08002, "reset_native",           reset_native },
  { 0xc08012, "loc_C08012",             reset_wram_clear },
  { 0xc08042, "loc_C08042",             reset_restart },
  { 0xc0805e, "loc_C0805E",             reset_mode_init },
  { 0xc080f4, "nmi_handler_gameplay",   nmi_handler_gameplay },
  { 0xc08bdb, "loc_C08BDB",       ppu_regs_default },
  { 0xc08be4, "loc_C08BE4",             ppu_regs_default_8BE4 },
  { 0xc08e2b, "loc_C08E2B",             ppu_regs_default_8E2B },
  { 0xc08e39, "loc_C08E39",             ppu_regs_default_8E39 },
  { 0xc08e7f, "loc_C08E7F",             ppu_regs_default_8E7F },
  { 0xc0a385, "ppu_init",               ppu_init },
  { 0xc0a442, "unused_vec",             unused_vec },
  { 0xc0a4d1, "nmi",                    nmi },
  { 0xc0a4e9, "loc_C0A4E9",             nmi_install_handler },
  { 0xc0a4f5, "loc_C0A4F5",             nmi_park },
  { 0xc0bd20, "nmi_handler_title_fade", nmi_handler_title_fade },
};
RECOMP_REGISTER(kTopLevel)
