/* Pentium III encoding inventory, independent of the emulator timing tables.
 * Encoding references: Intel IA-32 instruction set, and
 * https://www.sandpile.org/x86/opc_1.htm, opc_2.htm, opc_fpu.htm.
 * No SSE2, AMD instructions, or emulator-specific extensions are included.
 * This describes encodings, not an instruction implementation or timing oracle.
 */
#ifndef PENTIUM3_OPCODES_H
#define PENTIUM3_OPCODES_H

enum { MAP_BASE, MAP_0F, MAP_F3_0F, MAP_X87, MAP_COUNT };
enum { FORM_IMPLICIT, FORM_REGISTER, FORM_MEMORY };
enum { INTEGER, BRANCH, STACK, STRING, SYSTEM, X87, MMX, SSE, FAMILY_COUNT };
/* F_ALIAS also labels undocumented and obsolete compatibility encodings. */
enum { F_MODRM = 1, F_LOCK = 2, F_REP = 4, F_ALIAS = 8 };
static const char *family_names[] = {
    "integer", "branch", "stack", "string", "system", "x87", "mmx", "sse"
};
static const char *map_names[] = { "base", "0f", "f3-0f", "x87" };
static const char *form_names[] = { "implicit", "reg", "mem" };

typedef struct {
    const char *name;
    unsigned map, opcode, form, mask, value, family, flags;
} opcode_case_t;

#define MAX_OPCODE_CASES 1600
static opcode_case_t opcode_cases[MAX_OPCODE_CASES];
static unsigned opcode_case_count;
/* One owner per encoding. Unowned bytes are reserved/unsupported; prefixes
 * and escapes are exercised as part of instructions, never timed on their own. */
static unsigned short encoding_owner[MAP_COUNT][256][256];

static void
add_form(unsigned map, unsigned opcode, unsigned form, unsigned mask,
         unsigned value, const char *name, unsigned family, unsigned flags)
{
    CHECK(opcode_case_count < MAX_OPCODE_CASES);
    opcode_case_t *c = &opcode_cases[opcode_case_count++];
    *c = (opcode_case_t) {name, map, opcode, form, mask, value, family, flags};
    for (unsigned m = 0; m < 256; m++) {
        if ((m & mask) != value || (form == FORM_REGISTER && m < 0xc0)
            || (form == FORM_MEMORY && m >= 0xc0))
            continue;
        CHECK(encoding_owner[map][opcode][m] == 0);
        encoding_owner[map][opcode][m] = (unsigned short) opcode_case_count;
    }
}

static void
add_rm(unsigned map, unsigned opcode, int group, const char *name,
       unsigned family, unsigned flags, int memory_only, int register_only)
{
    unsigned mask = group < 0 ? 0 : 0x38;
    unsigned value = group < 0 ? 0 : (unsigned) group << 3;
    if (!memory_only)
        add_form(map, opcode, FORM_REGISTER, mask, value, name, family, flags | F_MODRM);
    if (!register_only)
        add_form(map, opcode, FORM_MEMORY, mask, value, name, family, flags | F_MODRM);
}

static void
add_implicit(unsigned map, unsigned opcode, const char *name, unsigned family, unsigned flags)
{
    add_form(map, opcode, FORM_IMPLICIT, 0, 0, name, family, flags);
}

static void
build_opcode_catalog(void)
{
    static const char *alu[] = {"ADD", "OR", "ADC", "SBB", "AND", "SUB", "XOR", "CMP"};
    static const char *cc[] = {"O", "NO", "B", "AE", "E", "NE", "BE", "A",
                              "S", "NS", "P", "NP", "L", "GE", "LE", "G"};
    static char jcc[16][12], cmov[16][12], setcc[16][12];
    for (unsigned a = 0; a < 8; a++) {
        for (unsigned o = 0; o < 4; o++)
            add_rm(MAP_BASE, a * 8 + o, -1, alu[a], INTEGER,
                   a != 7 && o < 2 ? F_LOCK : 0, 0, 0);
        add_implicit(MAP_BASE, a * 8 + 4, alu[a], INTEGER, 0);
        add_implicit(MAP_BASE, a * 8 + 5, alu[a], INTEGER, 0);
    }
    static const unsigned seg_push[] = {0x06, 0x0e, 0x16, 0x1e};
    static const unsigned seg_pop[] = {0x07, 0x17, 0x1f};
    for (unsigned i = 0; i < 4; i++) add_implicit(MAP_BASE, seg_push[i], "PUSH-seg", STACK, 0);
    for (unsigned i = 0; i < 3; i++) add_implicit(MAP_BASE, seg_pop[i], "POP-seg", STACK, 0);
    static const char *adjust[] = {"DAA", "DAS", "AAA", "AAS"};
    for (unsigned i = 0; i < 4; i++) add_implicit(MAP_BASE, 0x27 + i * 8, adjust[i], INTEGER, 0);
    for (unsigned i = 0; i < 8; i++) {
        add_implicit(MAP_BASE, 0x40 + i, "INC", INTEGER, 0);
        add_implicit(MAP_BASE, 0x48 + i, "DEC", INTEGER, 0);
        add_implicit(MAP_BASE, 0x50 + i, "PUSH", STACK, 0);
        add_implicit(MAP_BASE, 0x58 + i, "POP", STACK, 0);
    }
    add_implicit(MAP_BASE, 0x60, "PUSHA", STACK, 0);
    add_implicit(MAP_BASE, 0x61, "POPA", STACK, 0);
    add_rm(MAP_BASE, 0x62, -1, "BOUND", SYSTEM, 0, 1, 0);
    add_rm(MAP_BASE, 0x63, -1, "ARPL", SYSTEM, 0, 0, 0);
    add_implicit(MAP_BASE, 0x68, "PUSH-imm", STACK, 0);
    add_implicit(MAP_BASE, 0x6a, "PUSH-imm8", STACK, 0);
    add_rm(MAP_BASE, 0x69, -1, "IMUL-imm", INTEGER, 0, 0, 0);
    add_rm(MAP_BASE, 0x6b, -1, "IMUL-imm8", INTEGER, 0, 0, 0);
    for (unsigned i = 0; i < 4; i++)
        add_implicit(MAP_BASE, 0x6c + i, i < 2 ? "INS" : "OUTS", STRING, F_REP);
    for (unsigned i = 0; i < 16; i++) {
        snprintf(jcc[i], sizeof(jcc[i]), "J%s", cc[i]);
        snprintf(cmov[i], sizeof(cmov[i]), "CMOV%s", cc[i]);
        snprintf(setcc[i], sizeof(setcc[i]), "SET%s", cc[i]);
        add_implicit(MAP_BASE, 0x70 + i, jcc[i], BRANCH, 0);
        add_implicit(MAP_0F, 0x80 + i, jcc[i], BRANCH, 0);
        add_rm(MAP_0F, 0x40 + i, -1, cmov[i], INTEGER, 0, 0, 0);
        add_rm(MAP_0F, 0x90 + i, -1, setcc[i], INTEGER, 0, 0, 0);
    }
    for (unsigned o = 0x80; o <= 0x83; o++)
        for (unsigned g = 0; g < 8; g++)
            add_rm(MAP_BASE, o, g, alu[g], INTEGER,
                   (g != 7 ? F_LOCK : 0) | (o == 0x82 ? F_ALIAS : 0), 0, 0);
    for (unsigned o = 0x84; o <= 0x8b; o++)
        add_rm(MAP_BASE, o, -1, o < 0x86 ? "TEST" : o < 0x88 ? "XCHG" : "MOV",
               INTEGER, o >= 0x86 && o < 0x88 ? F_LOCK : 0, 0, 0);
    for (unsigned g = 0; g < 6; g++) {
        add_rm(MAP_BASE, 0x8c, g, "MOV-from-seg", SYSTEM, 0, 0, 0);
        if (g != 1) add_rm(MAP_BASE, 0x8e, g, "MOV-to-seg", SYSTEM, 0, 0, 0);
    }
    add_rm(MAP_BASE, 0x8d, -1, "LEA", INTEGER, 0, 1, 0);
    add_rm(MAP_BASE, 0x8f, 0, "POP", STACK, 0, 0, 0);
    for (unsigned o = 0x90; o <= 0x97; o++)
        add_implicit(MAP_BASE, o, o == 0x90 ? "NOP" : "XCHG-acc", INTEGER, 0);
    static const char *n98[] = {"CBW-CWDE", "CWD-CDQ", "CALL-far", "WAIT",
                               "PUSHF", "POPF", "SAHF", "LAHF"};
    static const unsigned f98[] = {INTEGER, INTEGER, BRANCH, X87, STACK, STACK, INTEGER, INTEGER};
    for (unsigned i = 0; i < 8; i++) add_implicit(MAP_BASE, 0x98 + i, n98[i], f98[i], 0);
    for (unsigned o = 0xa0; o <= 0xa3; o++) add_implicit(MAP_BASE, o, "MOV-moffs", INTEGER, 0);
    for (unsigned o = 0xa4; o <= 0xaf; o++) {
        const char *n = o < 0xa6 ? "MOVS" : o < 0xa8 ? "CMPS" : o < 0xaa ? "TEST"
                        : o < 0xac ? "STOS" : o < 0xae ? "LODS" : "SCAS";
        add_implicit(MAP_BASE, o, n, o == 0xa8 || o == 0xa9 ? INTEGER : STRING,
                     o == 0xa8 || o == 0xa9 ? 0 : F_REP);
    }
    for (unsigned o = 0xb0; o <= 0xbf; o++) add_implicit(MAP_BASE, o, "MOV-imm", INTEGER, 0);
    static const char *shift[] = {"ROL", "ROR", "RCL", "RCR", "SHL", "SHR", "SAL-alias", "SAR"};
    static const unsigned shift_ops[] = {0xc0, 0xc1, 0xd0, 0xd1, 0xd2, 0xd3};
    for (unsigned i = 0; i < 6; i++)
        for (unsigned g = 0; g < 8; g++)
            add_rm(MAP_BASE, shift_ops[i], g, shift[g], INTEGER, g == 6 ? F_ALIAS : 0, 0, 0);
    add_implicit(MAP_BASE, 0xc2, "RET-imm", BRANCH, 0);
    add_implicit(MAP_BASE, 0xc3, "RET", BRANCH, 0);
    add_rm(MAP_BASE, 0xc4, -1, "LES", SYSTEM, 0, 1, 0);
    add_rm(MAP_BASE, 0xc5, -1, "LDS", SYSTEM, 0, 1, 0);
    add_rm(MAP_BASE, 0xc6, 0, "MOV-imm", INTEGER, 0, 0, 0);
    add_rm(MAP_BASE, 0xc7, 0, "MOV-imm", INTEGER, 0, 0, 0);
    static const char *nc8[] = {"ENTER", "LEAVE", "RETF-imm", "RETF", "INT3", "INT", "INTO", "IRET"};
    for (unsigned i = 0; i < 8; i++)
        add_implicit(MAP_BASE, 0xc8 + i, nc8[i], i < 2 ? STACK : i < 4 ? BRANCH : SYSTEM, 0);
    add_implicit(MAP_BASE, 0xd4, "AAM", INTEGER, 0);
    add_implicit(MAP_BASE, 0xd5, "AAD", INTEGER, 0);
    add_implicit(MAP_BASE, 0xd6, "SALC", INTEGER, F_ALIAS);
    add_implicit(MAP_BASE, 0xd7, "XLAT", INTEGER, 0);
    static const char *ne0[] = {"LOOPNE", "LOOPE", "LOOP", "JCXZ", "IN", "IN", "OUT", "OUT",
                               "CALL", "JMP", "JMP-far", "JMP-short", "IN", "IN", "OUT", "OUT"};
    for (unsigned i = 0; i < 16; i++)
        add_implicit(MAP_BASE, 0xe0 + i, ne0[i], (i < 4 || (i >= 8 && i < 12)) ? BRANCH : SYSTEM, 0);
    add_implicit(MAP_BASE, 0xf1, "ICEBP", SYSTEM, F_ALIAS);
    add_implicit(MAP_BASE, 0xf4, "HLT", SYSTEM, 0);
    add_implicit(MAP_BASE, 0xf5, "CMC", INTEGER, 0);
    static const char *unary[] = {"TEST", "TEST-alias", "NOT", "NEG", "MUL", "IMUL", "DIV", "IDIV"};
    for (unsigned o = 0xf6; o <= 0xf7; o++)
        for (unsigned g = 0; g < 8; g++)
            add_rm(MAP_BASE, o, g, unary[g], INTEGER,
                   (g == 2 || g == 3 ? F_LOCK : 0) | (g == 1 ? F_ALIAS : 0), 0, 0);
    static const char *nf8[] = {"CLC", "STC", "CLI", "STI", "CLD", "STD"};
    for (unsigned i = 0; i < 6; i++) add_implicit(MAP_BASE, 0xf8 + i, nf8[i], i == 2 || i == 3 ? SYSTEM : INTEGER, 0);
    for (unsigned g = 0; g < 2; g++) add_rm(MAP_BASE, 0xfe, g, g ? "DEC" : "INC", INTEGER, F_LOCK, 0, 0);
    static const char *ff[] = {"INC", "DEC", "CALL-indirect", "CALL-far-indirect", "JMP-indirect", "JMP-far-indirect", "PUSH"};
    for (unsigned g = 0; g < 7; g++)
        add_rm(MAP_BASE, 0xff, g, ff[g], g < 2 ? INTEGER : g == 6 ? STACK : BRANCH,
               g < 2 ? F_LOCK : 0, g == 3 || g == 5, 0);

    static const char *sys0[] = {"SLDT", "STR", "LLDT", "LTR", "VERR", "VERW"};
    for (unsigned g = 0; g < 6; g++) add_rm(MAP_0F, 0x00, g, sys0[g], SYSTEM, 0, 0, 0);
    static const char *sys1[] = {"SGDT", "SIDT", "LGDT", "LIDT", "SMSW", NULL, "LMSW", "INVLPG"};
    for (unsigned g = 0; g < 8; g++)
        if (sys1[g]) add_rm(MAP_0F, 0x01, g, sys1[g], SYSTEM, 0, g != 4 && g != 6, 0);
    add_rm(MAP_0F, 0x02, -1, "LAR", SYSTEM, 0, 0, 0);
    add_rm(MAP_0F, 0x03, -1, "LSL", SYSTEM, 0, 0, 0);
    static const unsigned sysops[] = {0x06, 0x08, 0x09, 0x0b, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0xa2, 0xaa};
    static const char *sysnames[] = {"CLTS", "INVD", "WBINVD", "UD2", "WRMSR", "RDTSC", "RDMSR", "RDPMC", "SYSENTER", "SYSEXIT", "CPUID", "RSM"};
    for (unsigned i = 0; i < sizeof(sysops) / sizeof(sysops[0]); i++)
        add_implicit(MAP_0F, sysops[i], sysnames[i], SYSTEM, 0);
    for (unsigned o = 0x20; o <= 0x23; o++)
        for (unsigned g = 0; g < 8; g++)
            if ((o & 1) || g == 0 || g == 2 || g == 3 || g == 4)
                add_rm(MAP_0F, o, g, (o & 1) ? "MOV-DR" : "MOV-CR", SYSTEM, 0, 0, 1);
    for (unsigned g = 0; g < 4; g++) add_rm(MAP_0F, 0x18, g, "PREFETCH", SSE, 0, 1, 0);
    /* P6 hint NOPs accept ModR/M but do not access the effective address. */
    for (unsigned o = 0x19; o <= 0x1f; o++) add_rm(MAP_0F, o, -1, "HINT-NOP", INTEGER, o != 0x1f ? F_ALIAS : 0, 0, 0);
    add_implicit(MAP_0F, 0xa0, "PUSH-FS", STACK, 0);
    add_implicit(MAP_0F, 0xa1, "POP-FS", STACK, 0);
    add_implicit(MAP_0F, 0xa8, "PUSH-GS", STACK, 0);
    add_implicit(MAP_0F, 0xa9, "POP-GS", STACK, 0);
    static const unsigned bitops[] = {0xa3, 0xab, 0xb3, 0xbb};
    static const char *bitnames[] = {"BT", "BTS", "BTR", "BTC"};
    for (unsigned i = 0; i < 4; i++) {
        add_rm(MAP_0F, bitops[i], -1, bitnames[i], INTEGER, i ? F_LOCK : 0, 0, 0);
        add_rm(MAP_0F, 0xba, 4 + i, bitnames[i], INTEGER, i ? F_LOCK : 0, 0, 0);
    }
    static const unsigned shdouble[] = {0xa4, 0xa5, 0xac, 0xad};
    for (unsigned i = 0; i < 4; i++) add_rm(MAP_0F, shdouble[i], -1, i < 2 ? "SHLD" : "SHRD", INTEGER, 0, 0, 0);
    add_rm(MAP_0F, 0xaf, -1, "IMUL", INTEGER, 0, 0, 0);
    add_rm(MAP_0F, 0xb0, -1, "CMPXCHG", INTEGER, F_LOCK, 0, 0);
    add_rm(MAP_0F, 0xb1, -1, "CMPXCHG", INTEGER, F_LOCK, 0, 0);
    add_rm(MAP_0F, 0xb2, -1, "LSS", SYSTEM, 0, 1, 0);
    add_rm(MAP_0F, 0xb4, -1, "LFS", SYSTEM, 0, 1, 0);
    add_rm(MAP_0F, 0xb5, -1, "LGS", SYSTEM, 0, 1, 0);
    for (unsigned o = 0xb6; o <= 0xbf; o++) {
        if (o == 0xb6 || o == 0xb7 || o == 0xbe || o == 0xbf)
            add_rm(MAP_0F, o, -1, o < 0xbe ? "MOVZX" : "MOVSX", INTEGER, 0, 0, 0);
        if (o == 0xbc || o == 0xbd) add_rm(MAP_0F, o, -1, o == 0xbc ? "BSF" : "BSR", INTEGER, 0, 0, 0);
    }
    add_rm(MAP_0F, 0xc0, -1, "XADD", INTEGER, F_LOCK, 0, 0);
    add_rm(MAP_0F, 0xc1, -1, "XADD", INTEGER, F_LOCK, 0, 0);
    add_rm(MAP_0F, 0xc7, 1, "CMPXCHG8B", INTEGER, F_LOCK, 1, 0);
    for (unsigned o = 0xc8; o <= 0xcf; o++) add_implicit(MAP_0F, o, "BSWAP", INTEGER, 0);

    /* MMX, including the integer extensions introduced with SSE1. */
    static const char *mm60[] = {"PUNPCKLBW", "PUNPCKLWD", "PUNPCKLDQ", "PACKSSWB",
        "PCMPGTB", "PCMPGTW", "PCMPGTD", "PACKUSWB", "PUNPCKHBW", "PUNPCKHWD", "PUNPCKHDQ", "PACKSSDW"};
    for (unsigned i = 0; i < 12; i++) add_rm(MAP_0F, 0x60 + i, -1, mm60[i], MMX, 0, 0, 0);
    add_rm(MAP_0F, 0x6e, -1, "MOVD-to-MMX", MMX, 0, 0, 0);
    add_rm(MAP_0F, 0x6f, -1, "MOVQ-to-MMX", MMX, 0, 0, 0);
    add_rm(MAP_0F, 0x70, -1, "PSHUFW", MMX, 0, 0, 0);
    static const char *mmshift[3][3] = {{"PSRLW-imm", "PSRAW-imm", "PSLLW-imm"},
        {"PSRLD-imm", "PSRAD-imm", "PSLLD-imm"}, {"PSRLQ-imm", NULL, "PSLLQ-imm"}};
    for (unsigned o = 0; o < 3; o++)
        for (unsigned g = 0; g < 3; g++)
            if (mmshift[o][g]) add_rm(MAP_0F, 0x71 + o, 2 + g * 2, mmshift[o][g], MMX, 0, 0, 1);
    add_rm(MAP_0F, 0x74, -1, "PCMPEQB", MMX, 0, 0, 0);
    add_rm(MAP_0F, 0x75, -1, "PCMPEQW", MMX, 0, 0, 0);
    add_rm(MAP_0F, 0x76, -1, "PCMPEQD", MMX, 0, 0, 0);
    add_implicit(MAP_0F, 0x77, "EMMS", MMX, 0);
    add_rm(MAP_0F, 0x7e, -1, "MOVD-from-MMX", MMX, 0, 0, 0);
    add_rm(MAP_0F, 0x7f, -1, "MOVQ-from-MMX", MMX, 0, 0, 0);
    static const char *mmd0[] = {
        NULL, "PSRLW", "PSRLD", "PSRLQ", NULL, "PMULLW", NULL, "PMOVMSKB",
        "PSUBUSB", "PSUBUSW", "PMINUB", "PAND", "PADDUSB", "PADDUSW", "PMAXUB", "PANDN",
        "PAVGB", "PSRAW", "PSRAD", "PAVGW", "PMULHUW", "PMULHW", NULL, "MOVNTQ",
        "PSUBSB", "PSUBSW", "PMINSW", "POR", "PADDSB", "PADDSW", "PMAXSW", "PXOR",
        NULL, "PSLLW", "PSLLD", "PSLLQ", NULL, "PMADDWD", "PSADBW", "MASKMOVQ",
        "PSUBB", "PSUBW", "PSUBD", NULL, "PADDB", "PADDW", "PADDD", NULL};
    for (unsigned i = 0; i < 48; i++)
        if (mmd0[i]) add_rm(MAP_0F, 0xd0 + i, -1, mmd0[i], MMX, 0, i == 0x17, i == 7 || i == 0x27);
    add_rm(MAP_0F, 0xc4, -1, "PINSRW", MMX, 0, 0, 0);
    add_rm(MAP_0F, 0xc5, -1, "PEXTRW", MMX, 0, 0, 1);

    static const char *sse10[] = {"MOVUPS-load", "MOVUPS-store", "MOVLPS-MOVHLPS", "MOVLPS-store",
        "UNPCKLPS", "UNPCKHPS", "MOVHPS-MOVLHPS", "MOVHPS-store"};
    for (unsigned i = 0; i < 8; i++) add_rm(MAP_0F, 0x10 + i, -1, sse10[i], SSE, 0, i == 3 || i == 7, 0);
    static const char *sse28[] = {"MOVAPS-load", "MOVAPS-store", "CVTPI2PS", "MOVNTPS", "CVTTPS2PI", "CVTPS2PI", "UCOMISS", "COMISS"};
    for (unsigned i = 0; i < 8; i++) add_rm(MAP_0F, 0x28 + i, -1, sse28[i], SSE, 0, i == 3, 0);
    static const char *sse50[] = {"MOVMSKPS", "SQRTPS", "RSQRTPS", "RCPPS", "ANDPS", "ANDNPS", "ORPS", "XORPS",
        "ADDPS", "MULPS", NULL, NULL, "SUBPS", "MINPS", "DIVPS", "MAXPS"};
    for (unsigned i = 0; i < 16; i++)
        if (sse50[i]) add_rm(MAP_0F, 0x50 + i, -1, sse50[i], SSE, 0, 0, i == 0);
    add_rm(MAP_0F, 0xc2, -1, "CMPPS", SSE, 0, 0, 0);
    add_rm(MAP_0F, 0xc6, -1, "SHUFPS", SSE, 0, 0, 0);
    static const char *fx[] = {"FXSAVE", "FXRSTOR", "LDMXCSR", "STMXCSR"};
    for (unsigned g = 0; g < 4; g++) add_rm(MAP_0F, 0xae, g, fx[g], SSE, 0, 1, 0);
    add_form(MAP_0F, 0xae, FORM_REGISTER, 0xff, 0xf8, "SFENCE", SSE, F_MODRM);
    static const unsigned ssops[] = {0x10, 0x11, 0x2a, 0x2c, 0x2d, 0x51, 0x52, 0x53, 0x58, 0x59, 0x5c, 0x5d, 0x5e, 0x5f, 0xc2};
    static const char *ssnames[] = {"MOVSS-load", "MOVSS-store", "CVTSI2SS", "CVTTSS2SI", "CVTSS2SI", "SQRTSS", "RSQRTSS", "RCPSS",
        "ADDSS", "MULSS", "SUBSS", "MINSS", "DIVSS", "MAXSS", "CMPSS"};
    for (unsigned i = 0; i < 15; i++) add_rm(MAP_F3_0F, ssops[i], -1, ssnames[i], SSE, 0, 0, 0);

    static const char *xmem[8][8] = {
        {"FADD-m32", "FMUL-m32", "FCOM-m32", "FCOMP-m32", "FSUB-m32", "FSUBR-m32", "FDIV-m32", "FDIVR-m32"},
        {"FLD-m32", NULL, "FST-m32", "FSTP-m32", "FLDENV", "FLDCW", "FNSTENV", "FNSTCW"},
        {"FIADD-m32", "FIMUL-m32", "FICOM-m32", "FICOMP-m32", "FISUB-m32", "FISUBR-m32", "FIDIV-m32", "FIDIVR-m32"},
        {"FILD-m32", NULL, "FIST-m32", "FISTP-m32", NULL, "FLD-m80", NULL, "FSTP-m80"},
        {"FADD-m64", "FMUL-m64", "FCOM-m64", "FCOMP-m64", "FSUB-m64", "FSUBR-m64", "FDIV-m64", "FDIVR-m64"},
        {"FLD-m64", NULL, "FST-m64", "FSTP-m64", "FRSTOR", NULL, "FNSAVE", "FNSTSW"},
        {"FIADD-m16", "FIMUL-m16", "FICOM-m16", "FICOMP-m16", "FISUB-m16", "FISUBR-m16", "FIDIV-m16", "FIDIVR-m16"},
        {"FILD-m16", NULL, "FIST-m16", "FISTP-m16", "FBLD", "FILD-m64", "FBSTP", "FISTP-m64"}
    };
    for (unsigned o = 0; o < 8; o++)
        for (unsigned g = 0; g < 8; g++)
            if (xmem[o][g]) add_rm(MAP_X87, 0xd8 + o, g, xmem[o][g], X87, 0, 1, 0);
    static const char *xreg[8][8] = {
        {"FADD", "FMUL", "FCOM", "FCOMP", "FSUB", "FSUBR", "FDIV", "FDIVR"},
        {"FLD-ST", "FXCH", NULL, "FSTP-alias", NULL, NULL, NULL, NULL},
        {"FCMOVB", "FCMOVE", "FCMOVBE", "FCMOVU", NULL, NULL, NULL, NULL},
        {"FCMOVNB", "FCMOVNE", "FCMOVNBE", "FCMOVNU", NULL, "FUCOMI", "FCOMI", NULL},
        {"FADD-STi", "FMUL-STi", "FCOM-alias", "FCOMP-alias", "FSUBR-STi", "FSUB-STi", "FDIVR-STi", "FDIV-STi"},
        {"FFREE", "FXCH-alias", "FST-ST", "FSTP-ST", "FUCOM", "FUCOMP", NULL, NULL},
        {"FADDP", "FMULP", "FCOMP-alias", NULL, "FSUBRP", "FSUBP", "FDIVRP", "FDIVP"},
        {"FFREEP", "FXCH-alias", "FSTP-alias", "FSTP-alias", NULL, "FUCOMIP", "FCOMIP", NULL}
    };
    for (unsigned o = 0; o < 8; o++)
        for (unsigned g = 0; g < 8; g++)
            if (xreg[o][g]) add_rm(MAP_X87, 0xd8 + o, g, xreg[o][g], X87,
                                  strstr(xreg[o][g], "alias") || (o == 7 && g == 0) ? F_ALIAS : 0, 0, 1);
    static const char *xd9[48] = {
        "FNOP", NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        "FCHS", "FABS", NULL, NULL, "FTST", "FXAM", NULL, NULL,
        "FLD1", "FLDL2T", "FLDL2E", "FLDPI", "FLDLG2", "FLDLN2", "FLDZ", NULL,
        "F2XM1", "FYL2X", "FPTAN", "FPATAN", "FXTRACT", "FPREM1", "FDECSTP", "FINCSTP",
        "FPREM", "FYL2XP1", "FSQRT", "FSINCOS", "FRNDINT", "FSCALE", "FSIN", "FCOS"
    };
    for (unsigned i = 0; i < 48; i++)
        if (xd9[i]) add_form(MAP_X87, 0xd9, FORM_REGISTER, 0xff, 0xd0 + i, xd9[i], X87, F_MODRM);
    add_form(MAP_X87, 0xda, FORM_REGISTER, 0xff, 0xe9, "FUCOMPP", X87, F_MODRM);
    static const char *xdb[] = {"FNENI", "FNDISI", "FNCLEX", "FNINIT", "FNSETPM"};
    for (unsigned i = 0; i < 5; i++)
        add_form(MAP_X87, 0xdb, FORM_REGISTER, 0xff, 0xe0 + i, xdb[i], X87, F_MODRM | (i == 2 || i == 3 ? 0 : F_ALIAS));
    add_form(MAP_X87, 0xdb, FORM_REGISTER, 0xff, 0xe5, "FNOP-alias", X87, F_MODRM | F_ALIAS);
    add_form(MAP_X87, 0xde, FORM_REGISTER, 0xff, 0xd9, "FCOMPP", X87, F_MODRM);
    add_form(MAP_X87, 0xdf, FORM_REGISTER, 0xff, 0xe0, "FNSTSW-AX", X87, F_MODRM);
}
#endif
