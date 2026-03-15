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

uint8_t map_phy_buffer[4096]__attribute__((aligned(4096)))={0x36};

/*
 * Page Directory Entry (PDE) flags
 *
 * 當 CR4.PSE=1 且 PDE.PS=1 時，PDE 直接映射 4MB page（不經過 page table）：
 *
 *   31              22 21       13 12 11    9 8  7  6  5  4  3  2  1  0
 *   ┌─────────────────┬──────────┬──┬───────┬──┬──┬──┬──┬──┬──┬──┬──┬──┐
 *   │ Base [31:22]    │ 保留(0)  │PAT│ Avail │G │PS│D │A │CD│WT│U │W │P │
 *   └─────────────────┴──────────┴──┴───────┴──┴──┴──┴──┴──┴──┴──┴──┴──┘
 *
 *   P  (bit 0): Present — 1 = page 在記憶體中
 *   W  (bit 1): Read/Write — 1 = 可寫
 *   U  (bit 2): User/Supervisor — 1 = ring 3 可存取
 *   WT (bit 3): Write-Through — 1 = write-through cache
 *   CD (bit 4): Cache Disable — 1 = 不 cache
 *   A  (bit 5): Accessed — CPU 存取過時自動設為 1
 *   D  (bit 6): Dirty — CPU 寫過時自動設為 1
 *   PS (bit 7): Page Size — 1 = 4MB page（需 CR4.PSE=1），0 = 指向 page table
 *   G  (bit 8): Global — 1 = CR3 切換時不 flush 此 TLB entry
 *   Base [31:22]: 4MB page 的 physical base address（必須 4MB 對齊，低 22 bits = 0）
 */
#define PDE_P   (1 << 0)
#define PDE_W   (1 << 1)
#define PDE_U   (1 << 2)
#define PDE_PS  (1 << 7)

/*
 * Page Directory — 1024 entries，每個管 4MB，共覆蓋 4GB
 *
 * 目前只設定 entry[0]：identity map 前 4MB（virtual 0~4MB = physical 0~4MB）
 *
 *   entry[0] = 0x00000000 | PDE_P | PDE_W | PDE_U | PDE_PS
 *            = 0x00000087
 *
 *   Base [31:22] = 0x000 → physical base = 0x00000000
 *   PS=1 → 4MB page（不查 page table）
 *   P=1, W=1, U=1 → present, writable, user-accessible
 *
 *   映射結果：
 *     virtual  0x00000000 ~ 0x003FFFFF  →  physical 0x00000000 ~ 0x003FFFFF
 *     （涵蓋 0x7C00 boot code 所在位置，所以開啟 paging 後程式繼續正常執行）
 *
 *   entry[1~1023] = 0 → P=0，存取這些範圍會觸發 page fault
 *
 * aligned(4096)：page directory 必須 4KB（0x1000）對齊，
 * 因為 CR3 只取 bits[31:12] 當 base address，低 12 bits 是 flags/reserved
 */
//這個是只有1-level用來演示
/*uint32_t page_dir[1024] __attribute__((aligned(4096))) = {
    [0] = (0) | PDE_P | PDE_W | PDE_U | PDE_PS,
};*/