/* 65816 arithmetic with its flag side effects, so a C body can be read against
 * the disassembly instruction for instruction.
 *
 * Only the 16-bit (m = 0) forms are here; the routines converted so far do their
 * arithmetic with a 16-bit accumulator and drop to 8 bits only for register
 * stores, where the flags are set by ss_set_nz8(). Decimal mode is not modelled:
 * the game clears D at reset ($C08005 cld) and never sets it.
 */
#ifndef DREAM_ALU_H
#define DREAM_ALU_H

#include "snes_state.h"

static inline uint32_t ss_abs(SnesState* ss, uint32_t abs) {
  /* absolute / absolute-indexed: the data bank supplies the high byte */
  return (((uint32_t) ss_db(ss) << 16) + abs) & 0xffffff;
}

static inline uint16_t alu_adc16(SnesState* ss, uint16_t a, uint16_t v) {
  uint32_t r = (uint32_t) a + v + (ss_c(ss) ? 1u : 0u);
  ss_set_v(ss, ((a & 0x8000) == (v & 0x8000)) && ((v & 0x8000) != (r & 0x8000)));
  ss_set_c(ss, r > 0xffff);
  ss_set_nz16(ss, (uint16_t) r);
  return (uint16_t) r;
}

static inline uint16_t alu_sbc16(SnesState* ss, uint16_t a, uint16_t v) {
  return alu_adc16(ss, a, (uint16_t) ~v);   /* sbc is adc of the complement */
}

static inline void alu_cmp16(SnesState* ss, uint16_t a, uint16_t v) {
  uint32_t r = (uint32_t) a + (uint16_t) ~v + 1u;
  ss_set_c(ss, r > 0xffff);
  ss_set_nz16(ss, (uint16_t) r);
}

static inline void alu_cpx16(SnesState* ss, uint16_t x, uint16_t v) {
  alu_cmp16(ss, x, v);
}

/* bit with a non-immediate operand takes N and V from the *operand*; the
 * immediate form (bit #) touches only Z. */
static inline void alu_bit16(SnesState* ss, uint16_t a, uint16_t v) {
  ss_set_z(ss, (uint16_t) (a & v) == 0);
  ss_set_n(ss, (v & 0x8000) != 0);
  ss_set_v(ss, (v & 0x4000) != 0);
}

static inline void alu_bit_imm16(SnesState* ss, uint16_t a, uint16_t v) {
  ss_set_z(ss, (uint16_t) (a & v) == 0);
}

static inline void alu_bit_imm8(SnesState* ss, uint8_t a, uint8_t v) {
  ss_set_z(ss, (uint8_t) (a & v) == 0);
}

static inline void alu_bit8(SnesState* ss, uint8_t a, uint8_t v) {
  ss_set_z(ss, (uint8_t) (a & v) == 0);
  ss_set_n(ss, (v & 0x80) != 0);
  ss_set_v(ss, (v & 0x40) != 0);
}

static inline uint16_t alu_lsr16(SnesState* ss, uint16_t a) {
  ss_set_c(ss, (a & 1) != 0);
  a = (uint16_t) (a >> 1);
  ss_set_nz16(ss, a);
  return a;
}

static inline uint16_t alu_asl16(SnesState* ss, uint16_t a) {
  ss_set_c(ss, (a & 0x8000) != 0);
  a = (uint16_t) (a << 1);
  ss_set_nz16(ss, a);
  return a;
}

static inline uint16_t alu_ror16(SnesState* ss, uint16_t a) {
  bool cin = ss_c(ss);
  ss_set_c(ss, (a & 1) != 0);
  a = (uint16_t) ((a >> 1) | (cin ? 0x8000u : 0u));
  ss_set_nz16(ss, a);
  return a;
}

static inline uint16_t alu_inc16(SnesState* ss, uint16_t a) {
  a = (uint16_t) (a + 1);
  ss_set_nz16(ss, a);
  return a;
}

static inline uint16_t alu_dec16(SnesState* ss, uint16_t a) {
  a = (uint16_t) (a - 1);
  ss_set_nz16(ss, a);
  return a;
}

/* the logic ops leave C and V alone */
static inline uint16_t alu_and16(SnesState* ss, uint16_t a, uint16_t v) {
  a = (uint16_t) (a & v); ss_set_nz16(ss, a); return a;
}
static inline uint16_t alu_ora16(SnesState* ss, uint16_t a, uint16_t v) {
  a = (uint16_t) (a | v); ss_set_nz16(ss, a); return a;
}
static inline uint16_t alu_eor16(SnesState* ss, uint16_t a, uint16_t v) {
  a = (uint16_t) (a ^ v); ss_set_nz16(ss, a); return a;
}

#endif
