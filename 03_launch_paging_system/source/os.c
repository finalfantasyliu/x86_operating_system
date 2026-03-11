/**
 * 32-bit code for multitask execution
 */
#include "os.h"
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;

/*
 * Segment Selector（載入 CS、DS 等 segment register 的值）
 *
 * Selector 是 16-bit，不是直接的 address，而是 GDT 的 index + 屬性：
 *
 *   15                                3   2   1  0
 *   ┌────────────────────────────────┬───┬──────┐
 *   │         Index (13 bits)        │TI │ RPL  │
 *   └────────────────────────────────┴───┴──────┘
 *
 *   Index : GDT 中第幾個 entry（0~8191）
 *   TI    : 0 = GDT, 1 = LDT
 *   RPL   : Requested Privilege Level（0~3）
 *
 * KERNEL_CODE_SEG = 0x08 和 KERNEL_DATA_SEG = 0x10 的 bit-level 對比：
 *
 *   0x08 = 0000 0000 0000 1  0  00
 *          ╰── Index = 1 ──╯ TI RPL=0
 *          → GDT entry[1]，ring 0
 *
 *   0x10 = 0000 0000 0001 0  0  00
 *          ╰── Index = 2 ──╯ TI RPL=0
 *          → GDT entry[2]，ring 0
 *
 * 所以 selector / 8 = GDT index：
 *   0x08 / 8 = 1 → gdt_table[1] = Kernel Code Segment
 *   0x10 / 8 = 2 → gdt_table[2] = Kernel Data Segment
 *   0x00 / 8 = 0 → gdt_table[0] = Null Descriptor（Intel 規定必須為全 0）
 *
 * 為什麼除以 8？因為低 3 bits 是 TI + RPL，不是 index 的一部分：
 *   Index = Selector >> 3 = Selector / 8
 */

/*
 * GDT (Global Descriptor Table)
 *
 * 每個 entry 8 bytes（兩個 32-bit word），因為 286→386 的歷史包袱，欄位被拆散：
 *
 *   32-bit word 1（低位）                32-bit word 0（高位）
 *   31              16 15              0  31              16 15              0
 *   ┌─────────────────┬─────────────────┐┌─────────────────┬─────────────────┐
 *   │  Base [15:0]    │  Limit [15:0]   ││ Base[31:24]|Flg |  Attr |Base[23:16]│
 *   │  base_l         │  limit_l        ││ base_limit      |basehl_attr      │
 *   └─────────────────┴─────────────────┘└─────────────────┴─────────────────┘
 *
 *   展開 Attr 和 Flags：
 *
 *   Byte 5 (Access Byte):          Byte 6 (Flags + Limit[19:16]):
 *   ┌──┬───┬──┬──┬──┬──┬──┬──┐    ┌──┬──┬──┬──┬──┬──┬──┬──┐
 *   │P │DPL│S │E │DC│RW│A │  │    │G │D │L │  │Limit[19:16]│
 *   │1 │00 │1 │? │0 │? │0 │  │    │1 │1 │0 │0 │1111       │
 *   └──┴───┴──┴──┴──┴──┴──┴──┘    └──┴──┴──┴──┴──┴──┴──┴──┘
 *    7  6 5  4  3  2  1  0          7  6  5  4  3        0
 *
 *   P=1   : segment 存在於記憶體中
 *   DPL=00: ring 0（kernel 權限）
 *   S=1   : code/data segment（非 system descriptor）
 *   E     : 0=data segment, 1=code segment
 *   DC    : 0=向上擴展（data）/ 非 conforming（code）
 *   RW    : code→1=可讀, data→1=可寫
 *   A=0   : CPU 尚未存取過
 *   G=1   : limit 以 4KB 為單位（limit × 4KB）
 *   D=1   : 32-bit segment
 *   L=0   : 非 64-bit code segment
 *
 * ─────────────────────────────────────────────────────────────────────────────
 *
 * KERNEL_CODE_SEG（selector 0x8 → index 1）: {0xffff, 0x0000, 0x9a00, 0x00cf}
 *
 *   limit_l    = 0xFFFF  → Limit[15:0]  = 0xFFFF
 *   base_l     = 0x0000  → Base[15:0]   = 0x0000
 *   basehl_attr= 0x9A00  → Base[23:16]  = 0x00,  Access Byte = 0x9A
 *   base_limit = 0x00CF  → Base[31:24]  = 0x00,  Flags|Limit[19:16] = 0xCF
 *
 *   Base  = 0x00000000（flat model，從 0 開始）
 *   Limit = 0xFFFFF × 4KB(G=1) = 4GB
 *   Access Byte = 0x9A = 1001 1010
 *     P=1, DPL=00, S=1, E=1(code), DC=0, RW=1(readable), A=0
 *   Flags = 0xC = 1100 → G=1(4KB granularity), D=1(32-bit)
 *
 * ─────────────────────────────────────────────────────────────────────────────
 *
 * KERNEL_DATA_SEG（selector 0x10 → index 2）: {0xffff, 0x0000, 0x9200, 0x00cf}
 *
 *   limit_l    = 0xFFFF  → Limit[15:0]  = 0xFFFF
 *   base_l     = 0x0000  → Base[15:0]   = 0x0000
 *   basehl_attr= 0x9200  → Base[23:16]  = 0x00,  Access Byte = 0x92
 *   base_limit = 0x00CF  → Base[31:24]  = 0x00,  Flags|Limit[19:16] = 0xCF
 *
 *   Base  = 0x00000000（flat model，從 0 開始）
 *   Limit = 0xFFFFF × 4KB(G=1) = 4GB
 *   Access Byte = 0x92 = 1001 0010
 *     P=1, DPL=00, S=1, E=0(data), DC=0(expand-up), RW=1(writable), A=0
 *   Flags = 0xC = 1100 → G=1(4KB granularity), D=1(32-bit)
 *
 *   與 code segment 唯一差異：E bit（bit 3）
 *     Code: 0x9A = 1001 _1_010  → E=1
 *     Data: 0x92 = 1001 _0_010  → E=0
 */

struct {
    uint16_t limit_l;      /* Limit [15:0] */
    uint16_t base_l;       /* Base [15:0] */
    uint16_t basehl_attr;  /* Base [23:16] | Access Byte (P, DPL, S, Type) */
    uint16_t base_limit;   /* Base [31:24] | Flags (G, D/B, L) | Limit [19:16] */
} gdt_table[256] __attribute__((aligned(8))) = {
    /* Entry 0: Null descriptor（Intel 規定 GDT 第 0 個 entry 必須為全 0） */

    /* Entry 1 (selector 0x08): Kernel Code Segment — base=0, limit=4GB, execute/read, ring 0 */
    [KERNEL_CODE_SEG / 8] = {0xffff, 0x0000, 0x9a00, 0x00cf},

    /* Entry 2 (selector 0x10): Kernel Data Segment — base=0, limit=4GB, read/write, ring 0 */
    [KERNEL_DATA_SEG / 8] = {0xffff, 0x0000, 0x9200, 0x00cf},
};
