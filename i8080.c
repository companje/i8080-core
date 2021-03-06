// Intel 8080 (KR580VM80A) microprocessor core model
//
// Copyright (C) 2012 Alexander Demin <alexander@demin.ws>
//
// Optimized for Arduino by reducing memory usage by Rick Companje (2016)
//
// Credits
//
// Viacheslav Slavinsky, Vector-06C FPGA Replica
// http://code.google.com/p/vector06cc/
//
// Dmitry Tselikov, Bashrikia-2M and Radio-86RK on Altera DE1
// http://bashkiria-2m.narod.ru/fpga.html
//
// Ian Bartholomew, 8080/8085 CPU Exerciser
// http://www.idb.me.uk/sunhillow/8080.html
//
// Frank Cringle, The original exerciser for the Z80.
//
// Thanks to zx.pk.ru and nedopc.org/forum communities.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

#include "i8080.h"
#include "i8080_hal.h"

#define RD_BYTE(addr) i8080_hal_memory_read_byte(addr)
#define RD_WORD(addr) i8080_hal_memory_read_word(addr)

#define WR_BYTE(addr, value) i8080_hal_memory_write_byte(addr, value)
#define WR_WORD(addr, value) i8080_hal_memory_write_word(addr, value)

typedef unsigned char           uns8;
typedef unsigned short          uns16;
typedef unsigned long int       uns32;
typedef signed char             sgn8;
typedef signed short            sgn16;
typedef signed long int         sgn32;

typedef union {
    struct {
        uns8 l, h;
    } b;
    uns16 w;
} reg_pair;

typedef struct {
    uns8 carry_flag;
    uns8 unused1;
    uns8 parity_flag;
    uns8 unused3;
    uns8 half_carry_flag;
    uns8 unused5;
    uns8 zero_flag;
    uns8 sign_flag;
} flag_reg;

struct i8080 {
    flag_reg f;
    reg_pair af, bc, de, hl;
    reg_pair sp, pc;
    uns16 iff;
    uns16 last_pc;
};

#define FLAGS           cpu.f
#define AF              cpu.af.w
#define BC              cpu.bc.w
#define DE              cpu.de.w
#define HL              cpu.hl.w
#define SP              cpu.sp.w
#define PC              cpu.pc.w
#define A               cpu.af.b.h
#define F               cpu.af.b.l
#define B               cpu.bc.b.h
#define C               cpu.bc.b.l
#define D               cpu.de.b.h
#define E               cpu.de.b.l
#define H               cpu.hl.b.h
#define L               cpu.hl.b.l
#define HSP             cpu.sp.b.h
#define LSP             cpu.sp.b.l
#define HPC             cpu.pc.b.h
#define LPC             cpu.pc.b.l
#define IFF             cpu.iff

#define F_CARRY         0x01
#define F_UN1           0x02
#define F_PARITY        0x04
#define F_UN3           0x08
#define F_HCARRY        0x10
#define F_UN5           0x20
#define F_ZERO          0x40
#define F_NEG           0x80

#define C_FLAG          FLAGS.carry_flag
#define P_FLAG          FLAGS.parity_flag
#define H_FLAG          FLAGS.half_carry_flag
#define Z_FLAG          FLAGS.zero_flag
#define S_FLAG          FLAGS.sign_flag
#define UN1_FLAG        FLAGS.unused1
#define UN3_FLAG        FLAGS.unused3
#define UN5_FLAG        FLAGS.unused5

#define SET(flag)       (flag = 1)
#define CLR(flag)       (flag = 0)
#define TST(flag)       (flag)
#define CPL(flag)       (flag = !flag)

#define POP(reg)        { (reg) = RD_WORD(SP); SP += 2; }
#define PUSH(reg)       { SP -= 2; WR_WORD(SP, (reg)); }
#define RET()           { POP(PC); }
#define STC()           { SET(C_FLAG); }
#define CMC()           { CPL(C_FLAG); }

#define INR(reg) \
{                                               \
    ++(reg);                                    \
    S_FLAG = (((reg) & 0x80) != 0);             \
    Z_FLAG = ((reg) == 0);                      \
    H_FLAG = (((reg) & 0x0f) == 0);             \
    P_FLAG = PARITY(reg);                       \
}

#define DCR(reg) \
{                                               \
    --(reg);                                    \
    S_FLAG = (((reg) & 0x80) != 0);             \
    Z_FLAG = ((reg) == 0);                      \
    H_FLAG = !(((reg) & 0x0f) == 0x0f);         \
    P_FLAG = PARITY(reg);                       \
}

#define ADD(val) \
{                                               \
    work16 = (uns16)A + (val);                  \
    index = ((A & 0x88) >> 1) |                 \
            (((val) & 0x88) >> 2) |             \
            ((work16 & 0x88) >> 3);             \
    A = work16 & 0xff;                          \
    S_FLAG = ((A & 0x80) != 0);                 \
    Z_FLAG = (A == 0);                          \
    H_FLAG = half_carry_table[index & 0x7];     \
    P_FLAG = PARITY(A);                         \
    C_FLAG = ((work16 & 0x0100) != 0);          \
}

#define ADC(val) \
{                                               \
    work16 = (uns16)A + (val) + C_FLAG;         \
    index = ((A & 0x88) >> 1) |                 \
            (((val) & 0x88) >> 2) |             \
            ((work16 & 0x88) >> 3);             \
    A = work16 & 0xff;                          \
    S_FLAG = ((A & 0x80) != 0);                 \
    Z_FLAG = (A == 0);                          \
    H_FLAG = half_carry_table[index & 0x7];     \
    P_FLAG = PARITY(A);                         \
    C_FLAG = ((work16 & 0x0100) != 0);          \
}

#define SUB(val) \
{                                                \
    work16 = (uns16)A - (val);                   \
    index = ((A & 0x88) >> 1) |                  \
            (((val) & 0x88) >> 2) |              \
            ((work16 & 0x88) >> 3);              \
    A = work16 & 0xff;                           \
    S_FLAG = ((A & 0x80) != 0);                  \
    Z_FLAG = (A == 0);                           \
    H_FLAG = !sub_half_carry_table[index & 0x7]; \
    P_FLAG = PARITY(A);                          \
    C_FLAG = ((work16 & 0x0100) != 0);           \
}

#define SBB(val) \
{                                                \
    work16 = (uns16)A - (val) - C_FLAG;          \
    index = ((A & 0x88) >> 1) |                  \
            (((val) & 0x88) >> 2) |              \
            ((work16 & 0x88) >> 3);              \
    A = work16 & 0xff;                           \
    S_FLAG = ((A & 0x80) != 0);                  \
    Z_FLAG = (A == 0);                           \
    H_FLAG = !sub_half_carry_table[index & 0x7]; \
    P_FLAG = PARITY(A);                          \
    C_FLAG = ((work16 & 0x0100) != 0);           \
}

#define CMP(val) \
{                                                \
    work16 = (uns16)A - (val);                   \
    index = ((A & 0x88) >> 1) |                  \
            (((val) & 0x88) >> 2) |              \
            ((work16 & 0x88) >> 3);              \
    S_FLAG = ((work16 & 0x80) != 0);             \
    Z_FLAG = ((work16 & 0xff) == 0);             \
    H_FLAG = !sub_half_carry_table[index & 0x7]; \
    C_FLAG = ((work16 & 0x0100) != 0);           \
    P_FLAG = PARITY(work16 & 0xff);              \
}

#define ANA(val) \
{                                               \
    H_FLAG = ((A | val) & 0x08) != 0;           \
    A &= (val);                                 \
    S_FLAG = ((A & 0x80) != 0);                 \
    Z_FLAG = (A == 0);                          \
    P_FLAG = PARITY(A);                         \
    CLR(C_FLAG);                                \
}

#define XRA(val) \
{                                               \
    A ^= (val);                                 \
    S_FLAG = ((A & 0x80) != 0);                 \
    Z_FLAG = (A == 0);                          \
    CLR(H_FLAG);                                \
    P_FLAG = PARITY(A);                         \
    CLR(C_FLAG);                                \
}

#define ORA(val) \
{                                               \
    A |= (val);                                 \
    S_FLAG = ((A & 0x80) != 0);                 \
    Z_FLAG = (A == 0);                          \
    CLR(H_FLAG);                                \
    P_FLAG = PARITY(A);                         \
    CLR(C_FLAG);                                \
}

#define DAD(reg) \
{                                               \
    work32 = (uns32)HL + (reg);                 \
    HL = work32 & 0xffff;                       \
    C_FLAG = ((work32 & 0x10000L) != 0);        \
}

#define CALL \
{                                               \
    PUSH(PC + 2);                               \
    PC = RD_WORD(PC);                           \
}

#define RST(addr) \
{                                               \
    PUSH(PC);                                   \
    PC = (addr);                                \
}

#define PARITY(reg) i8080_getParity(reg)

static struct i8080 cpu;

static uns32 work32;
static uns16 work16;
static uns8 work8;
static int index;
static uns8 carry, add;

int i8080_getParity(int val) {
  val ^= val >> 4;
  val &= 0xf;
  return !((0x6996 >> val) & 1);
}

int half_carry_table[] = { 0, 0, 1, 0, 1, 0, 1, 1 };
int sub_half_carry_table[] = { 0, 1, 1, 1, 0, 0, 0, 1 };

#define DEST(x) (x >> 3 & 7)
#define SOURCE(x) (x & 7)
#define CONDITION(x) (x >> 3 & 7)
#define VECTOR(x) (x >> 3 & 7)
#define RP(x) (x >> 4 & 3)

uns8 M;
uns8* REG[] = { &B, &C, &D, &E, &H, &L, &M, &A };
uns16* PAIR[] = { &BC, &DE, &HL, &SP };

void i8080_init(void) {
    C_FLAG = 0;
    S_FLAG = 0;
    Z_FLAG = 0;
    H_FLAG = 0;
    P_FLAG = 0;
    UN1_FLAG = 1;
    UN3_FLAG = 0;
    UN5_FLAG = 0;

    PC = 0xF800;
}

static void i8080_store_flags(void) {
    if (S_FLAG) F |= F_NEG;      else F &= ~F_NEG;
    if (Z_FLAG) F |= F_ZERO;     else F &= ~F_ZERO;
    if (H_FLAG) F |= F_HCARRY;   else F &= ~F_HCARRY;
    if (P_FLAG) F |= F_PARITY;   else F &= ~F_PARITY;
    if (C_FLAG) F |= F_CARRY;    else F &= ~F_CARRY;
    F |= F_UN1;    // UN1_FLAG is always 1.
    F &= ~F_UN3;   // UN3_FLAG is always 0.
    F &= ~F_UN5;   // UN5_FLAG is always 0.
}

static void i8080_retrieve_flags(void) {
    S_FLAG = F & F_NEG      ? 1 : 0;
    Z_FLAG = F & F_ZERO     ? 1 : 0;
    H_FLAG = F & F_HCARRY   ? 1 : 0;
    P_FLAG = F & F_PARITY   ? 1 : 0;
    C_FLAG = F & F_CARRY    ? 1 : 0;
}

static uns8 i8080_checkCondition(uns8 c) {
  switch (c) {
    case 0: return !Z_FLAG;
    case 1: return Z_FLAG;
    case 2: return !C_FLAG;
    case 3: return C_FLAG;
    case 4: return !P_FLAG;
    case 5: return P_FLAG;
    case 6: return !S_FLAG;
    case 7: return S_FLAG;
  }
  return 0;
}

static int i8080_execute(int opcode) {
    int cpu_cycles = 0;

    switch (opcode) {
        case 0x00:            /* nop */
        // Undocumented NOP.
        case 0x08:            /* nop */
        case 0x10:            /* nop */
        case 0x18:            /* nop */
        case 0x20:            /* nop */
        case 0x28:            /* nop */
        case 0x30:            /* nop */
        case 0x38:            /* nop */
            cpu_cycles = 4;
            break;

        case 0x07:            /* rlc */
            cpu_cycles = 4;
            C_FLAG = ((A & 0x80) != 0);
            A = (A << 1) | C_FLAG;
            break;

        case 0x0F:            /* rrc */
            cpu_cycles = 4;
            C_FLAG = A & 0x01;
            A = (A >> 1) | (C_FLAG << 7);
            break;

        case 0x17:            /* ral */
            cpu_cycles = 4;
            work8 = (uns8)C_FLAG;
            C_FLAG = ((A & 0x80) != 0);
            A = (A << 1) | work8;
            break;

        case 0x1F:             /* rar */
            cpu_cycles = 4;
            work8 = (uns8)C_FLAG;
            C_FLAG = A & 0x01;
            A = (A >> 1) | (work8 << 7);
            break;

        case 0x22:            /* shld addr */
            cpu_cycles = 16;
            WR_WORD(RD_WORD(PC), HL);
            PC += 2;
            break;

        case 0x27:            /* daa */
            cpu_cycles = 4;
            carry = (uns8)C_FLAG;
            add = 0;
            if (H_FLAG || (A & 0x0f) > 9) {
                add = 0x06;
            }
            if (C_FLAG || (A >> 4) > 9 || ((A >> 4) >= 9 && (A & 0x0f) > 9)) {
                add |= 0x60;
                carry = 1;
            }
            ADD(add);
            P_FLAG = PARITY(A);
            C_FLAG = carry;
            break;

        case 0x2A:            /* ldhl addr */
            cpu_cycles = 16;
            HL = RD_WORD(RD_WORD(PC));
            PC += 2;
            break;

        case 0x2F:            /* cma */
            cpu_cycles = 4;
            A ^= 0xff;
            break;

        case 0x32:            /* sta addr */
            cpu_cycles = 13;
            WR_BYTE(RD_WORD(PC), A);
            PC += 2;
            break;

        case 0x34:            /* inr m */
            cpu_cycles = 10;
            work8 = RD_BYTE(HL);
            INR(work8);
            WR_BYTE(HL, work8);
            break;

        case 0x35:            /* dcr m */
            cpu_cycles = 10;
            work8 = RD_BYTE(HL);
            DCR(work8);
            WR_BYTE(HL, work8);
            break;

        case 0x36:            /* mvi m, data8 */
            cpu_cycles = 10;
            WR_BYTE(HL, RD_BYTE(PC++));
            break;

        case 0x37:            /* stc */
            cpu_cycles = 4;
            SET(C_FLAG);
            break;

        case 0x3A:            /* lda addr */
            cpu_cycles = 13;
            A = RD_BYTE(RD_WORD(PC));
            PC += 2;
            break;

        case 0x3F:            /* cmc */
            cpu_cycles = 4;
            CPL(C_FLAG);
            break;

        case 0x76:            /* hlt */
            cpu_cycles = 4;
            PC--;
            break;

        case 0x86:            /* add m */
            cpu_cycles = 7;
            work8 = RD_BYTE(HL);
            ADD(work8);
            break;

        case 0x8E:            /* adc m */
            cpu_cycles = 7;
            work8 = RD_BYTE(HL);
            ADC(work8);
            break;

        case 0x96:            /* sub m */
            cpu_cycles = 7;
            work8 = RD_BYTE(HL);
            SUB(work8);
            break;

        case 0x9E:            /* sbb m */
            cpu_cycles = 7;
            work8 = RD_BYTE(HL);
            SBB(work8);
            break;

        case 0xA6:            /* ana m */
            cpu_cycles = 7;
            work8 = RD_BYTE(HL);
            ANA(work8);
            break;

        case 0xAE:            /* xra m */
            cpu_cycles = 7;
            work8 = RD_BYTE(HL);
            XRA(work8);
            break;

        case 0xB6:            /* ora m */
            cpu_cycles = 7;
            work8 = RD_BYTE(HL);
            ORA(work8);
            break;

        case 0xBE:            /* cmp m */
            cpu_cycles = 7;
            work8 = RD_BYTE(HL);
            CMP(work8);
            break;

        case 0xC3:            /* jmp addr */
        case 0xCB:            /* jmp addr, undocumented */
            cpu_cycles = 10;
            PC = RD_WORD(PC);
            break;

        case 0xC6:            /* adi data8 */
            cpu_cycles = 7;
            work8 = RD_BYTE(PC++);
            ADD(work8);
            break;

        case 0xC9:            /* ret */
        case 0xD9:            /* ret, undocumented */
            cpu_cycles = 10;
            POP(PC);
            break;

        case 0xCD:            /* call addr */
        case 0xDD:            /* call, undocumented */
        case 0xED:
        case 0xFD:
            cpu_cycles = 17;
            CALL;
            break;

        case 0xCE:            /* aci data8 */
            cpu_cycles = 7;
            work8 = RD_BYTE(PC++);
            ADC(work8);
            break;

        case 0xD3:            /* out port8 */
            cpu_cycles = 10;
            i8080_hal_io_output(RD_BYTE(PC++), A);
            break;

        case 0xD6:            /* sui data8 */
            cpu_cycles = 7;
            work8 = RD_BYTE(PC++);
            SUB(work8);
            break;

        case 0xDB:            /* in port8 */
            cpu_cycles = 10;
            A = i8080_hal_io_input(RD_BYTE(PC++));
            break;

        case 0xDE:            /* sbi data8 */
            cpu_cycles = 7;
            work8 = RD_BYTE(PC++);
            SBB(work8);
            break;

        case 0xE3:            /* xthl */
            cpu_cycles = 18;
            work16 = RD_WORD(SP);
            WR_WORD(SP, HL);
            HL = work16;
            break;

        case 0xE6:            /* ani data8 */
            cpu_cycles = 7;
            work8 = RD_BYTE(PC++);
            ANA(work8);
            break;

        case 0xE9:            /* pchl */
            cpu_cycles = 5;
            PC = HL;
            break;

        case 0xEB:            /* xchg */
            cpu_cycles = 4;
            work16 = DE;
            DE = HL;
            HL = work16;
            break;

        case 0xEE:            /* xri data8 */
            cpu_cycles = 7;
            work8 = RD_BYTE(PC++);
            XRA(work8);
            break;

        case 0xF1:            /* pop psw */
            cpu_cycles = 10;
            POP(AF);
            i8080_retrieve_flags();
            break;

        case 0xF3:            /* di */
            cpu_cycles = 4;
            IFF = 0;
            i8080_hal_iff(IFF);
            break;

        case 0xF5:            /* push psw */
            cpu_cycles = 11;
            i8080_store_flags();
            PUSH(AF);
            break;

        case 0xF6:            /* ori data8 */
            cpu_cycles = 7;
            work8 = RD_BYTE(PC++);
            ORA(work8);
            break;

        case 0xF9:            /* sphl */
            cpu_cycles = 5;
            SP = HL;
            break;

        case 0xFB:            /* ei */
            cpu_cycles = 4;
            IFF = 1;
            i8080_hal_iff(IFF);
            break;

        case 0xFE:            /* cpi data8 */
            cpu_cycles = 7;
            work8 = RD_BYTE(PC++);
            CMP(work8);
            break;

    }

    if (cpu_cycles!=0) {
        return cpu_cycles;
    }

    // cmp,ora,xra,ana,sbb,sub,adc,add
    switch (opcode & 0b11111000) {
        case 0b10111000: CMP(*REG[SOURCE(opcode)]); return 4; // cmp s  ZSPCA   Compare register with A
        case 0b10110000: ORA(*REG[SOURCE(opcode)]); return 4; // ora s  ZSPCA   OR  register with A
        case 0b10101000: XRA(*REG[SOURCE(opcode)]); return 4; // xra s  ZSPCA   ExclusiveOR register with A
        case 0b10100000: ANA(*REG[SOURCE(opcode)]); return 4; // ana s  ZSCPA   AND register with A
        case 0b10011000: SBB(*REG[SOURCE(opcode)]); return 4; // sbb s  ZSCPA   Subtract register from A with borrow
        case 0b10010000: SUB(*REG[SOURCE(opcode)]); return 4; // sub s  ZSCPA   Subtract register from A
        case 0b10001000: ADC(*REG[SOURCE(opcode)]); return 4; // adc s  ZSCPA   Add register to A with carry
        case 0b10000000: ADD(*REG[SOURCE(opcode)]); return 4; // add s  ZSPCA   Add register to A
    }

    // rst,cccc,jccc,rccc,mvi,dcr,inr
    switch (opcode & 0b11000111) {
        case 0b11000111: RST(DEST(opcode)*8); return 11; // rst n - Restart (n*8)
        case 0b11000100: if (i8080_checkCondition(CONDITION(opcode))) { CALL return 17; } else { PC+=2; return 11; } // cccc a    lb hb    -       Conditional subroutine call
        case 0b11000010: if (i8080_checkCondition(CONDITION(opcode))) PC = RD_WORD(PC); else PC+=2; return 10; // jccc a    lb hb    -       Conditional jump
        case 0b11000000: if (i8080_checkCondition(CONDITION(opcode))) { POP(PC); return 11; } else return 5; // rccc -       Conditional return 0 from subroutine
        case 0b00000110: *REG[DEST(opcode)] = RD_BYTE(PC++); return 7; // mvi d,#   db - Move immediate to register
        case 0b00000101: DCR(*REG[DEST(opcode)]); return 5; // dcr d   ZSPA    Decrement register
        case 0b00000100: INR(*REG[DEST(opcode)]); return 5; // inr d   ZSPA    Increment register
    }

    // push,pop,dcx,ldax,dad,inx,stax,lxi
    switch (opcode & 0b11001111) {
        case 0b11000101: PUSH(*PAIR[RP(opcode)]); return 11; // push rp   *2       -       Push register pair on the stack
        case 0b11000001: POP(*PAIR[RP(opcode)]); return 11; // pop rp    *2       *2      Pop  register pair from the stack
        case 0b00001011: (*PAIR[RP(opcode)])--; return 5; // dcx rp -       Decrement register pair
        case 0b00001010: A = RD_BYTE(*PAIR[RP(opcode)]); return 7; // ldax rp   *1       -       Load indirect through BC or DE
        case 0b00001001: DAD(*PAIR[RP(opcode)]); return 10; // dad rp             C       Add register pair to HL (16 bit add)
        case 0b00000011: (*PAIR[RP(opcode)])++; return 5; // inx rp             -       Increment register pair
        case 0b00000010: WR_BYTE(*PAIR[RP(opcode)],A); return 7; // stax rp   *1       -       Store indirect through BC or DE
        case 0b00000001: *PAIR[RP(opcode)] = RD_WORD(PC); PC+=2; return 10; // lxi rp,#  lb hb    -       Load register pair immediate
    }

    // mov d,s - Move register to register
    if ((opcode & 0b11000000) == 0b01000000) { 
        if (DEST(opcode)==6) WR_BYTE(HL,*REG[SOURCE(opcode)]);
        else if (SOURCE(opcode)==6) *REG[DEST(opcode)] = RD_BYTE(HL);
        else *REG[DEST(opcode)] = *REG[SOURCE(opcode)]; 
        return 5;
    }

    return -1;
}

int i8080_instruction(void) {
    return i8080_execute(RD_BYTE(PC++));
}

void i8080_jump(int addr) {
    PC = addr & 0xffff;
}

int i8080_pc(void) {
    return PC;
}

int i8080_regs_bc(void) {
    return BC;
}

int i8080_regs_de(void) {
    return DE;
}

int i8080_regs_hl(void) {
    return HL;
}

int i8080_regs_sp(void) {
    return SP;
}

int i8080_regs_a(void) {
    return A;
}

int i8080_regs_b(void) {
    return B;
}

int i8080_regs_c(void) {
    return C;
}

int i8080_regs_d(void) {
    return D;
}

int i8080_regs_e(void) {
    return E;
}

int i8080_regs_h(void) {
    return H;
}

int i8080_regs_l(void) {
    return L;
}
