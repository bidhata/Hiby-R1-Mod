#include "cpu.h"
#include "gb.h"
#include "mmu.h"
#include <string.h>

#define F_Z 0x80
#define F_N 0x40
#define F_H 0x20
#define F_C 0x10

/* Every memory access occupies one four-cycle machine cycle, and the access
 * lands at the end of it. Ticking the rest of the hardware here is what keeps
 * a timer read inside an instruction returning the value the real chip would
 * have had at that point. cpu.tick_acc records what an instruction has already
 * ticked so gb_cpu_step can settle the difference afterwards. */
static inline void cpu_tick(gb_t *gb, int cycles) {
    gb->cpu.tick_acc += cycles;
    gb_tick(gb, cycles);
}

static inline u8 cpu_read(gb_t *gb, u16 addr) {
    cpu_tick(gb, 4);
    return gb_mmu_read(&gb->mmu, gb, addr);
}

static inline void cpu_write(gb_t *gb, u16 addr, u8 val) {
    cpu_tick(gb, 4);
    gb_mmu_write(&gb->mmu, gb, addr, val);
}

#define READ(gb, a)    cpu_read((gb), (a))
#define WRITE(gb, a, v) cpu_write((gb), (a), (v))

/* An internal machine cycle: the CPU is busy but touches no memory. */
#define INTERNAL(gb) cpu_tick((gb), 4)

typedef int (*cpu_op_t)(gb_t *gb);
static cpu_op_t cb_ops[256];

static inline u8 fetch_byte(gb_t *gb) {
    u8 v = READ(gb, gb->cpu.reg.pc);
    gb->cpu.reg.pc++;
    return v;
}

static inline u16 fetch_word(gb_t *gb) {
    u8 lo = fetch_byte(gb);
    u8 hi = fetch_byte(gb);
    return (u16)(hi << 8) | lo;
}

static inline void push_u16(gb_t *gb, u16 val) {
    /* The stack pointer is decremented in an internal cycle that runs before
     * either byte reaches memory. */
    INTERNAL(gb);
    gb->cpu.reg.sp -= 2;
    WRITE(gb, gb->cpu.reg.sp + 1, (u8)(val >> 8));
    WRITE(gb, gb->cpu.reg.sp, (u8)(val & 0xFF));
}

static inline u16 pop_u16(gb_t *gb) {
    u8 lo = READ(gb, gb->cpu.reg.sp);
    u8 hi = READ(gb, gb->cpu.reg.sp + 1);
    gb->cpu.reg.sp += 2;
    return (u16)(hi << 8) | lo;
}

#define SET_Z(cpu, v) do { if(v) (cpu)->reg.f |= F_Z; else (cpu)->reg.f &= ~F_Z; } while(0)
#define SET_N(cpu, v) do { if(v) (cpu)->reg.f |= F_N; else (cpu)->reg.f &= ~F_N; } while(0)
#define SET_H(cpu, v) do { if(v) (cpu)->reg.f |= F_H; else (cpu)->reg.f &= ~F_H; } while(0)
#define SET_C(cpu, v) do { if(v) (cpu)->reg.f |= F_C; else (cpu)->reg.f &= ~F_C; } while(0)

static inline int read_reg_idx(gb_t *gb, int idx) {
    switch(idx & 7) {
        case 0: return gb->cpu.reg.b;
        case 1: return gb->cpu.reg.c;
        case 2: return gb->cpu.reg.d;
        case 3: return gb->cpu.reg.e;
        case 4: return gb->cpu.reg.h;
        case 5: return gb->cpu.reg.l;
        case 6: return READ(gb, gb->cpu.reg.hl);
        case 7: return gb->cpu.reg.a;
    }
    return 0;
}

static inline void write_reg_idx(gb_t *gb, int idx, u8 val) {
    switch(idx & 7) {
        case 0: gb->cpu.reg.b = val; break;
        case 1: gb->cpu.reg.c = val; break;
        case 2: gb->cpu.reg.d = val; break;
        case 3: gb->cpu.reg.e = val; break;
        case 4: gb->cpu.reg.h = val; break;
        case 5: gb->cpu.reg.l = val; break;
        case 6: WRITE(gb, gb->cpu.reg.hl, val); break;
        case 7: gb->cpu.reg.a = val; break;
    }
}

static inline u8 alu_add(gb_t *gb, u8 a, u8 b, int carry) {
    int result = a + b + carry;
    int half = (a & 0xF) + (b & 0xF) + carry;
    SET_Z(&gb->cpu, (result & 0xFF) == 0);
    SET_N(&gb->cpu, 0);
    SET_H(&gb->cpu, half > 0xF);
    SET_C(&gb->cpu, result > 0xFF);
    return (u8)result;
}

static inline u8 alu_sub(gb_t *gb, u8 a, u8 b, int carry) {
    int result = a - b - carry;
    int half = (a & 0xF) - (b & 0xF) - carry;
    SET_Z(&gb->cpu, (result & 0xFF) == 0);
    SET_N(&gb->cpu, 1);
    SET_H(&gb->cpu, half < 0);
    SET_C(&gb->cpu, result < 0);
    return (u8)result;
}

static inline u8 alu_and(gb_t *gb, u8 a, u8 b) {
    u8 r = a & b;
    SET_Z(&gb->cpu, r == 0);
    SET_N(&gb->cpu, 0);
    SET_H(&gb->cpu, 1);
    SET_C(&gb->cpu, 0);
    return r;
}

static inline u8 alu_xor(gb_t *gb, u8 a, u8 b) {
    u8 r = a ^ b;
    SET_Z(&gb->cpu, r == 0);
    SET_N(&gb->cpu, 0);
    SET_H(&gb->cpu, 0);
    SET_C(&gb->cpu, 0);
    return r;
}

static inline u8 alu_or(gb_t *gb, u8 a, u8 b) {
    u8 r = a | b;
    SET_Z(&gb->cpu, r == 0);
    SET_N(&gb->cpu, 0);
    SET_H(&gb->cpu, 0);
    SET_C(&gb->cpu, 0);
    return r;
}

static inline void alu_cp(gb_t *gb, u8 a, u8 b) {
    alu_sub(gb, a, b, 0);
}

static inline u8 alu_inc(gb_t *gb, u8 val) {
    u8 r = val + 1;
    SET_Z(&gb->cpu, r == 0);
    SET_N(&gb->cpu, 0);
    SET_H(&gb->cpu, (val & 0xF) == 0xF);
    return r;
}

static inline u8 alu_dec(gb_t *gb, u8 val) {
    u8 r = val - 1;
    SET_Z(&gb->cpu, r == 0);
    SET_N(&gb->cpu, 1);
    SET_H(&gb->cpu, (val & 0xF) == 0);
    return r;
}

static inline void add_hl(gb_t *gb, u16 val) {
    u32 hl = gb->cpu.reg.hl;
    u32 result = hl + val;
    SET_N(&gb->cpu, 0);
    SET_H(&gb->cpu, ((hl & 0xFFF) + (val & 0xFFF)) > 0xFFF);
    SET_C(&gb->cpu, result > 0xFFFF);
    gb->cpu.reg.hl = (u16)result;
}

static inline void add_sp_r8(gb_t *gb) {
    s8 r8 = (s8)fetch_byte(gb);
    u16 sp = gb->cpu.reg.sp;
    u16 result = sp + (u16)(s16)r8;
    SET_Z(&gb->cpu, 0);
    SET_N(&gb->cpu, 0);
    SET_H(&gb->cpu, ((sp & 0xF) + ((u8)r8 & 0xF)) > 0xF);
    SET_C(&gb->cpu, ((sp & 0xFF) + ((u8)r8 & 0xFF)) > 0xFF);
    gb->cpu.reg.sp = result;
}

static inline u8 rotate_left(gb_t *gb, u8 val) {
    u8 old = val;
    val = (val << 1) | (val >> 7);
    SET_Z(&gb->cpu, 0);
    SET_N(&gb->cpu, 0);
    SET_H(&gb->cpu, 0);
    SET_C(&gb->cpu, (old & 0x80) != 0);
    return val;
}

static inline u8 rotate_right(gb_t *gb, u8 val) {
    u8 old = val;
    val = (val >> 1) | (val << 7);
    SET_Z(&gb->cpu, 0);
    SET_N(&gb->cpu, 0);
    SET_H(&gb->cpu, 0);
    SET_C(&gb->cpu, (old & 1) != 0);
    return val;
}

static inline u8 rotate_left_carry(gb_t *gb, u8 val) {
    u8 old = val;
    val = (val << 1) | (gb->cpu.reg.f & F_C ? 1 : 0);
    SET_Z(&gb->cpu, 0);
    SET_N(&gb->cpu, 0);
    SET_H(&gb->cpu, 0);
    SET_C(&gb->cpu, (old & 0x80) != 0);
    return val;
}

static inline u8 rotate_right_carry(gb_t *gb, u8 val) {
    u8 old = val;
    val = (val >> 1) | (gb->cpu.reg.f & F_C ? 0x80 : 0);
    SET_Z(&gb->cpu, 0);
    SET_N(&gb->cpu, 0);
    SET_H(&gb->cpu, 0);
    SET_C(&gb->cpu, (old & 1) != 0);
    return val;
}

/* CB-prefixed rotates differ from the accumulator forms: they set Z. */
static inline u8 cb_rotate_left(gb_t *gb, u8 val) {
    u8 r = (u8)((val << 1) | (val >> 7));
    SET_Z(&gb->cpu, r == 0);
    SET_N(&gb->cpu, 0);
    SET_H(&gb->cpu, 0);
    SET_C(&gb->cpu, (val & 0x80) != 0);
    return r;
}

static inline u8 cb_rotate_right(gb_t *gb, u8 val) {
    u8 r = (u8)((val >> 1) | (val << 7));
    SET_Z(&gb->cpu, r == 0);
    SET_N(&gb->cpu, 0);
    SET_H(&gb->cpu, 0);
    SET_C(&gb->cpu, (val & 1) != 0);
    return r;
}

static inline u8 cb_rotate_left_carry(gb_t *gb, u8 val) {
    u8 r = (u8)((val << 1) | ((gb->cpu.reg.f & F_C) ? 1 : 0));
    SET_Z(&gb->cpu, r == 0);
    SET_N(&gb->cpu, 0);
    SET_H(&gb->cpu, 0);
    SET_C(&gb->cpu, (val & 0x80) != 0);
    return r;
}

static inline u8 cb_rotate_right_carry(gb_t *gb, u8 val) {
    u8 r = (u8)((val >> 1) | ((gb->cpu.reg.f & F_C) ? 0x80 : 0));
    SET_Z(&gb->cpu, r == 0);
    SET_N(&gb->cpu, 0);
    SET_H(&gb->cpu, 0);
    SET_C(&gb->cpu, (val & 1) != 0);
    return r;
}

static inline u8 shift_left_arith(gb_t *gb, u8 val) {
    SET_C(&gb->cpu, (val & 0x80) != 0);
    val <<= 1;
    SET_Z(&gb->cpu, val == 0);
    SET_N(&gb->cpu, 0);
    SET_H(&gb->cpu, 0);
    return val;
}

static inline u8 shift_right_arith(gb_t *gb, u8 val) {
    SET_C(&gb->cpu, val & 1);
    val = (val >> 1) | (val & 0x80);
    SET_Z(&gb->cpu, val == 0);
    SET_N(&gb->cpu, 0);
    SET_H(&gb->cpu, 0);
    return val;
}

static inline u8 shift_right_logical(gb_t *gb, u8 val) {
    SET_C(&gb->cpu, val & 1);
    val >>= 1;
    SET_Z(&gb->cpu, val == 0);
    SET_N(&gb->cpu, 0);
    SET_H(&gb->cpu, 0);
    return val;
}

static inline u8 swap_nibbles(gb_t *gb, u8 val) {
    u8 r = ((val & 0xF) << 4) | ((val >> 4) & 0xF);
    SET_Z(&gb->cpu, r == 0);
    SET_N(&gb->cpu, 0);
    SET_H(&gb->cpu, 0);
    SET_C(&gb->cpu, 0);
    return r;
}

static inline void daa(gb_t *gb) {
    int a = gb->cpu.reg.a;
    int carry = (gb->cpu.reg.f & F_C) != 0;
    int half = (gb->cpu.reg.f & F_H) != 0;

    if (!(gb->cpu.reg.f & F_N)) {
        /* After an addition, carry is set whenever the high nibble is fixed up. */
        if (half || (a & 0x0F) > 0x09) a += 0x06;
        if (carry || a > 0x9F) { a += 0x60; carry = 1; }
    } else {
        /* After a subtraction, the carry flag is preserved. */
        if (half) a = (a - 0x06) & 0xFF;
        if (carry) a -= 0x60;
    }

    gb->cpu.reg.a = (u8)a;
    SET_Z(&gb->cpu, gb->cpu.reg.a == 0);
    SET_H(&gb->cpu, 0);
    SET_C(&gb->cpu, carry);
}

/* ========== Main opcodes 0x00 - 0x3F ========== */
static int op_00(gb_t *gb) { (void)gb; return 4; } /* NOP */
static int op_01(gb_t *gb) { gb->cpu.reg.bc = fetch_word(gb); return 12; } /* LD BC,d16 */
static int op_02(gb_t *gb) { WRITE(gb, gb->cpu.reg.bc, gb->cpu.reg.a); return 8; } /* LD (BC),A */
static int op_03(gb_t *gb) { gb->cpu.reg.bc++; return 8; } /* INC BC */
static int op_04(gb_t *gb) { gb->cpu.reg.b = alu_inc(gb, gb->cpu.reg.b); return 4; } /* INC B */
static int op_05(gb_t *gb) { gb->cpu.reg.b = alu_dec(gb, gb->cpu.reg.b); return 4; } /* DEC B */
static int op_06(gb_t *gb) { gb->cpu.reg.b = fetch_byte(gb); return 8; } /* LD B,d8 */
static int op_07(gb_t *gb) { gb->cpu.reg.a = rotate_left(gb, gb->cpu.reg.a); return 4; } /* RLCA */
static int op_08(gb_t *gb) { u16 a16 = fetch_word(gb); WRITE(gb, a16, gb->cpu.reg.sp & 0xFF); WRITE(gb, a16+1, gb->cpu.reg.sp >> 8); return 20; } /* LD (a16),SP */
static int op_09(gb_t *gb) { add_hl(gb, gb->cpu.reg.bc); return 8; } /* ADD HL,BC */
static int op_0A(gb_t *gb) { gb->cpu.reg.a = READ(gb, gb->cpu.reg.bc); return 8; } /* LD A,(BC) */
static int op_0B(gb_t *gb) { gb->cpu.reg.bc--; return 8; } /* DEC BC */
static int op_0C(gb_t *gb) { gb->cpu.reg.c = alu_inc(gb, gb->cpu.reg.c); return 4; } /* INC C */
static int op_0D(gb_t *gb) { gb->cpu.reg.c = alu_dec(gb, gb->cpu.reg.c); return 4; } /* DEC C */
static int op_0E(gb_t *gb) { gb->cpu.reg.c = fetch_byte(gb); return 8; } /* LD C,d8 */
static int op_0F(gb_t *gb) { gb->cpu.reg.a = rotate_right(gb, gb->cpu.reg.a); return 4; } /* RRCA */
/* STOP is two bytes but only four cycles: the second byte is skipped without a
 * memory cycle of its own. */
/* STOP is two bytes but only four cycles: the second byte is skipped without a
 * memory cycle of its own. On CGB it also carries out a speed switch that KEY1
 * has armed, which is the only use most games have for it. */
static int op_10(gb_t *gb) {
    gb->cpu.reg.pc++;
    if (gb->cgb_mode && gb->speed_switch_armed) {
        gb->double_speed = !gb->double_speed;
        gb->speed_switch_armed = false;
        /* DIV restarts across a speed change. */
        gb->io[0x04] = 0;
        gb->cpu.div_counter = 0;
    }
    return 4;
}
static int op_11(gb_t *gb) { gb->cpu.reg.de = fetch_word(gb); return 12; } /* LD DE,d16 */
static int op_12(gb_t *gb) { WRITE(gb, gb->cpu.reg.de, gb->cpu.reg.a); return 8; } /* LD (DE),A */
static int op_13(gb_t *gb) { gb->cpu.reg.de++; return 8; } /* INC DE */
static int op_14(gb_t *gb) { gb->cpu.reg.d = alu_inc(gb, gb->cpu.reg.d); return 4; } /* INC D */
static int op_15(gb_t *gb) { gb->cpu.reg.d = alu_dec(gb, gb->cpu.reg.d); return 4; } /* DEC D */
static int op_16(gb_t *gb) { gb->cpu.reg.d = fetch_byte(gb); return 8; } /* LD D,d8 */
static int op_17(gb_t *gb) { gb->cpu.reg.a = rotate_left_carry(gb, gb->cpu.reg.a); return 4; } /* RLA */
static int op_18(gb_t *gb) { s8 r8 = (s8)fetch_byte(gb); gb->cpu.reg.pc += r8; return 12; } /* JR r8 */
static int op_19(gb_t *gb) { add_hl(gb, gb->cpu.reg.de); return 8; } /* ADD HL,DE */
static int op_1A(gb_t *gb) { gb->cpu.reg.a = READ(gb, gb->cpu.reg.de); return 8; } /* LD A,(DE) */
static int op_1B(gb_t *gb) { gb->cpu.reg.de--; return 8; } /* DEC DE */
static int op_1C(gb_t *gb) { gb->cpu.reg.e = alu_inc(gb, gb->cpu.reg.e); return 4; } /* INC E */
static int op_1D(gb_t *gb) { gb->cpu.reg.e = alu_dec(gb, gb->cpu.reg.e); return 4; } /* DEC E */
static int op_1E(gb_t *gb) { gb->cpu.reg.e = fetch_byte(gb); return 8; } /* LD E,d8 */
static int op_1F(gb_t *gb) { gb->cpu.reg.a = rotate_right_carry(gb, gb->cpu.reg.a); return 4; } /* RRA */
static int op_20(gb_t *gb) { s8 r8 = (s8)fetch_byte(gb); if (!(gb->cpu.reg.f & F_Z)) { gb->cpu.reg.pc += r8; return 12; } return 8; } /* JR NZ,r8 */
static int op_21(gb_t *gb) { gb->cpu.reg.hl = fetch_word(gb); return 12; } /* LD HL,d16 */
static int op_22(gb_t *gb) { WRITE(gb, gb->cpu.reg.hl, gb->cpu.reg.a); gb->cpu.reg.hl++; return 8; } /* LD (HL+),A */
static int op_23(gb_t *gb) { gb->cpu.reg.hl++; return 8; } /* INC HL */
static int op_24(gb_t *gb) { gb->cpu.reg.h = alu_inc(gb, gb->cpu.reg.h); return 4; } /* INC H */
static int op_25(gb_t *gb) { gb->cpu.reg.h = alu_dec(gb, gb->cpu.reg.h); return 4; } /* DEC H */
static int op_26(gb_t *gb) { gb->cpu.reg.h = fetch_byte(gb); return 8; } /* LD H,d8 */
static int op_27(gb_t *gb) { daa(gb); return 4; } /* DAA */
static int op_28(gb_t *gb) { s8 r8 = (s8)fetch_byte(gb); if (gb->cpu.reg.f & F_Z) { gb->cpu.reg.pc += r8; return 12; } return 8; } /* JR Z,r8 */
static int op_29(gb_t *gb) { add_hl(gb, gb->cpu.reg.hl); return 8; } /* ADD HL,HL */
static int op_2A(gb_t *gb) { gb->cpu.reg.a = READ(gb, gb->cpu.reg.hl); gb->cpu.reg.hl++; return 8; } /* LD A,(HL+) */
static int op_2B(gb_t *gb) { gb->cpu.reg.hl--; return 8; } /* DEC HL */
static int op_2C(gb_t *gb) { gb->cpu.reg.l = alu_inc(gb, gb->cpu.reg.l); return 4; } /* INC L */
static int op_2D(gb_t *gb) { gb->cpu.reg.l = alu_dec(gb, gb->cpu.reg.l); return 4; } /* DEC L */
static int op_2E(gb_t *gb) { gb->cpu.reg.l = fetch_byte(gb); return 8; } /* LD L,d8 */
static int op_2F(gb_t *gb) { gb->cpu.reg.a = ~gb->cpu.reg.a; SET_N(&gb->cpu, 1); SET_H(&gb->cpu, 1); return 4; } /* CPL */
static int op_30(gb_t *gb) { s8 r8 = (s8)fetch_byte(gb); if (!(gb->cpu.reg.f & F_C)) { gb->cpu.reg.pc += r8; return 12; } return 8; } /* JR NC,r8 */
static int op_31(gb_t *gb) { gb->cpu.reg.sp = fetch_word(gb); return 12; } /* LD SP,d16 */
static int op_32(gb_t *gb) { WRITE(gb, gb->cpu.reg.hl, gb->cpu.reg.a); gb->cpu.reg.hl--; return 8; } /* LD (HL-),A */
static int op_33(gb_t *gb) { gb->cpu.reg.sp++; return 8; } /* INC SP */
static int op_34(gb_t *gb) { WRITE(gb, gb->cpu.reg.hl, alu_inc(gb, READ(gb, gb->cpu.reg.hl))); return 12; } /* INC (HL) */
static int op_35(gb_t *gb) { WRITE(gb, gb->cpu.reg.hl, alu_dec(gb, READ(gb, gb->cpu.reg.hl))); return 12; } /* DEC (HL) */
static int op_36(gb_t *gb) { WRITE(gb, gb->cpu.reg.hl, fetch_byte(gb)); return 12; } /* LD (HL),d8 */
static int op_37(gb_t *gb) { SET_N(&gb->cpu, 0); SET_H(&gb->cpu, 0); SET_C(&gb->cpu, 1); return 4; } /* SCF */
static int op_38(gb_t *gb) { s8 r8 = (s8)fetch_byte(gb); if (gb->cpu.reg.f & F_C) { gb->cpu.reg.pc += r8; return 12; } return 8; } /* JR C,r8 */
static int op_39(gb_t *gb) { add_hl(gb, gb->cpu.reg.sp); return 8; } /* ADD HL,SP */
static int op_3A(gb_t *gb) { gb->cpu.reg.a = READ(gb, gb->cpu.reg.hl); gb->cpu.reg.hl--; return 8; } /* LD A,(HL-) */
static int op_3B(gb_t *gb) { gb->cpu.reg.sp--; return 8; } /* DEC SP */
static int op_3C(gb_t *gb) { gb->cpu.reg.a = alu_inc(gb, gb->cpu.reg.a); return 4; } /* INC A */
static int op_3D(gb_t *gb) { gb->cpu.reg.a = alu_dec(gb, gb->cpu.reg.a); return 4; } /* DEC A */
static int op_3E(gb_t *gb) { gb->cpu.reg.a = fetch_byte(gb); return 8; } /* LD A,d8 */
static int op_3F(gb_t *gb) { SET_N(&gb->cpu, 0); SET_H(&gb->cpu, 0); SET_C(&gb->cpu, !(gb->cpu.reg.f & F_C)); return 4; } /* CCF */

/* ========== Main opcodes 0x40 - 0x7F (LD r,r') ========== */
static int op_40(gb_t *gb) { (void)gb; return 4; } /* LD B,B */
static int op_41(gb_t *gb) { gb->cpu.reg.b = gb->cpu.reg.c; return 4; } /* LD B,C */
static int op_42(gb_t *gb) { gb->cpu.reg.b = gb->cpu.reg.d; return 4; } /* LD B,D */
static int op_43(gb_t *gb) { gb->cpu.reg.b = gb->cpu.reg.e; return 4; } /* LD B,E */
static int op_44(gb_t *gb) { gb->cpu.reg.b = gb->cpu.reg.h; return 4; } /* LD B,H */
static int op_45(gb_t *gb) { gb->cpu.reg.b = gb->cpu.reg.l; return 4; } /* LD B,L */
static int op_46(gb_t *gb) { gb->cpu.reg.b = READ(gb, gb->cpu.reg.hl); return 8; } /* LD B,(HL) */
static int op_47(gb_t *gb) { gb->cpu.reg.b = gb->cpu.reg.a; return 4; } /* LD B,A */
static int op_48(gb_t *gb) { gb->cpu.reg.c = gb->cpu.reg.b; return 4; } /* LD C,B */
static int op_49(gb_t *gb) { (void)gb; return 4; } /* LD C,C */
static int op_4A(gb_t *gb) { gb->cpu.reg.c = gb->cpu.reg.d; return 4; } /* LD C,D */
static int op_4B(gb_t *gb) { gb->cpu.reg.c = gb->cpu.reg.e; return 4; } /* LD C,E */
static int op_4C(gb_t *gb) { gb->cpu.reg.c = gb->cpu.reg.h; return 4; } /* LD C,H */
static int op_4D(gb_t *gb) { gb->cpu.reg.c = gb->cpu.reg.l; return 4; } /* LD C,L */
static int op_4E(gb_t *gb) { gb->cpu.reg.c = READ(gb, gb->cpu.reg.hl); return 8; } /* LD C,(HL) */
static int op_4F(gb_t *gb) { gb->cpu.reg.c = gb->cpu.reg.a; return 4; } /* LD C,A */
static int op_50(gb_t *gb) { gb->cpu.reg.d = gb->cpu.reg.b; return 4; } /* LD D,B */
static int op_51(gb_t *gb) { gb->cpu.reg.d = gb->cpu.reg.c; return 4; } /* LD D,C */
static int op_52(gb_t *gb) { (void)gb; return 4; } /* LD D,D */
static int op_53(gb_t *gb) { gb->cpu.reg.d = gb->cpu.reg.e; return 4; } /* LD D,E */
static int op_54(gb_t *gb) { gb->cpu.reg.d = gb->cpu.reg.h; return 4; } /* LD D,H */
static int op_55(gb_t *gb) { gb->cpu.reg.d = gb->cpu.reg.l; return 4; } /* LD D,L */
static int op_56(gb_t *gb) { gb->cpu.reg.d = READ(gb, gb->cpu.reg.hl); return 8; } /* LD D,(HL) */
static int op_57(gb_t *gb) { gb->cpu.reg.d = gb->cpu.reg.a; return 4; } /* LD D,A */
static int op_58(gb_t *gb) { gb->cpu.reg.e = gb->cpu.reg.b; return 4; } /* LD E,B */
static int op_59(gb_t *gb) { gb->cpu.reg.e = gb->cpu.reg.c; return 4; } /* LD E,C */
static int op_5A(gb_t *gb) { gb->cpu.reg.e = gb->cpu.reg.d; return 4; } /* LD E,D */
static int op_5B(gb_t *gb) { (void)gb; return 4; } /* LD E,E */
static int op_5C(gb_t *gb) { gb->cpu.reg.e = gb->cpu.reg.h; return 4; } /* LD E,H */
static int op_5D(gb_t *gb) { gb->cpu.reg.e = gb->cpu.reg.l; return 4; } /* LD E,L */
static int op_5E(gb_t *gb) { gb->cpu.reg.e = READ(gb, gb->cpu.reg.hl); return 8; } /* LD E,(HL) */
static int op_5F(gb_t *gb) { gb->cpu.reg.e = gb->cpu.reg.a; return 4; } /* LD E,A */
static int op_60(gb_t *gb) { gb->cpu.reg.h = gb->cpu.reg.b; return 4; } /* LD H,B */
static int op_61(gb_t *gb) { gb->cpu.reg.h = gb->cpu.reg.c; return 4; } /* LD H,C */
static int op_62(gb_t *gb) { gb->cpu.reg.h = gb->cpu.reg.d; return 4; } /* LD H,D */
static int op_63(gb_t *gb) { gb->cpu.reg.h = gb->cpu.reg.e; return 4; } /* LD H,E */
static int op_64(gb_t *gb) { (void)gb; return 4; } /* LD H,H */
static int op_65(gb_t *gb) { gb->cpu.reg.h = gb->cpu.reg.l; return 4; } /* LD H,L */
static int op_66(gb_t *gb) { gb->cpu.reg.h = READ(gb, gb->cpu.reg.hl); return 8; } /* LD H,(HL) */
static int op_67(gb_t *gb) { gb->cpu.reg.h = gb->cpu.reg.a; return 4; } /* LD H,A */
static int op_68(gb_t *gb) { gb->cpu.reg.l = gb->cpu.reg.b; return 4; } /* LD L,B */
static int op_69(gb_t *gb) { gb->cpu.reg.l = gb->cpu.reg.c; return 4; } /* LD L,C */
static int op_6A(gb_t *gb) { gb->cpu.reg.l = gb->cpu.reg.d; return 4; } /* LD L,D */
static int op_6B(gb_t *gb) { gb->cpu.reg.l = gb->cpu.reg.e; return 4; } /* LD L,E */
static int op_6C(gb_t *gb) { gb->cpu.reg.l = gb->cpu.reg.h; return 4; } /* LD L,H */
static int op_6D(gb_t *gb) { (void)gb; return 4; } /* LD L,L */
static int op_6E(gb_t *gb) { gb->cpu.reg.l = READ(gb, gb->cpu.reg.hl); return 8; } /* LD L,(HL) */
static int op_6F(gb_t *gb) { gb->cpu.reg.l = gb->cpu.reg.a; return 4; } /* LD L,A */
static int op_70(gb_t *gb) { WRITE(gb, gb->cpu.reg.hl, gb->cpu.reg.b); return 8; } /* LD (HL),B */
static int op_71(gb_t *gb) { WRITE(gb, gb->cpu.reg.hl, gb->cpu.reg.c); return 8; } /* LD (HL),C */
static int op_72(gb_t *gb) { WRITE(gb, gb->cpu.reg.hl, gb->cpu.reg.d); return 8; } /* LD (HL),D */
static int op_73(gb_t *gb) { WRITE(gb, gb->cpu.reg.hl, gb->cpu.reg.e); return 8; } /* LD (HL),E */
static int op_74(gb_t *gb) { WRITE(gb, gb->cpu.reg.hl, gb->cpu.reg.h); return 8; } /* LD (HL),H */
static int op_75(gb_t *gb) { WRITE(gb, gb->cpu.reg.hl, gb->cpu.reg.l); return 8; } /* LD (HL),L */
static int op_76(gb_t *gb) { gb->cpu.halted = 1; return 4; } /* HALT */
static int op_77(gb_t *gb) { WRITE(gb, gb->cpu.reg.hl, gb->cpu.reg.a); return 8; } /* LD (HL),A */
static int op_78(gb_t *gb) { gb->cpu.reg.a = gb->cpu.reg.b; return 4; } /* LD A,B */
static int op_79(gb_t *gb) { gb->cpu.reg.a = gb->cpu.reg.c; return 4; } /* LD A,C */
static int op_7A(gb_t *gb) { gb->cpu.reg.a = gb->cpu.reg.d; return 4; } /* LD A,D */
static int op_7B(gb_t *gb) { gb->cpu.reg.a = gb->cpu.reg.e; return 4; } /* LD A,E */
static int op_7C(gb_t *gb) { gb->cpu.reg.a = gb->cpu.reg.h; return 4; } /* LD A,H */
static int op_7D(gb_t *gb) { gb->cpu.reg.a = gb->cpu.reg.l; return 4; } /* LD A,L */
static int op_7E(gb_t *gb) { gb->cpu.reg.a = READ(gb, gb->cpu.reg.hl); return 8; } /* LD A,(HL) */
static int op_7F(gb_t *gb) { (void)gb; return 4; } /* LD A,A */

/* ========== Main opcodes 0x80 - 0xBF (ALU A,r) ========== */
static int op_80(gb_t *gb) { gb->cpu.reg.a = alu_add(gb, gb->cpu.reg.a, gb->cpu.reg.b, 0); return 4; }
static int op_81(gb_t *gb) { gb->cpu.reg.a = alu_add(gb, gb->cpu.reg.a, gb->cpu.reg.c, 0); return 4; }
static int op_82(gb_t *gb) { gb->cpu.reg.a = alu_add(gb, gb->cpu.reg.a, gb->cpu.reg.d, 0); return 4; }
static int op_83(gb_t *gb) { gb->cpu.reg.a = alu_add(gb, gb->cpu.reg.a, gb->cpu.reg.e, 0); return 4; }
static int op_84(gb_t *gb) { gb->cpu.reg.a = alu_add(gb, gb->cpu.reg.a, gb->cpu.reg.h, 0); return 4; }
static int op_85(gb_t *gb) { gb->cpu.reg.a = alu_add(gb, gb->cpu.reg.a, gb->cpu.reg.l, 0); return 4; }
static int op_86(gb_t *gb) { gb->cpu.reg.a = alu_add(gb, gb->cpu.reg.a, READ(gb, gb->cpu.reg.hl), 0); return 8; }
static int op_87(gb_t *gb) { gb->cpu.reg.a = alu_add(gb, gb->cpu.reg.a, gb->cpu.reg.a, 0); return 4; }
static int op_88(gb_t *gb) { gb->cpu.reg.a = alu_add(gb, gb->cpu.reg.a, gb->cpu.reg.b, !!(gb->cpu.reg.f & F_C)); return 4; }
static int op_89(gb_t *gb) { gb->cpu.reg.a = alu_add(gb, gb->cpu.reg.a, gb->cpu.reg.c, !!(gb->cpu.reg.f & F_C)); return 4; }
static int op_8A(gb_t *gb) { gb->cpu.reg.a = alu_add(gb, gb->cpu.reg.a, gb->cpu.reg.d, !!(gb->cpu.reg.f & F_C)); return 4; }
static int op_8B(gb_t *gb) { gb->cpu.reg.a = alu_add(gb, gb->cpu.reg.a, gb->cpu.reg.e, !!(gb->cpu.reg.f & F_C)); return 4; }
static int op_8C(gb_t *gb) { gb->cpu.reg.a = alu_add(gb, gb->cpu.reg.a, gb->cpu.reg.h, !!(gb->cpu.reg.f & F_C)); return 4; }
static int op_8D(gb_t *gb) { gb->cpu.reg.a = alu_add(gb, gb->cpu.reg.a, gb->cpu.reg.l, !!(gb->cpu.reg.f & F_C)); return 4; }
static int op_8E(gb_t *gb) { gb->cpu.reg.a = alu_add(gb, gb->cpu.reg.a, READ(gb, gb->cpu.reg.hl), !!(gb->cpu.reg.f & F_C)); return 8; }
static int op_8F(gb_t *gb) { gb->cpu.reg.a = alu_add(gb, gb->cpu.reg.a, gb->cpu.reg.a, !!(gb->cpu.reg.f & F_C)); return 4; }
static int op_90(gb_t *gb) { gb->cpu.reg.a = alu_sub(gb, gb->cpu.reg.a, gb->cpu.reg.b, 0); return 4; }
static int op_91(gb_t *gb) { gb->cpu.reg.a = alu_sub(gb, gb->cpu.reg.a, gb->cpu.reg.c, 0); return 4; }
static int op_92(gb_t *gb) { gb->cpu.reg.a = alu_sub(gb, gb->cpu.reg.a, gb->cpu.reg.d, 0); return 4; }
static int op_93(gb_t *gb) { gb->cpu.reg.a = alu_sub(gb, gb->cpu.reg.a, gb->cpu.reg.e, 0); return 4; }
static int op_94(gb_t *gb) { gb->cpu.reg.a = alu_sub(gb, gb->cpu.reg.a, gb->cpu.reg.h, 0); return 4; }
static int op_95(gb_t *gb) { gb->cpu.reg.a = alu_sub(gb, gb->cpu.reg.a, gb->cpu.reg.l, 0); return 4; }
static int op_96(gb_t *gb) { gb->cpu.reg.a = alu_sub(gb, gb->cpu.reg.a, READ(gb, gb->cpu.reg.hl), 0); return 8; }
static int op_97(gb_t *gb) { gb->cpu.reg.a = alu_sub(gb, gb->cpu.reg.a, gb->cpu.reg.a, 0); return 4; }
static int op_98(gb_t *gb) { gb->cpu.reg.a = alu_sub(gb, gb->cpu.reg.a, gb->cpu.reg.b, !!(gb->cpu.reg.f & F_C)); return 4; }
static int op_99(gb_t *gb) { gb->cpu.reg.a = alu_sub(gb, gb->cpu.reg.a, gb->cpu.reg.c, !!(gb->cpu.reg.f & F_C)); return 4; }
static int op_9A(gb_t *gb) { gb->cpu.reg.a = alu_sub(gb, gb->cpu.reg.a, gb->cpu.reg.d, !!(gb->cpu.reg.f & F_C)); return 4; }
static int op_9B(gb_t *gb) { gb->cpu.reg.a = alu_sub(gb, gb->cpu.reg.a, gb->cpu.reg.e, !!(gb->cpu.reg.f & F_C)); return 4; }
static int op_9C(gb_t *gb) { gb->cpu.reg.a = alu_sub(gb, gb->cpu.reg.a, gb->cpu.reg.h, !!(gb->cpu.reg.f & F_C)); return 4; }
static int op_9D(gb_t *gb) { gb->cpu.reg.a = alu_sub(gb, gb->cpu.reg.a, gb->cpu.reg.l, !!(gb->cpu.reg.f & F_C)); return 4; }
static int op_9E(gb_t *gb) { gb->cpu.reg.a = alu_sub(gb, gb->cpu.reg.a, READ(gb, gb->cpu.reg.hl), !!(gb->cpu.reg.f & F_C)); return 8; }
static int op_9F(gb_t *gb) { gb->cpu.reg.a = alu_sub(gb, gb->cpu.reg.a, gb->cpu.reg.a, !!(gb->cpu.reg.f & F_C)); return 4; }
static int op_A0(gb_t *gb) { gb->cpu.reg.a = alu_and(gb, gb->cpu.reg.a, gb->cpu.reg.b); return 4; }
static int op_A1(gb_t *gb) { gb->cpu.reg.a = alu_and(gb, gb->cpu.reg.a, gb->cpu.reg.c); return 4; }
static int op_A2(gb_t *gb) { gb->cpu.reg.a = alu_and(gb, gb->cpu.reg.a, gb->cpu.reg.d); return 4; }
static int op_A3(gb_t *gb) { gb->cpu.reg.a = alu_and(gb, gb->cpu.reg.a, gb->cpu.reg.e); return 4; }
static int op_A4(gb_t *gb) { gb->cpu.reg.a = alu_and(gb, gb->cpu.reg.a, gb->cpu.reg.h); return 4; }
static int op_A5(gb_t *gb) { gb->cpu.reg.a = alu_and(gb, gb->cpu.reg.a, gb->cpu.reg.l); return 4; }
static int op_A6(gb_t *gb) { gb->cpu.reg.a = alu_and(gb, gb->cpu.reg.a, READ(gb, gb->cpu.reg.hl)); return 8; }
static int op_A7(gb_t *gb) { gb->cpu.reg.a = alu_and(gb, gb->cpu.reg.a, gb->cpu.reg.a); return 4; }
static int op_A8(gb_t *gb) { gb->cpu.reg.a = alu_xor(gb, gb->cpu.reg.a, gb->cpu.reg.b); return 4; }
static int op_A9(gb_t *gb) { gb->cpu.reg.a = alu_xor(gb, gb->cpu.reg.a, gb->cpu.reg.c); return 4; }
static int op_AA(gb_t *gb) { gb->cpu.reg.a = alu_xor(gb, gb->cpu.reg.a, gb->cpu.reg.d); return 4; }
static int op_AB(gb_t *gb) { gb->cpu.reg.a = alu_xor(gb, gb->cpu.reg.a, gb->cpu.reg.e); return 4; }
static int op_AC(gb_t *gb) { gb->cpu.reg.a = alu_xor(gb, gb->cpu.reg.a, gb->cpu.reg.h); return 4; }
static int op_AD(gb_t *gb) { gb->cpu.reg.a = alu_xor(gb, gb->cpu.reg.a, gb->cpu.reg.l); return 4; }
static int op_AE(gb_t *gb) { gb->cpu.reg.a = alu_xor(gb, gb->cpu.reg.a, READ(gb, gb->cpu.reg.hl)); return 8; }
static int op_AF(gb_t *gb) { gb->cpu.reg.a = alu_xor(gb, gb->cpu.reg.a, gb->cpu.reg.a); return 4; }
static int op_B0(gb_t *gb) { gb->cpu.reg.a = alu_or(gb, gb->cpu.reg.a, gb->cpu.reg.b); return 4; }
static int op_B1(gb_t *gb) { gb->cpu.reg.a = alu_or(gb, gb->cpu.reg.a, gb->cpu.reg.c); return 4; }
static int op_B2(gb_t *gb) { gb->cpu.reg.a = alu_or(gb, gb->cpu.reg.a, gb->cpu.reg.d); return 4; }
static int op_B3(gb_t *gb) { gb->cpu.reg.a = alu_or(gb, gb->cpu.reg.a, gb->cpu.reg.e); return 4; }
static int op_B4(gb_t *gb) { gb->cpu.reg.a = alu_or(gb, gb->cpu.reg.a, gb->cpu.reg.h); return 4; }
static int op_B5(gb_t *gb) { gb->cpu.reg.a = alu_or(gb, gb->cpu.reg.a, gb->cpu.reg.l); return 4; }
static int op_B6(gb_t *gb) { gb->cpu.reg.a = alu_or(gb, gb->cpu.reg.a, READ(gb, gb->cpu.reg.hl)); return 8; }
static int op_B7(gb_t *gb) { gb->cpu.reg.a = alu_or(gb, gb->cpu.reg.a, gb->cpu.reg.a); return 4; }
static int op_B8(gb_t *gb) { alu_cp(gb, gb->cpu.reg.a, gb->cpu.reg.b); return 4; }
static int op_B9(gb_t *gb) { alu_cp(gb, gb->cpu.reg.a, gb->cpu.reg.c); return 4; }
static int op_BA(gb_t *gb) { alu_cp(gb, gb->cpu.reg.a, gb->cpu.reg.d); return 4; }
static int op_BB(gb_t *gb) { alu_cp(gb, gb->cpu.reg.a, gb->cpu.reg.e); return 4; }
static int op_BC(gb_t *gb) { alu_cp(gb, gb->cpu.reg.a, gb->cpu.reg.h); return 4; }
static int op_BD(gb_t *gb) { alu_cp(gb, gb->cpu.reg.a, gb->cpu.reg.l); return 4; }
static int op_BE(gb_t *gb) { alu_cp(gb, gb->cpu.reg.a, READ(gb, gb->cpu.reg.hl)); return 8; }
static int op_BF(gb_t *gb) { alu_cp(gb, gb->cpu.reg.a, gb->cpu.reg.a); return 4; }
/* ========== Main opcodes 0xC0 - 0xFF ========== */
static int op_C0(gb_t *gb) { if (!(gb->cpu.reg.f & F_Z)) { gb->cpu.reg.pc = pop_u16(gb); return 20; } return 8; } /* RET NZ */
static int op_C1(gb_t *gb) { gb->cpu.reg.bc = pop_u16(gb); return 12; } /* POP BC */
static int op_C2(gb_t *gb) { u16 a16 = fetch_word(gb); if (!(gb->cpu.reg.f & F_Z)) { gb->cpu.reg.pc = a16; return 16; } return 12; } /* JP NZ,a16 */
static int op_C3(gb_t *gb) { gb->cpu.reg.pc = fetch_word(gb); return 16; } /* JP a16 */
static int op_C4(gb_t *gb) { u16 a16 = fetch_word(gb); if (!(gb->cpu.reg.f & F_Z)) { push_u16(gb, gb->cpu.reg.pc); gb->cpu.reg.pc = a16; return 24; } return 12; } /* CALL NZ,a16 */
static int op_C5(gb_t *gb) { push_u16(gb, gb->cpu.reg.bc); return 16; } /* PUSH BC */
static int op_C6(gb_t *gb) { gb->cpu.reg.a = alu_add(gb, gb->cpu.reg.a, fetch_byte(gb), 0); return 8; } /* ADD A,d8 */
static int op_C7(gb_t *gb) { push_u16(gb, gb->cpu.reg.pc); gb->cpu.reg.pc = 0x00; return 16; } /* RST 00H */
static int op_C8(gb_t *gb) { if (gb->cpu.reg.f & F_Z) { gb->cpu.reg.pc = pop_u16(gb); return 20; } return 8; } /* RET Z */
static int op_C9(gb_t *gb) { gb->cpu.reg.pc = pop_u16(gb); return 16; } /* RET */
static int op_CA(gb_t *gb) { u16 a16 = fetch_word(gb); if (gb->cpu.reg.f & F_Z) { gb->cpu.reg.pc = a16; return 16; } return 12; } /* JP Z,a16 */
static int op_CB(gb_t *gb) {
    u8 op = fetch_byte(gb);
    int cycles = cb_ops[op](gb);
    return cycles;
} /* CB prefix */
static int op_CC(gb_t *gb) { u16 a16 = fetch_word(gb); if (gb->cpu.reg.f & F_Z) { push_u16(gb, gb->cpu.reg.pc); gb->cpu.reg.pc = a16; return 24; } return 12; } /* CALL Z,a16 */
static int op_CD(gb_t *gb) { u16 a16 = fetch_word(gb); push_u16(gb, gb->cpu.reg.pc); gb->cpu.reg.pc = a16; return 24; } /* CALL a16 */
static int op_CE(gb_t *gb) { gb->cpu.reg.a = alu_add(gb, gb->cpu.reg.a, fetch_byte(gb), !!(gb->cpu.reg.f & F_C)); return 8; } /* ADC A,d8 */
static int op_CF(gb_t *gb) { push_u16(gb, gb->cpu.reg.pc); gb->cpu.reg.pc = 0x08; return 16; } /* RST 08H */
static int op_D0(gb_t *gb) { if (!(gb->cpu.reg.f & F_C)) { gb->cpu.reg.pc = pop_u16(gb); return 20; } return 8; } /* RET NC */
static int op_D1(gb_t *gb) { gb->cpu.reg.de = pop_u16(gb); return 12; } /* POP DE */
static int op_D2(gb_t *gb) { u16 a16 = fetch_word(gb); if (!(gb->cpu.reg.f & F_C)) { gb->cpu.reg.pc = a16; return 16; } return 12; } /* JP NC,a16 */
static int op_D3(gb_t *gb) { (void)gb; return 4; } /* ILLEGAL: locks up on hardware */
static int op_D4(gb_t *gb) { u16 a16 = fetch_word(gb); if (!(gb->cpu.reg.f & F_C)) { push_u16(gb, gb->cpu.reg.pc); gb->cpu.reg.pc = a16; return 24; } return 12; } /* CALL NC,a16 */
static int op_D5(gb_t *gb) { push_u16(gb, gb->cpu.reg.de); return 16; } /* PUSH DE */
static int op_D6(gb_t *gb) { gb->cpu.reg.a = alu_sub(gb, gb->cpu.reg.a, fetch_byte(gb), 0); return 8; } /* SUB d8 */
static int op_D7(gb_t *gb) { push_u16(gb, gb->cpu.reg.pc); gb->cpu.reg.pc = 0x10; return 16; } /* RST 10H */
static int op_D8(gb_t *gb) { if (gb->cpu.reg.f & F_C) { gb->cpu.reg.pc = pop_u16(gb); return 20; } return 8; } /* RET C */
static int op_D9(gb_t *gb) { gb->cpu.reg.pc = pop_u16(gb); gb->cpu.ime = 1; gb->cpu.ime_pending = 0; return 16; } /* RETI */
static int op_DA(gb_t *gb) { u16 a16 = fetch_word(gb); if (gb->cpu.reg.f & F_C) { gb->cpu.reg.pc = a16; return 16; } return 12; } /* JP C,a16 */
static int op_DB(gb_t *gb) { (void)gb; return 4; } /* ILLEGAL: locks up on hardware */
static int op_DC(gb_t *gb) { u16 a16 = fetch_word(gb); if (gb->cpu.reg.f & F_C) { push_u16(gb, gb->cpu.reg.pc); gb->cpu.reg.pc = a16; return 24; } return 12; } /* CALL C,a16 */
static int op_DD(gb_t *gb) { (void)gb; return 4; } /* ILLEGAL: locks up on hardware */
static int op_DE(gb_t *gb) { gb->cpu.reg.a = alu_sub(gb, gb->cpu.reg.a, fetch_byte(gb), !!(gb->cpu.reg.f & F_C)); return 8; } /* SBC A,d8 */
static int op_DF(gb_t *gb) { push_u16(gb, gb->cpu.reg.pc); gb->cpu.reg.pc = 0x18; return 16; } /* RST 18H */
static int op_E0(gb_t *gb) { WRITE(gb, 0xFF00 + fetch_byte(gb), gb->cpu.reg.a); return 12; } /* LDH (a8),A */
static int op_E1(gb_t *gb) { gb->cpu.reg.hl = pop_u16(gb); return 12; } /* POP HL */
static int op_E2(gb_t *gb) { WRITE(gb, 0xFF00 + gb->cpu.reg.c, gb->cpu.reg.a); return 8; } /* LD (C),A */
static int op_E3(gb_t *gb) { (void)gb; return 4; } /* ILLEGAL: locks up on hardware */
static int op_E4(gb_t *gb) { (void)gb; return 4; } /* ILLEGAL: locks up on hardware */
static int op_E5(gb_t *gb) { push_u16(gb, gb->cpu.reg.hl); return 16; } /* PUSH HL */
static int op_E6(gb_t *gb) { gb->cpu.reg.a = alu_and(gb, gb->cpu.reg.a, fetch_byte(gb)); return 8; } /* AND d8 */
static int op_E7(gb_t *gb) { push_u16(gb, gb->cpu.reg.pc); gb->cpu.reg.pc = 0x20; return 16; } /* RST 20H */
static int op_E8(gb_t *gb) { add_sp_r8(gb); return 16; } /* ADD SP,r8 */
static int op_E9(gb_t *gb) { gb->cpu.reg.pc = gb->cpu.reg.hl; return 4; } /* JP (HL) */
static int op_EA(gb_t *gb) { WRITE(gb, fetch_word(gb), gb->cpu.reg.a); return 16; } /* LD (a16),A */
static int op_EB(gb_t *gb) { (void)gb; return 4; } /* ILLEGAL: locks up on hardware */
static int op_EC(gb_t *gb) { (void)gb; return 4; } /* ILLEGAL: locks up on hardware */
static int op_ED(gb_t *gb) { (void)gb; return 4; } /* ILLEGAL: locks up on hardware */
static int op_EE(gb_t *gb) { gb->cpu.reg.a = alu_xor(gb, gb->cpu.reg.a, fetch_byte(gb)); return 8; } /* XOR d8 */
static int op_EF(gb_t *gb) { push_u16(gb, gb->cpu.reg.pc); gb->cpu.reg.pc = 0x28; return 16; } /* RST 28H */
static int op_F0(gb_t *gb) { gb->cpu.reg.a = READ(gb, 0xFF00 + fetch_byte(gb)); return 12; } /* LDH A,(a8) */
static int op_F1(gb_t *gb) { gb->cpu.reg.af = pop_u16(gb); gb->cpu.reg.f &= 0xF0; return 12; } /* POP AF */
static int op_F2(gb_t *gb) { gb->cpu.reg.a = READ(gb, 0xFF00 + gb->cpu.reg.c); return 8; } /* LD A,(C) */
static int op_F3(gb_t *gb) { gb->cpu.ime = 0; gb->cpu.ime_pending = 0; return 4; } /* DI */
static int op_F4(gb_t *gb) { (void)gb; return 4; } /* ILLEGAL: locks up on hardware */
static int op_F5(gb_t *gb) { push_u16(gb, gb->cpu.reg.af); return 16; } /* PUSH AF */
static int op_F6(gb_t *gb) { gb->cpu.reg.a = alu_or(gb, gb->cpu.reg.a, fetch_byte(gb)); return 8; } /* OR d8 */
static int op_F7(gb_t *gb) { push_u16(gb, gb->cpu.reg.pc); gb->cpu.reg.pc = 0x30; return 16; } /* RST 30H */
static int op_F8(gb_t *gb) {
    s8 r8 = (s8)fetch_byte(gb);
    gb->cpu.reg.hl = gb->cpu.reg.sp + (u16)(s16)r8;
    SET_Z(&gb->cpu, 0); SET_N(&gb->cpu, 0);
    SET_H(&gb->cpu, ((gb->cpu.reg.sp & 0xF) + ((u8)r8 & 0xF)) > 0xF);
    SET_C(&gb->cpu, ((gb->cpu.reg.sp & 0xFF) + ((u8)r8 & 0xFF)) > 0xFF);
    return 12;
} /* LD HL,SP+r8 */
static int op_F9(gb_t *gb) { gb->cpu.reg.sp = gb->cpu.reg.hl; return 8; } /* LD SP,HL */
static int op_FA(gb_t *gb) { gb->cpu.reg.a = READ(gb, fetch_word(gb)); return 16; } /* LD A,(a16) */
static int op_FB(gb_t *gb) { gb->cpu.ime_pending = 1; return 4; } /* EI */
static int op_FC(gb_t *gb) { (void)gb; return 4; } /* ILLEGAL: locks up on hardware */
static int op_FD(gb_t *gb) { (void)gb; return 4; } /* ILLEGAL: locks up on hardware */
static int op_FE(gb_t *gb) { alu_cp(gb, gb->cpu.reg.a, fetch_byte(gb)); return 8; } /* CP d8 */
static int op_FF(gb_t *gb) { push_u16(gb, gb->cpu.reg.pc); gb->cpu.reg.pc = 0x38; return 16; } /* RST 38H */
/* ========== CB-prefixed opcodes 0x00 - 0x3F (rotates/shifts) ========== */
/* RLC r: 0x00-0x07 */
static int cb_00(gb_t *gb) { gb->cpu.reg.b = cb_rotate_left(gb, gb->cpu.reg.b); return 8; }
static int cb_01(gb_t *gb) { gb->cpu.reg.c = cb_rotate_left(gb, gb->cpu.reg.c); return 8; }
static int cb_02(gb_t *gb) { gb->cpu.reg.d = cb_rotate_left(gb, gb->cpu.reg.d); return 8; }
static int cb_03(gb_t *gb) { gb->cpu.reg.e = cb_rotate_left(gb, gb->cpu.reg.e); return 8; }
static int cb_04(gb_t *gb) { gb->cpu.reg.h = cb_rotate_left(gb, gb->cpu.reg.h); return 8; }
static int cb_05(gb_t *gb) { gb->cpu.reg.l = cb_rotate_left(gb, gb->cpu.reg.l); return 8; }
static int cb_06(gb_t *gb) { WRITE(gb, gb->cpu.reg.hl, cb_rotate_left(gb, READ(gb, gb->cpu.reg.hl))); return 16; }
static int cb_07(gb_t *gb) { gb->cpu.reg.a = cb_rotate_left(gb, gb->cpu.reg.a); return 8; }
/* RRC r: 0x08-0x0F */
static int cb_08(gb_t *gb) { gb->cpu.reg.b = cb_rotate_right(gb, gb->cpu.reg.b); return 8; }
static int cb_09(gb_t *gb) { gb->cpu.reg.c = cb_rotate_right(gb, gb->cpu.reg.c); return 8; }
static int cb_0A(gb_t *gb) { gb->cpu.reg.d = cb_rotate_right(gb, gb->cpu.reg.d); return 8; }
static int cb_0B(gb_t *gb) { gb->cpu.reg.e = cb_rotate_right(gb, gb->cpu.reg.e); return 8; }
static int cb_0C(gb_t *gb) { gb->cpu.reg.h = cb_rotate_right(gb, gb->cpu.reg.h); return 8; }
static int cb_0D(gb_t *gb) { gb->cpu.reg.l = cb_rotate_right(gb, gb->cpu.reg.l); return 8; }
static int cb_0E(gb_t *gb) { WRITE(gb, gb->cpu.reg.hl, cb_rotate_right(gb, READ(gb, gb->cpu.reg.hl))); return 16; }
static int cb_0F(gb_t *gb) { gb->cpu.reg.a = cb_rotate_right(gb, gb->cpu.reg.a); return 8; }
/* RL r: 0x10-0x17 */
static int cb_10(gb_t *gb) { gb->cpu.reg.b = cb_rotate_left_carry(gb, gb->cpu.reg.b); return 8; }
static int cb_11(gb_t *gb) { gb->cpu.reg.c = cb_rotate_left_carry(gb, gb->cpu.reg.c); return 8; }
static int cb_12(gb_t *gb) { gb->cpu.reg.d = cb_rotate_left_carry(gb, gb->cpu.reg.d); return 8; }
static int cb_13(gb_t *gb) { gb->cpu.reg.e = cb_rotate_left_carry(gb, gb->cpu.reg.e); return 8; }
static int cb_14(gb_t *gb) { gb->cpu.reg.h = cb_rotate_left_carry(gb, gb->cpu.reg.h); return 8; }
static int cb_15(gb_t *gb) { gb->cpu.reg.l = cb_rotate_left_carry(gb, gb->cpu.reg.l); return 8; }
static int cb_16(gb_t *gb) { WRITE(gb, gb->cpu.reg.hl, cb_rotate_left_carry(gb, READ(gb, gb->cpu.reg.hl))); return 16; }
static int cb_17(gb_t *gb) { gb->cpu.reg.a = cb_rotate_left_carry(gb, gb->cpu.reg.a); return 8; }
/* RR r: 0x18-0x1F */
static int cb_18(gb_t *gb) { gb->cpu.reg.b = cb_rotate_right_carry(gb, gb->cpu.reg.b); return 8; }
static int cb_19(gb_t *gb) { gb->cpu.reg.c = cb_rotate_right_carry(gb, gb->cpu.reg.c); return 8; }
static int cb_1A(gb_t *gb) { gb->cpu.reg.d = cb_rotate_right_carry(gb, gb->cpu.reg.d); return 8; }
static int cb_1B(gb_t *gb) { gb->cpu.reg.e = cb_rotate_right_carry(gb, gb->cpu.reg.e); return 8; }
static int cb_1C(gb_t *gb) { gb->cpu.reg.h = cb_rotate_right_carry(gb, gb->cpu.reg.h); return 8; }
static int cb_1D(gb_t *gb) { gb->cpu.reg.l = cb_rotate_right_carry(gb, gb->cpu.reg.l); return 8; }
static int cb_1E(gb_t *gb) { WRITE(gb, gb->cpu.reg.hl, cb_rotate_right_carry(gb, READ(gb, gb->cpu.reg.hl))); return 16; }
static int cb_1F(gb_t *gb) { gb->cpu.reg.a = cb_rotate_right_carry(gb, gb->cpu.reg.a); return 8; }
/* SLA r: 0x20-0x27 */
static int cb_20(gb_t *gb) { gb->cpu.reg.b = shift_left_arith(gb, gb->cpu.reg.b); return 8; }
static int cb_21(gb_t *gb) { gb->cpu.reg.c = shift_left_arith(gb, gb->cpu.reg.c); return 8; }
static int cb_22(gb_t *gb) { gb->cpu.reg.d = shift_left_arith(gb, gb->cpu.reg.d); return 8; }
static int cb_23(gb_t *gb) { gb->cpu.reg.e = shift_left_arith(gb, gb->cpu.reg.e); return 8; }
static int cb_24(gb_t *gb) { gb->cpu.reg.h = shift_left_arith(gb, gb->cpu.reg.h); return 8; }
static int cb_25(gb_t *gb) { gb->cpu.reg.l = shift_left_arith(gb, gb->cpu.reg.l); return 8; }
static int cb_26(gb_t *gb) { WRITE(gb, gb->cpu.reg.hl, shift_left_arith(gb, READ(gb, gb->cpu.reg.hl))); return 16; }
static int cb_27(gb_t *gb) { gb->cpu.reg.a = shift_left_arith(gb, gb->cpu.reg.a); return 8; }
/* SRA r: 0x28-0x2F */
static int cb_28(gb_t *gb) { gb->cpu.reg.b = shift_right_arith(gb, gb->cpu.reg.b); return 8; }
static int cb_29(gb_t *gb) { gb->cpu.reg.c = shift_right_arith(gb, gb->cpu.reg.c); return 8; }
static int cb_2A(gb_t *gb) { gb->cpu.reg.d = shift_right_arith(gb, gb->cpu.reg.d); return 8; }
static int cb_2B(gb_t *gb) { gb->cpu.reg.e = shift_right_arith(gb, gb->cpu.reg.e); return 8; }
static int cb_2C(gb_t *gb) { gb->cpu.reg.h = shift_right_arith(gb, gb->cpu.reg.h); return 8; }
static int cb_2D(gb_t *gb) { gb->cpu.reg.l = shift_right_arith(gb, gb->cpu.reg.l); return 8; }
static int cb_2E(gb_t *gb) { WRITE(gb, gb->cpu.reg.hl, shift_right_arith(gb, READ(gb, gb->cpu.reg.hl))); return 16; }
static int cb_2F(gb_t *gb) { gb->cpu.reg.a = shift_right_arith(gb, gb->cpu.reg.a); return 8; }
/* SWAP r: 0x30-0x37 */
static int cb_30(gb_t *gb) { gb->cpu.reg.b = swap_nibbles(gb, gb->cpu.reg.b); return 8; }
static int cb_31(gb_t *gb) { gb->cpu.reg.c = swap_nibbles(gb, gb->cpu.reg.c); return 8; }
static int cb_32(gb_t *gb) { gb->cpu.reg.d = swap_nibbles(gb, gb->cpu.reg.d); return 8; }
static int cb_33(gb_t *gb) { gb->cpu.reg.e = swap_nibbles(gb, gb->cpu.reg.e); return 8; }
static int cb_34(gb_t *gb) { gb->cpu.reg.h = swap_nibbles(gb, gb->cpu.reg.h); return 8; }
static int cb_35(gb_t *gb) { gb->cpu.reg.l = swap_nibbles(gb, gb->cpu.reg.l); return 8; }
static int cb_36(gb_t *gb) { WRITE(gb, gb->cpu.reg.hl, swap_nibbles(gb, READ(gb, gb->cpu.reg.hl))); return 16; }
static int cb_37(gb_t *gb) { gb->cpu.reg.a = swap_nibbles(gb, gb->cpu.reg.a); return 8; }
/* SRL r: 0x38-0x3F */
static int cb_38(gb_t *gb) { gb->cpu.reg.b = shift_right_logical(gb, gb->cpu.reg.b); return 8; }
static int cb_39(gb_t *gb) { gb->cpu.reg.c = shift_right_logical(gb, gb->cpu.reg.c); return 8; }
static int cb_3A(gb_t *gb) { gb->cpu.reg.d = shift_right_logical(gb, gb->cpu.reg.d); return 8; }
static int cb_3B(gb_t *gb) { gb->cpu.reg.e = shift_right_logical(gb, gb->cpu.reg.e); return 8; }
static int cb_3C(gb_t *gb) { gb->cpu.reg.h = shift_right_logical(gb, gb->cpu.reg.h); return 8; }
static int cb_3D(gb_t *gb) { gb->cpu.reg.l = shift_right_logical(gb, gb->cpu.reg.l); return 8; }
static int cb_3E(gb_t *gb) { WRITE(gb, gb->cpu.reg.hl, shift_right_logical(gb, READ(gb, gb->cpu.reg.hl))); return 16; }
static int cb_3F(gb_t *gb) { gb->cpu.reg.a = shift_right_logical(gb, gb->cpu.reg.a); return 8; }
/* ========== CB BIT/RES/SET 0x40-0xFF ========== */
/* Helper for BIT b,r */
#define BIT_OP(gb, bit, val) do { SET_Z(&gb->cpu, !((val) & (1 << (bit)))); SET_N(&gb->cpu, 0); SET_H(&gb->cpu, 1); } while(0)
/* BIT b,(HL) takes 12 cycles, BIT b,r takes 8 */
/* RES and SET for (HL) take 16 cycles, for r take 8 */

/* BIT 0,r: 0x40-0x47 */
static int cb_40(gb_t *gb) { BIT_OP(gb, 0, gb->cpu.reg.b); return 8; }
static int cb_41(gb_t *gb) { BIT_OP(gb, 0, gb->cpu.reg.c); return 8; }
static int cb_42(gb_t *gb) { BIT_OP(gb, 0, gb->cpu.reg.d); return 8; }
static int cb_43(gb_t *gb) { BIT_OP(gb, 0, gb->cpu.reg.e); return 8; }
static int cb_44(gb_t *gb) { BIT_OP(gb, 0, gb->cpu.reg.h); return 8; }
static int cb_45(gb_t *gb) { BIT_OP(gb, 0, gb->cpu.reg.l); return 8; }
static int cb_46(gb_t *gb) { BIT_OP(gb, 0, READ(gb, gb->cpu.reg.hl)); return 12; }
static int cb_47(gb_t *gb) { BIT_OP(gb, 0, gb->cpu.reg.a); return 8; }
/* BIT 1,r: 0x48-0x4F */
static int cb_48(gb_t *gb) { BIT_OP(gb, 1, gb->cpu.reg.b); return 8; }
static int cb_49(gb_t *gb) { BIT_OP(gb, 1, gb->cpu.reg.c); return 8; }
static int cb_4A(gb_t *gb) { BIT_OP(gb, 1, gb->cpu.reg.d); return 8; }
static int cb_4B(gb_t *gb) { BIT_OP(gb, 1, gb->cpu.reg.e); return 8; }
static int cb_4C(gb_t *gb) { BIT_OP(gb, 1, gb->cpu.reg.h); return 8; }
static int cb_4D(gb_t *gb) { BIT_OP(gb, 1, gb->cpu.reg.l); return 8; }
static int cb_4E(gb_t *gb) { BIT_OP(gb, 1, READ(gb, gb->cpu.reg.hl)); return 12; }
static int cb_4F(gb_t *gb) { BIT_OP(gb, 1, gb->cpu.reg.a); return 8; }
/* BIT 2,r: 0x50-0x57 */
static int cb_50(gb_t *gb) { BIT_OP(gb, 2, gb->cpu.reg.b); return 8; }
static int cb_51(gb_t *gb) { BIT_OP(gb, 2, gb->cpu.reg.c); return 8; }
static int cb_52(gb_t *gb) { BIT_OP(gb, 2, gb->cpu.reg.d); return 8; }
static int cb_53(gb_t *gb) { BIT_OP(gb, 2, gb->cpu.reg.e); return 8; }
static int cb_54(gb_t *gb) { BIT_OP(gb, 2, gb->cpu.reg.h); return 8; }
static int cb_55(gb_t *gb) { BIT_OP(gb, 2, gb->cpu.reg.l); return 8; }
static int cb_56(gb_t *gb) { BIT_OP(gb, 2, READ(gb, gb->cpu.reg.hl)); return 12; }
static int cb_57(gb_t *gb) { BIT_OP(gb, 2, gb->cpu.reg.a); return 8; }
/* BIT 3,r: 0x58-0x5F */
static int cb_58(gb_t *gb) { BIT_OP(gb, 3, gb->cpu.reg.b); return 8; }
static int cb_59(gb_t *gb) { BIT_OP(gb, 3, gb->cpu.reg.c); return 8; }
static int cb_5A(gb_t *gb) { BIT_OP(gb, 3, gb->cpu.reg.d); return 8; }
static int cb_5B(gb_t *gb) { BIT_OP(gb, 3, gb->cpu.reg.e); return 8; }
static int cb_5C(gb_t *gb) { BIT_OP(gb, 3, gb->cpu.reg.h); return 8; }
static int cb_5D(gb_t *gb) { BIT_OP(gb, 3, gb->cpu.reg.l); return 8; }
static int cb_5E(gb_t *gb) { BIT_OP(gb, 3, READ(gb, gb->cpu.reg.hl)); return 12; }
static int cb_5F(gb_t *gb) { BIT_OP(gb, 3, gb->cpu.reg.a); return 8; }
/* BIT 4,r: 0x60-0x67 */
static int cb_60(gb_t *gb) { BIT_OP(gb, 4, gb->cpu.reg.b); return 8; }
static int cb_61(gb_t *gb) { BIT_OP(gb, 4, gb->cpu.reg.c); return 8; }
static int cb_62(gb_t *gb) { BIT_OP(gb, 4, gb->cpu.reg.d); return 8; }
static int cb_63(gb_t *gb) { BIT_OP(gb, 4, gb->cpu.reg.e); return 8; }
static int cb_64(gb_t *gb) { BIT_OP(gb, 4, gb->cpu.reg.h); return 8; }
static int cb_65(gb_t *gb) { BIT_OP(gb, 4, gb->cpu.reg.l); return 8; }
static int cb_66(gb_t *gb) { BIT_OP(gb, 4, READ(gb, gb->cpu.reg.hl)); return 12; }
static int cb_67(gb_t *gb) { BIT_OP(gb, 4, gb->cpu.reg.a); return 8; }
/* BIT 5,r: 0x68-0x6F */
static int cb_68(gb_t *gb) { BIT_OP(gb, 5, gb->cpu.reg.b); return 8; }
static int cb_69(gb_t *gb) { BIT_OP(gb, 5, gb->cpu.reg.c); return 8; }
static int cb_6A(gb_t *gb) { BIT_OP(gb, 5, gb->cpu.reg.d); return 8; }
static int cb_6B(gb_t *gb) { BIT_OP(gb, 5, gb->cpu.reg.e); return 8; }
static int cb_6C(gb_t *gb) { BIT_OP(gb, 5, gb->cpu.reg.h); return 8; }
static int cb_6D(gb_t *gb) { BIT_OP(gb, 5, gb->cpu.reg.l); return 8; }
static int cb_6E(gb_t *gb) { BIT_OP(gb, 5, READ(gb, gb->cpu.reg.hl)); return 12; }
static int cb_6F(gb_t *gb) { BIT_OP(gb, 5, gb->cpu.reg.a); return 8; }
/* BIT 6,r: 0x70-0x77 */
static int cb_70(gb_t *gb) { BIT_OP(gb, 6, gb->cpu.reg.b); return 8; }
static int cb_71(gb_t *gb) { BIT_OP(gb, 6, gb->cpu.reg.c); return 8; }
static int cb_72(gb_t *gb) { BIT_OP(gb, 6, gb->cpu.reg.d); return 8; }
static int cb_73(gb_t *gb) { BIT_OP(gb, 6, gb->cpu.reg.e); return 8; }
static int cb_74(gb_t *gb) { BIT_OP(gb, 6, gb->cpu.reg.h); return 8; }
static int cb_75(gb_t *gb) { BIT_OP(gb, 6, gb->cpu.reg.l); return 8; }
static int cb_76(gb_t *gb) { BIT_OP(gb, 6, READ(gb, gb->cpu.reg.hl)); return 12; }
static int cb_77(gb_t *gb) { BIT_OP(gb, 6, gb->cpu.reg.a); return 8; }
/* BIT 7,r: 0x78-0x7F */
static int cb_78(gb_t *gb) { BIT_OP(gb, 7, gb->cpu.reg.b); return 8; }
static int cb_79(gb_t *gb) { BIT_OP(gb, 7, gb->cpu.reg.c); return 8; }
static int cb_7A(gb_t *gb) { BIT_OP(gb, 7, gb->cpu.reg.d); return 8; }
static int cb_7B(gb_t *gb) { BIT_OP(gb, 7, gb->cpu.reg.e); return 8; }
static int cb_7C(gb_t *gb) { BIT_OP(gb, 7, gb->cpu.reg.h); return 8; }
static int cb_7D(gb_t *gb) { BIT_OP(gb, 7, gb->cpu.reg.l); return 8; }
static int cb_7E(gb_t *gb) { BIT_OP(gb, 7, READ(gb, gb->cpu.reg.hl)); return 12; }
static int cb_7F(gb_t *gb) { BIT_OP(gb, 7, gb->cpu.reg.a); return 8; }
/* RES 0,r: 0x80-0x87 */
static int cb_80(gb_t *gb) { gb->cpu.reg.b &= ~(1 << 0); return 8; }
static int cb_81(gb_t *gb) { gb->cpu.reg.c &= ~(1 << 0); return 8; }
static int cb_82(gb_t *gb) { gb->cpu.reg.d &= ~(1 << 0); return 8; }
static int cb_83(gb_t *gb) { gb->cpu.reg.e &= ~(1 << 0); return 8; }
static int cb_84(gb_t *gb) { gb->cpu.reg.h &= ~(1 << 0); return 8; }
static int cb_85(gb_t *gb) { gb->cpu.reg.l &= ~(1 << 0); return 8; }
static int cb_86(gb_t *gb) { u8 v = READ(gb, gb->cpu.reg.hl); v &= ~(1 << 0); WRITE(gb, gb->cpu.reg.hl, v); return 16; }
static int cb_87(gb_t *gb) { gb->cpu.reg.a &= ~(1 << 0); return 8; }
/* RES 1,r: 0x88-0x8F */
static int cb_88(gb_t *gb) { gb->cpu.reg.b &= ~(1 << 1); return 8; }
static int cb_89(gb_t *gb) { gb->cpu.reg.c &= ~(1 << 1); return 8; }
static int cb_8A(gb_t *gb) { gb->cpu.reg.d &= ~(1 << 1); return 8; }
static int cb_8B(gb_t *gb) { gb->cpu.reg.e &= ~(1 << 1); return 8; }
static int cb_8C(gb_t *gb) { gb->cpu.reg.h &= ~(1 << 1); return 8; }
static int cb_8D(gb_t *gb) { gb->cpu.reg.l &= ~(1 << 1); return 8; }
static int cb_8E(gb_t *gb) { u8 v = READ(gb, gb->cpu.reg.hl); v &= ~(1 << 1); WRITE(gb, gb->cpu.reg.hl, v); return 16; }
static int cb_8F(gb_t *gb) { gb->cpu.reg.a &= ~(1 << 1); return 8; }
/* RES 2,r: 0x90-0x97 */
static int cb_90(gb_t *gb) { gb->cpu.reg.b &= ~(1 << 2); return 8; }
static int cb_91(gb_t *gb) { gb->cpu.reg.c &= ~(1 << 2); return 8; }
static int cb_92(gb_t *gb) { gb->cpu.reg.d &= ~(1 << 2); return 8; }
static int cb_93(gb_t *gb) { gb->cpu.reg.e &= ~(1 << 2); return 8; }
static int cb_94(gb_t *gb) { gb->cpu.reg.h &= ~(1 << 2); return 8; }
static int cb_95(gb_t *gb) { gb->cpu.reg.l &= ~(1 << 2); return 8; }
static int cb_96(gb_t *gb) { u8 v = READ(gb, gb->cpu.reg.hl); v &= ~(1 << 2); WRITE(gb, gb->cpu.reg.hl, v); return 16; }
static int cb_97(gb_t *gb) { gb->cpu.reg.a &= ~(1 << 2); return 8; }
/* RES 3,r: 0x98-0x9F */
static int cb_98(gb_t *gb) { gb->cpu.reg.b &= ~(1 << 3); return 8; }
static int cb_99(gb_t *gb) { gb->cpu.reg.c &= ~(1 << 3); return 8; }
static int cb_9A(gb_t *gb) { gb->cpu.reg.d &= ~(1 << 3); return 8; }
static int cb_9B(gb_t *gb) { gb->cpu.reg.e &= ~(1 << 3); return 8; }
static int cb_9C(gb_t *gb) { gb->cpu.reg.h &= ~(1 << 3); return 8; }
static int cb_9D(gb_t *gb) { gb->cpu.reg.l &= ~(1 << 3); return 8; }
static int cb_9E(gb_t *gb) { u8 v = READ(gb, gb->cpu.reg.hl); v &= ~(1 << 3); WRITE(gb, gb->cpu.reg.hl, v); return 16; }
static int cb_9F(gb_t *gb) { gb->cpu.reg.a &= ~(1 << 3); return 8; }
/* RES 4,r: 0xA0-0xA7 */
static int cb_A0(gb_t *gb) { gb->cpu.reg.b &= ~(1 << 4); return 8; }
static int cb_A1(gb_t *gb) { gb->cpu.reg.c &= ~(1 << 4); return 8; }
static int cb_A2(gb_t *gb) { gb->cpu.reg.d &= ~(1 << 4); return 8; }
static int cb_A3(gb_t *gb) { gb->cpu.reg.e &= ~(1 << 4); return 8; }
static int cb_A4(gb_t *gb) { gb->cpu.reg.h &= ~(1 << 4); return 8; }
static int cb_A5(gb_t *gb) { gb->cpu.reg.l &= ~(1 << 4); return 8; }
static int cb_A6(gb_t *gb) { u8 v = READ(gb, gb->cpu.reg.hl); v &= ~(1 << 4); WRITE(gb, gb->cpu.reg.hl, v); return 16; }
static int cb_A7(gb_t *gb) { gb->cpu.reg.a &= ~(1 << 4); return 8; }
/* RES 5,r: 0xA8-0xAF */
static int cb_A8(gb_t *gb) { gb->cpu.reg.b &= ~(1 << 5); return 8; }
static int cb_A9(gb_t *gb) { gb->cpu.reg.c &= ~(1 << 5); return 8; }
static int cb_AA(gb_t *gb) { gb->cpu.reg.d &= ~(1 << 5); return 8; }
static int cb_AB(gb_t *gb) { gb->cpu.reg.e &= ~(1 << 5); return 8; }
static int cb_AC(gb_t *gb) { gb->cpu.reg.h &= ~(1 << 5); return 8; }
static int cb_AD(gb_t *gb) { gb->cpu.reg.l &= ~(1 << 5); return 8; }
static int cb_AE(gb_t *gb) { u8 v = READ(gb, gb->cpu.reg.hl); v &= ~(1 << 5); WRITE(gb, gb->cpu.reg.hl, v); return 16; }
static int cb_AF(gb_t *gb) { gb->cpu.reg.a &= ~(1 << 5); return 8; }
/* RES 6,r: 0xB0-0xB7 */
static int cb_B0(gb_t *gb) { gb->cpu.reg.b &= ~(1 << 6); return 8; }
static int cb_B1(gb_t *gb) { gb->cpu.reg.c &= ~(1 << 6); return 8; }
static int cb_B2(gb_t *gb) { gb->cpu.reg.d &= ~(1 << 6); return 8; }
static int cb_B3(gb_t *gb) { gb->cpu.reg.e &= ~(1 << 6); return 8; }
static int cb_B4(gb_t *gb) { gb->cpu.reg.h &= ~(1 << 6); return 8; }
static int cb_B5(gb_t *gb) { gb->cpu.reg.l &= ~(1 << 6); return 8; }
static int cb_B6(gb_t *gb) { u8 v = READ(gb, gb->cpu.reg.hl); v &= ~(1 << 6); WRITE(gb, gb->cpu.reg.hl, v); return 16; }
static int cb_B7(gb_t *gb) { gb->cpu.reg.a &= ~(1 << 6); return 8; }
/* RES 7,r: 0xB8-0xBF */
static int cb_B8(gb_t *gb) { gb->cpu.reg.b &= ~(1 << 7); return 8; }
static int cb_B9(gb_t *gb) { gb->cpu.reg.c &= ~(1 << 7); return 8; }
static int cb_BA(gb_t *gb) { gb->cpu.reg.d &= ~(1 << 7); return 8; }
static int cb_BB(gb_t *gb) { gb->cpu.reg.e &= ~(1 << 7); return 8; }
static int cb_BC(gb_t *gb) { gb->cpu.reg.h &= ~(1 << 7); return 8; }
static int cb_BD(gb_t *gb) { gb->cpu.reg.l &= ~(1 << 7); return 8; }
static int cb_BE(gb_t *gb) { u8 v = READ(gb, gb->cpu.reg.hl); v &= ~(1 << 7); WRITE(gb, gb->cpu.reg.hl, v); return 16; }
static int cb_BF(gb_t *gb) { gb->cpu.reg.a &= ~(1 << 7); return 8; }
/* SET 0,r: 0xC0-0xC7 */
static int cb_C0(gb_t *gb) { gb->cpu.reg.b |= (1 << 0); return 8; }
static int cb_C1(gb_t *gb) { gb->cpu.reg.c |= (1 << 0); return 8; }
static int cb_C2(gb_t *gb) { gb->cpu.reg.d |= (1 << 0); return 8; }
static int cb_C3(gb_t *gb) { gb->cpu.reg.e |= (1 << 0); return 8; }
static int cb_C4(gb_t *gb) { gb->cpu.reg.h |= (1 << 0); return 8; }
static int cb_C5(gb_t *gb) { gb->cpu.reg.l |= (1 << 0); return 8; }
static int cb_C6(gb_t *gb) { u8 v = READ(gb, gb->cpu.reg.hl); v |= (1 << 0); WRITE(gb, gb->cpu.reg.hl, v); return 16; }
static int cb_C7(gb_t *gb) { gb->cpu.reg.a |= (1 << 0); return 8; }
/* SET 1,r: 0xC8-0xCF */
static int cb_C8(gb_t *gb) { gb->cpu.reg.b |= (1 << 1); return 8; }
static int cb_C9(gb_t *gb) { gb->cpu.reg.c |= (1 << 1); return 8; }
static int cb_CA(gb_t *gb) { gb->cpu.reg.d |= (1 << 1); return 8; }
static int cb_CB(gb_t *gb) { gb->cpu.reg.e |= (1 << 1); return 8; }
static int cb_CC(gb_t *gb) { gb->cpu.reg.h |= (1 << 1); return 8; }
static int cb_CD(gb_t *gb) { gb->cpu.reg.l |= (1 << 1); return 8; }
static int cb_CE(gb_t *gb) { u8 v = READ(gb, gb->cpu.reg.hl); v |= (1 << 1); WRITE(gb, gb->cpu.reg.hl, v); return 16; }
static int cb_CF(gb_t *gb) { gb->cpu.reg.a |= (1 << 1); return 8; }
/* SET 2,r: 0xD0-0xD7 */
static int cb_D0(gb_t *gb) { gb->cpu.reg.b |= (1 << 2); return 8; }
static int cb_D1(gb_t *gb) { gb->cpu.reg.c |= (1 << 2); return 8; }
static int cb_D2(gb_t *gb) { gb->cpu.reg.d |= (1 << 2); return 8; }
static int cb_D3(gb_t *gb) { gb->cpu.reg.e |= (1 << 2); return 8; }
static int cb_D4(gb_t *gb) { gb->cpu.reg.h |= (1 << 2); return 8; }
static int cb_D5(gb_t *gb) { gb->cpu.reg.l |= (1 << 2); return 8; }
static int cb_D6(gb_t *gb) { u8 v = READ(gb, gb->cpu.reg.hl); v |= (1 << 2); WRITE(gb, gb->cpu.reg.hl, v); return 16; }
static int cb_D7(gb_t *gb) { gb->cpu.reg.a |= (1 << 2); return 8; }
/* SET 3,r: 0xD8-0xDF */
static int cb_D8(gb_t *gb) { gb->cpu.reg.b |= (1 << 3); return 8; }
static int cb_D9(gb_t *gb) { gb->cpu.reg.c |= (1 << 3); return 8; }
static int cb_DA(gb_t *gb) { gb->cpu.reg.d |= (1 << 3); return 8; }
static int cb_DB(gb_t *gb) { gb->cpu.reg.e |= (1 << 3); return 8; }
static int cb_DC(gb_t *gb) { gb->cpu.reg.h |= (1 << 3); return 8; }
static int cb_DD(gb_t *gb) { gb->cpu.reg.l |= (1 << 3); return 8; }
static int cb_DE(gb_t *gb) { u8 v = READ(gb, gb->cpu.reg.hl); v |= (1 << 3); WRITE(gb, gb->cpu.reg.hl, v); return 16; }
static int cb_DF(gb_t *gb) { gb->cpu.reg.a |= (1 << 3); return 8; }
/* SET 4,r: 0xE0-0xE7 */
static int cb_E0(gb_t *gb) { gb->cpu.reg.b |= (1 << 4); return 8; }
static int cb_E1(gb_t *gb) { gb->cpu.reg.c |= (1 << 4); return 8; }
static int cb_E2(gb_t *gb) { gb->cpu.reg.d |= (1 << 4); return 8; }
static int cb_E3(gb_t *gb) { gb->cpu.reg.e |= (1 << 4); return 8; }
static int cb_E4(gb_t *gb) { gb->cpu.reg.h |= (1 << 4); return 8; }
static int cb_E5(gb_t *gb) { gb->cpu.reg.l |= (1 << 4); return 8; }
static int cb_E6(gb_t *gb) { u8 v = READ(gb, gb->cpu.reg.hl); v |= (1 << 4); WRITE(gb, gb->cpu.reg.hl, v); return 16; }
static int cb_E7(gb_t *gb) { gb->cpu.reg.a |= (1 << 4); return 8; }
/* SET 5,r: 0xE8-0xEF */
static int cb_E8(gb_t *gb) { gb->cpu.reg.b |= (1 << 5); return 8; }
static int cb_E9(gb_t *gb) { gb->cpu.reg.c |= (1 << 5); return 8; }
static int cb_EA(gb_t *gb) { gb->cpu.reg.d |= (1 << 5); return 8; }
static int cb_EB(gb_t *gb) { gb->cpu.reg.e |= (1 << 5); return 8; }
static int cb_EC(gb_t *gb) { gb->cpu.reg.h |= (1 << 5); return 8; }
static int cb_ED(gb_t *gb) { gb->cpu.reg.l |= (1 << 5); return 8; }
static int cb_EE(gb_t *gb) { u8 v = READ(gb, gb->cpu.reg.hl); v |= (1 << 5); WRITE(gb, gb->cpu.reg.hl, v); return 16; }
static int cb_EF(gb_t *gb) { gb->cpu.reg.a |= (1 << 5); return 8; }
/* SET 6,r: 0xF0-0xF7 */
static int cb_F0(gb_t *gb) { gb->cpu.reg.b |= (1 << 6); return 8; }
static int cb_F1(gb_t *gb) { gb->cpu.reg.c |= (1 << 6); return 8; }
static int cb_F2(gb_t *gb) { gb->cpu.reg.d |= (1 << 6); return 8; }
static int cb_F3(gb_t *gb) { gb->cpu.reg.e |= (1 << 6); return 8; }
static int cb_F4(gb_t *gb) { gb->cpu.reg.h |= (1 << 6); return 8; }
static int cb_F5(gb_t *gb) { gb->cpu.reg.l |= (1 << 6); return 8; }
static int cb_F6(gb_t *gb) { u8 v = READ(gb, gb->cpu.reg.hl); v |= (1 << 6); WRITE(gb, gb->cpu.reg.hl, v); return 16; }
static int cb_F7(gb_t *gb) { gb->cpu.reg.a |= (1 << 6); return 8; }
/* SET 7,r: 0xF8-0xFF */
static int cb_F8(gb_t *gb) { gb->cpu.reg.b |= (1 << 7); return 8; }
static int cb_F9(gb_t *gb) { gb->cpu.reg.c |= (1 << 7); return 8; }
static int cb_FA(gb_t *gb) { gb->cpu.reg.d |= (1 << 7); return 8; }
static int cb_FB(gb_t *gb) { gb->cpu.reg.e |= (1 << 7); return 8; }
static int cb_FC(gb_t *gb) { gb->cpu.reg.h |= (1 << 7); return 8; }
static int cb_FD(gb_t *gb) { gb->cpu.reg.l |= (1 << 7); return 8; }
static int cb_FE(gb_t *gb) { u8 v = READ(gb, gb->cpu.reg.hl); v |= (1 << 7); WRITE(gb, gb->cpu.reg.hl, v); return 16; }
static int cb_FF(gb_t *gb) { gb->cpu.reg.a |= (1 << 7); return 8; }
/* ========== Opcode dispatch tables ========== */

static cpu_op_t ops[256] = {
    op_00, op_01, op_02, op_03, op_04, op_05, op_06, op_07,
    op_08, op_09, op_0A, op_0B, op_0C, op_0D, op_0E, op_0F,
    op_10, op_11, op_12, op_13, op_14, op_15, op_16, op_17,
    op_18, op_19, op_1A, op_1B, op_1C, op_1D, op_1E, op_1F,
    op_20, op_21, op_22, op_23, op_24, op_25, op_26, op_27,
    op_28, op_29, op_2A, op_2B, op_2C, op_2D, op_2E, op_2F,
    op_30, op_31, op_32, op_33, op_34, op_35, op_36, op_37,
    op_38, op_39, op_3A, op_3B, op_3C, op_3D, op_3E, op_3F,
    op_40, op_41, op_42, op_43, op_44, op_45, op_46, op_47,
    op_48, op_49, op_4A, op_4B, op_4C, op_4D, op_4E, op_4F,
    op_50, op_51, op_52, op_53, op_54, op_55, op_56, op_57,
    op_58, op_59, op_5A, op_5B, op_5C, op_5D, op_5E, op_5F,
    op_60, op_61, op_62, op_63, op_64, op_65, op_66, op_67,
    op_68, op_69, op_6A, op_6B, op_6C, op_6D, op_6E, op_6F,
    op_70, op_71, op_72, op_73, op_74, op_75, op_76, op_77,
    op_78, op_79, op_7A, op_7B, op_7C, op_7D, op_7E, op_7F,
    op_80, op_81, op_82, op_83, op_84, op_85, op_86, op_87,
    op_88, op_89, op_8A, op_8B, op_8C, op_8D, op_8E, op_8F,
    op_90, op_91, op_92, op_93, op_94, op_95, op_96, op_97,
    op_98, op_99, op_9A, op_9B, op_9C, op_9D, op_9E, op_9F,
    op_A0, op_A1, op_A2, op_A3, op_A4, op_A5, op_A6, op_A7,
    op_A8, op_A9, op_AA, op_AB, op_AC, op_AD, op_AE, op_AF,
    op_B0, op_B1, op_B2, op_B3, op_B4, op_B5, op_B6, op_B7,
    op_B8, op_B9, op_BA, op_BB, op_BC, op_BD, op_BE, op_BF,
    op_C0, op_C1, op_C2, op_C3, op_C4, op_C5, op_C6, op_C7,
    op_C8, op_C9, op_CA, op_CB, op_CC, op_CD, op_CE, op_CF,
    op_D0, op_D1, op_D2, op_D3, op_D4, op_D5, op_D6, op_D7,
    op_D8, op_D9, op_DA, op_DB, op_DC, op_DD, op_DE, op_DF,
    op_E0, op_E1, op_E2, op_E3, op_E4, op_E5, op_E6, op_E7,
    op_E8, op_E9, op_EA, op_EB, op_EC, op_ED, op_EE, op_EF,
    op_F0, op_F1, op_F2, op_F3, op_F4, op_F5, op_F6, op_F7,
    op_F8, op_F9, op_FA, op_FB, op_FC, op_FD, op_FE, op_FF
};

static cpu_op_t cb_ops[256] = {
    cb_00, cb_01, cb_02, cb_03, cb_04, cb_05, cb_06, cb_07,
    cb_08, cb_09, cb_0A, cb_0B, cb_0C, cb_0D, cb_0E, cb_0F,
    cb_10, cb_11, cb_12, cb_13, cb_14, cb_15, cb_16, cb_17,
    cb_18, cb_19, cb_1A, cb_1B, cb_1C, cb_1D, cb_1E, cb_1F,
    cb_20, cb_21, cb_22, cb_23, cb_24, cb_25, cb_26, cb_27,
    cb_28, cb_29, cb_2A, cb_2B, cb_2C, cb_2D, cb_2E, cb_2F,
    cb_30, cb_31, cb_32, cb_33, cb_34, cb_35, cb_36, cb_37,
    cb_38, cb_39, cb_3A, cb_3B, cb_3C, cb_3D, cb_3E, cb_3F,
    cb_40, cb_41, cb_42, cb_43, cb_44, cb_45, cb_46, cb_47,
    cb_48, cb_49, cb_4A, cb_4B, cb_4C, cb_4D, cb_4E, cb_4F,
    cb_50, cb_51, cb_52, cb_53, cb_54, cb_55, cb_56, cb_57,
    cb_58, cb_59, cb_5A, cb_5B, cb_5C, cb_5D, cb_5E, cb_5F,
    cb_60, cb_61, cb_62, cb_63, cb_64, cb_65, cb_66, cb_67,
    cb_68, cb_69, cb_6A, cb_6B, cb_6C, cb_6D, cb_6E, cb_6F,
    cb_70, cb_71, cb_72, cb_73, cb_74, cb_75, cb_76, cb_77,
    cb_78, cb_79, cb_7A, cb_7B, cb_7C, cb_7D, cb_7E, cb_7F,
    cb_80, cb_81, cb_82, cb_83, cb_84, cb_85, cb_86, cb_87,
    cb_88, cb_89, cb_8A, cb_8B, cb_8C, cb_8D, cb_8E, cb_8F,
    cb_90, cb_91, cb_92, cb_93, cb_94, cb_95, cb_96, cb_97,
    cb_98, cb_99, cb_9A, cb_9B, cb_9C, cb_9D, cb_9E, cb_9F,
    cb_A0, cb_A1, cb_A2, cb_A3, cb_A4, cb_A5, cb_A6, cb_A7,
    cb_A8, cb_A9, cb_AA, cb_AB, cb_AC, cb_AD, cb_AE, cb_AF,
    cb_B0, cb_B1, cb_B2, cb_B3, cb_B4, cb_B5, cb_B6, cb_B7,
    cb_B8, cb_B9, cb_BA, cb_BB, cb_BC, cb_BD, cb_BE, cb_BF,
    cb_C0, cb_C1, cb_C2, cb_C3, cb_C4, cb_C5, cb_C6, cb_C7,
    cb_C8, cb_C9, cb_CA, cb_CB, cb_CC, cb_CD, cb_CE, cb_CF,
    cb_D0, cb_D1, cb_D2, cb_D3, cb_D4, cb_D5, cb_D6, cb_D7,
    cb_D8, cb_D9, cb_DA, cb_DB, cb_DC, cb_DD, cb_DE, cb_DF,
    cb_E0, cb_E1, cb_E2, cb_E3, cb_E4, cb_E5, cb_E6, cb_E7,
    cb_E8, cb_E9, cb_EA, cb_EB, cb_EC, cb_ED, cb_EE, cb_EF,
    cb_F0, cb_F1, cb_F2, cb_F3, cb_F4, cb_F5, cb_F6, cb_F7,
    cb_F8, cb_F9, cb_FA, cb_FB, cb_FC, cb_FD, cb_FE, cb_FF
};
/* ========== CPU init / step / interrupt ========== */
void gb_cpu_init(gb_cpu_t *cpu) {
    memset(cpu, 0, sizeof(*cpu));
    cpu->reg.af = 0x01B0;
    cpu->reg.bc = 0x0013;
    cpu->reg.de = 0x00D8;
    cpu->reg.hl = 0x014D;
    cpu->reg.sp = 0xFFFE;
    cpu->reg.pc = 0x0100;
    cpu->halted = false;
    cpu->ime = false;
    cpu->ime_pending = false;
    cpu->ime_delay = 0;
    cpu->cycles = 0;
}

void gb_cpu_trigger_interrupt(gb_t *gb, int flag) {
    gb->io[0x0F] |= (1 << flag);
    if (gb->cpu.halted) gb->cpu.halted = false;
}

static int handle_interrupts(gb_t *gb) {
    u8 pending = gb->ie & gb->io[0x0F] & 0x1F;
    if (!pending) return 0;

    /* A pending interrupt wakes the CPU even when IME is clear. */
    gb->cpu.halted = false;

    if (!gb->cpu.ime) return 0;

    for (int i = 0; i < 5; i++) {
        if (pending & (1 << i)) {
            gb->cpu.ime = 0;
            gb->cpu.ime_pending = 0;
            gb->io[0x0F] &= ~(1 << i);
            /* Two internal cycles precede the push; the second overlaps the
             * jump to the handler. */
            INTERNAL(gb);
            push_u16(gb, gb->cpu.reg.pc);
            gb->cpu.reg.pc = (u16)(0x40 + (i * 8));
            return 20;
        }
    }
    return 0;
}

int gb_cpu_step(gb_cpu_t *cpu, gb_t *gb) {
    /* EI takes effect only after the instruction that follows it. */
    bool enable_ime_after = cpu->ime_pending;

    cpu->tick_acc = 0;

    int cycles = handle_interrupts(gb);
    if (cycles == 0) {
        if (cpu->halted) {
            cycles = 4;
        } else {
            u8 opcode = fetch_byte(gb);
            cycles = ops[opcode](gb);

            if (enable_ime_after && cpu->ime_pending) {
                cpu->ime = 1;
                cpu->ime_pending = 0;
            }
        }
    }

    /* Memory accesses have already ticked the hardware in step with the
     * instruction; hand over whatever internal cycles are left. */
    if (cycles > cpu->tick_acc) {
        gb_tick(gb, cycles - cpu->tick_acc);
    }

    cpu->cycles += cycles;
    return cycles;
}
