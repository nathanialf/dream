/* Implementation of the recomp hook API over the vendored LakeSnes core.
 *
 * The timed accessors go through snes_cpuRead / snes_cpuWrite / snes_cpuIdle,
 * the very functions the CPU core hands to cpu_init(), so a hook that replays a
 * routine's bus transactions costs the emulator exactly what the routine cost:
 * the same access times, the same DMA/HDMA interleaving, the same open-bus.
 */
#include <stdint.h>
#include <stdbool.h>

#include "ss_internal.h"

/* ---- registers -------------------------------------------------------- */
uint16_t ss_a(const SnesState* ss)  { return ss->snes->cpu->a; }
uint16_t ss_x(const SnesState* ss)  { return ss->snes->cpu->x; }
uint16_t ss_y(const SnesState* ss)  { return ss->snes->cpu->y; }
uint16_t ss_sp(const SnesState* ss) { return ss->snes->cpu->sp; }
uint16_t ss_dp(const SnesState* ss) { return ss->snes->cpu->dp; }
uint16_t ss_pc(const SnesState* ss) { return ss->snes->cpu->pc; }
uint8_t  ss_db(const SnesState* ss) { return ss->snes->cpu->db; }
uint8_t  ss_pb(const SnesState* ss) { return ss->snes->cpu->k; }

uint8_t ss_p(const SnesState* ss) {
  const Cpu* c = ss->snes->cpu;
  return (uint8_t) ((c->n << 7) | (c->v << 6) | (c->mf << 5) | (c->xf << 4) |
                    (c->d << 3) | (c->i << 2) | (c->z << 1) | c->c);
}

void ss_set_a(SnesState* ss, uint16_t v)  { ss->snes->cpu->a = v; }
void ss_set_x(SnesState* ss, uint16_t v)  { ss->snes->cpu->x = v; }
void ss_set_y(SnesState* ss, uint16_t v)  { ss->snes->cpu->y = v; }
void ss_set_sp(SnesState* ss, uint16_t v) { ss->snes->cpu->sp = v; }
void ss_set_dp(SnesState* ss, uint16_t v) { ss->snes->cpu->dp = v; }
void ss_set_db(SnesState* ss, uint8_t v)  { ss->snes->cpu->db = v; }

void ss_set_pc(SnesState* ss, uint8_t bank, uint16_t pc) {
  ss->snes->cpu->k = bank;
  ss->snes->cpu->pc = pc;
}

void ss_set_p(SnesState* ss, uint8_t val) {
  Cpu* c = ss->snes->cpu;
  c->n = (val & 0x80) != 0;
  c->v = (val & 0x40) != 0;
  c->mf = (val & 0x20) != 0;
  c->xf = (val & 0x10) != 0;
  c->d = (val & 0x08) != 0;
  c->i = (val & 0x04) != 0;
  c->z = (val & 0x02) != 0;
  c->c = (val & 0x01) != 0;
  if(c->e) { c->mf = true; c->xf = true; c->sp = (c->sp & 0xff) | 0x100; }
  if(c->xf) { c->x &= 0xff; c->y &= 0xff; }
}

bool ss_flag_e(const SnesState* ss) { return ss->snes->cpu->e; }
bool ss_flag_m(const SnesState* ss) { return ss->snes->cpu->mf; }
bool ss_flag_x(const SnesState* ss) { return ss->snes->cpu->xf; }

void ss_set_nz16(SnesState* ss, uint16_t v) {
  Cpu* c = ss->snes->cpu;
  c->z = (v == 0);
  c->n = (v & 0x8000) != 0;
}

void ss_set_nz8(SnesState* ss, uint8_t v) {
  Cpu* c = ss->snes->cpu;
  c->z = (v == 0);
  c->n = (v & 0x80) != 0;
}

bool ss_int_pending(const SnesState* ss) {
  const Cpu* c = ss->snes->cpu;
  return c->intWanted || c->nmiWanted || c->irqWanted;
}

void ss_check_int(SnesState* ss) {
  cpu_checkIntPublic(ss->snes->cpu);
}

/* ---- memory, untimed -------------------------------------------------- */
uint8_t ss_wram_r8(const SnesState* ss, uint32_t off) {
  return ss->snes->ram[off & 0x1ffff];
}

uint16_t ss_wram_r16(const SnesState* ss, uint32_t off) {
  return (uint16_t) (ss->snes->ram[off & 0x1ffff] |
                     (ss->snes->ram[(off + 1) & 0x1ffff] << 8));
}

void ss_wram_w8(SnesState* ss, uint32_t off, uint8_t v) {
  ss->snes->ram[off & 0x1ffff] = v;
}

void ss_wram_w16(SnesState* ss, uint32_t off, uint16_t v) {
  ss->snes->ram[off & 0x1ffff] = (uint8_t) v;
  ss->snes->ram[(off + 1) & 0x1ffff] = (uint8_t) (v >> 8);
}

uint8_t ss_r8(SnesState* ss, uint32_t adr24) {
  return snes_read(ss->snes, adr24 & 0xffffff);
}

uint16_t ss_r16(SnesState* ss, uint32_t adr24) {
  uint8_t lo = snes_read(ss->snes, adr24 & 0xffffff);
  uint8_t hi = snes_read(ss->snes, (adr24 + 1) & 0xffffff);
  return (uint16_t) (lo | (hi << 8));
}

void ss_w8(SnesState* ss, uint32_t adr24, uint8_t v) {
  snes_write(ss->snes, adr24 & 0xffffff, v);
}

void ss_w16(SnesState* ss, uint32_t adr24, uint16_t v) {
  snes_write(ss->snes, adr24 & 0xffffff, (uint8_t) v);
  snes_write(ss->snes, (adr24 + 1) & 0xffffff, (uint8_t) (v >> 8));
}

/* ---- memory, timed ---------------------------------------------------- */
uint8_t ss_bus_r8(SnesState* ss, uint32_t adr24) {
  return snes_cpuRead(ss->snes, adr24 & 0xffffff);
}

void ss_bus_w8(SnesState* ss, uint32_t adr24, uint8_t v) {
  snes_cpuWrite(ss->snes, adr24 & 0xffffff, v);
}

void ss_bus_w16(SnesState* ss, uint32_t adr24, uint16_t v) {
  /* the 65816 writes the low byte first for every non-reversed 16-bit store */
  snes_cpuWrite(ss->snes, adr24 & 0xffffff, (uint8_t) v);
  snes_cpuWrite(ss->snes, (adr24 + 1) & 0xffffff, (uint8_t) (v >> 8));
}

void ss_fetch(SnesState* ss, int bytes) {
  Cpu* c = ss->snes->cpu;
  for(int i = 0; i < bytes; i++) {
    snes_cpuRead(ss->snes, (uint32_t) ((c->k << 16) | c->pc));
    c->pc = (uint16_t) (c->pc + 1);
  }
}

void ss_idle(SnesState* ss) {
  snes_cpuIdle(ss->snes, false);
}

/* ---- returns ---------------------------------------------------------- */
static uint8_t ss_pull8(SnesState* ss) {
  Cpu* c = ss->snes->cpu;
  c->sp++;
  if(c->e) c->sp = (uint16_t) ((c->sp & 0xff) | 0x100);
  return snes_cpuRead(ss->snes, c->sp);
}

void ss_rts(SnesState* ss) {
  /* mirrors LakeSnes cpu.c case 0x60 exactly */
  Cpu* c = ss->snes->cpu;
  ss_idle(ss);
  ss_idle(ss);
  uint8_t lo = ss_pull8(ss);
  uint8_t hi = ss_pull8(ss);
  c->pc = (uint16_t) ((lo | (hi << 8)) + 1);
  cpu_checkIntPublic(c);
  ss_idle(ss);
}

void ss_rtl(SnesState* ss) {
  /* mirrors LakeSnes cpu.c case 0x6b exactly */
  Cpu* c = ss->snes->cpu;
  ss_idle(ss);
  ss_idle(ss);
  uint8_t lo = ss_pull8(ss);
  uint8_t hi = ss_pull8(ss);
  c->pc = (uint16_t) ((lo | (hi << 8)) + 1);
  cpu_checkIntPublic(c);
  c->k = ss_pull8(ss);
}

/* ---- table helper ----------------------------------------------------- */
unsigned recomp_hooks_count(const RecompHook* table) {
  unsigned n = 0;
  while(table[n].fn != NULL) n++;
  return n;
}
