/**
 * 16-bit and 32-bit mixed startup code
 */
#include "os.h"

/* Declare _start as a global symbol so it can be referenced from other files */
.global _start

/* Generate 16-bit machine instructions for real mode execution at boot */
.code16

/* Use Intel assembly syntax (dest, src) without % register prefix */
.intel_syntax noprefix

/* Text (code) section */

.text
_start:
    /* 將所有 segment register 清為 0，建立 real mode 下的 flat 環境
     * DS=ES=SS=FS=GS=0，所有 segment:offset 存取都以 0 為 base
     * 確保後續記憶體存取的 physical address = offset（不受殘留值影響） */
    xor     ax, ax
    mov     ds, ax    
    mov     es, ax
    mov     ss, ax
    mov     fs, ax
    mov     gs, ax

    /* Stack 從 0x7C00 向下生長（_start = 0x7C00）
     * 使用 offset 關鍵字：Intel 語法下，bare label 可能被解讀為 memory reference
     * 加 offset 明確表示「取 label 的地址值」而非「讀取該地址的內容」 */
    mov     esp, offset _start 

read_self_all:
    /* INT 0x13 AH=02h: Read Sectors From Drive
     *   ES:BX = 讀取目標的 buffer address（ES=0，BX=_start_32=0x7E00）
     *   CH = cylinder number（0）
     *   CL = 起始 sector number（2，因為 sector 1 就是目前的 boot sector，已在 0x7C00）
     *   AH = 02h（read sectors 功能）
     *   AL = 讀取的 sector 數量（0x40 = 64 sectors = 32KB）
     *   DH = head number（0）
     *   DL = drive number（0x80 = 第一個硬碟）
     * 讀取失敗時 CF=1，jc 重試 */
    mov bx, offset _start_32
    mov cx, 0x2
    mov ax, 0x240
    mov dx, 0x80
    int 0x13
    jc read_self_all
    
    /* 關閉中斷，避免在切換 protected mode 過程中被打斷
     * 此時 IDT 尚未設定，任何中斷都會導致 triple fault */
    cli

    /* 載入 GDT descriptor 到 GDTR register
     * lgdt 從 [gdt_desc] 讀取 6 bytes：
     *   - 前 2 bytes（.word）= GDT limit（GDT 大小 - 1）
     *   - 後 4 bytes（.long）= GDT base address（gdt_table 的 linear address）
     * 用 [...] 是因為 lgdt 需要從記憶體讀取，不需要加 offset */
    lgdt [gdt_desc]

    /* 設定 CR0.PE = 1 進入 protected mode
     *
     * 這裡用 mov cr0 而非 lmsw，兩者的差異：
     *
     *   lmsw ax:
     *     - 286 時代的指令，只寫入 CR0 的低 16 bits（bit 15~0）
     *     - 且 PE bit 一旦被設為 1 就無法用 lmsw 清回 0（286 沒有退出 protected mode 的機制）
     *     - 如果用 mov eax, 1; lmsw eax，會把 CR0[15:1] 全部清為 0
     *       可能覆蓋 BIOS 已設定的 bit（如 ET=1 for 387 coprocessor type）
     *
     *   mov cr0, eax:
     *     - 386+ 的指令，寫入完整的 32 bits
     *     - 可以用 read-modify-write（先讀 cr0，or 上 PE bit，再寫回）
     *     - 只改 PE bit，保留其他所有 bit 的原始值
     *     - 也可以清除 PE bit 退回 real mode（lmsw 做不到）
     */
    mov eax, cr0
    or  eax, 1
    mov cr0, eax

    /* Far jump: 同時設定 CS = KERNEL_CODE_SEG（0x8）和 EIP = _start_32
     * 目的：(1) 將 CS 從 real mode 值切換為 protected mode 的 selector
     *       (2) flush CPU pipeline 中殘留的 16-bit 指令
     *       (3) 跳到 .code32 的 32-bit 程式碼 */
    jmp KERNEL_CODE_SEG, _start_32

    /* 填充 NOP（0x90）到 offset 0x1FE，加上 2-byte boot signature
     * 確保 boot sector 恰好 512 bytes，且最後兩個 byte 為 0x55AA */
    .org 0x1FE, 0x90
    .byte 0x55, 0xAA

    /* 從此處開始產生 32-bit 指令 */
    .code32
    .text
_start_32:
    /* 進入 protected mode 後，DS/SS/ES/FS/GS 仍殘留 real mode 的值
     * 必須重新載入為 protected mode 的 data segment selector（0x10）
     * 注意：CS 已經由上面的 far jump 設為 KERNEL_CODE_SEG（0x8）
     * DS/SS/ES 不能用 code segment（type=0x9A 不允許載入 DS），必須用 data segment */
    mov ax, KERNEL_DATA_SEG
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    /* Stack 從 0x7C00 向下生長（與 real mode 相同位置）
     * Protected mode 的 flat model 下 base=0，所以 linear address = offset = physical address */
    mov esp, offset _start

    /* 暫停：目前尚未實作 kernel entry point，避免 CPU fall through 到下方的 gdt_desc 資料
     * gdt_desc 是資料不是指令，如果不停下來 CPU 會把它當指令執行 → 未定義行為 */
    jmp .


/* GDT Descriptor（GDTR 的內容）
 * lgdt 指令從這裡讀取 6 bytes 載入 GDTR register
 *
 *   低地址                                 高地址
 *   ┌─────────────────┬──────────────────────────────────┐
 *   │  Limit (2 bytes) │  Base Address (4 bytes)          │
 *   │  .word           │  .long                           │
 *   └─────────────────┴──────────────────────────────────┘
 *
 *   Limit = (256 entries × 8 bytes/entry) - 1 = 2047
 *   Base  = gdt_table 的 linear address（定義在 os.c）
 */
gdt_desc:
    .word (256*8)-1
    .long gdt_table
