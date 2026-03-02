# 02b - 載入剩餘磁區並進入 Protected Mode 完全解析

本篇涵蓋從 bootloader 載入磁區、進入 Protected Mode 到 80386 Segment-Level Protection 的所有知識點。

---

## Table of Contents

**Part 1: 組合語言基礎**
1. [Segment Register 為什麼不能直接賦值？](#1-segment-register-為什麼不能直接賦值)
2. [Segment:Offset 配對總整理](#2-segmentoffset-配對總整理)
3. [.S 檔案中什麼會變成機器碼？](#3-s-檔案中什麼會變成機器碼)
4. [Assembly 中的 Label 是什麼？](#4-assembly-中的-label-是什麼)
5. [.code16 / .code32 的作用範圍](#5-code16--code32-的作用範圍)
6. [.org / .byte 的位置與程式碼組織](#6-org--byte-的位置與程式碼組織)

**Part 2: BIOS 中斷與磁碟讀取**
7. [BIOS 中斷的 AH 慣例](#7-bios-中斷的-ah-慣例)
8. [INT 0x13 磁碟讀取詳解](#8-int-0x13-磁碟讀取詳解)

**Part 3: CPU 演進與模式切換**
9. [x86 CPU 演進史：8086 → 286 → 386 → 現代](#9-x86-cpu-演進史8086--286--386--現代)
10. [Real Mode → Protected Mode → Long Mode](#10-real-mode--protected-mode--long-mode)
11. [UEFI 在開機流程中的角色](#11-uefi-在開機流程中的角色)
12. [如何進入 Protected Mode](#12-如何進入-protected-mode)
13. [lmsw 與 mov cr0 的差異](#13-lmsw-與-mov-cr0-的差異)

**Part 4: Segment Descriptor 與 GDT**
14. [Segment Descriptor 結構與歷史包袱](#14-segment-descriptor-結構與歷史包袱)
15. [286 的 16-bit Limit 為什麼合理？](#15-286-的-16-bit-limit-為什麼合理)
16. [4GB 是每個 Segment 的上限還是總共的？](#16-4gb-是每個-segment-的上限還是總共的)
17. [GDT、LDT 與 8192 Descriptors](#17-gdtldt-與-8192-descriptors)
18. [GDT Descriptor（gdt_desc）的格式解析](#18-gdt-descriptorgdt_desc的格式解析)
19. [Protected Mode 中 CS 是 Selector，那 Offset 是什麼？](#19-protected-mode-中-cs-是-selector那-offset-是什麼)

**Part 5: Segment-Level Protection**
20. [為什麼需要保護？](#20-為什麼需要保護)
21. [Type 欄位詳解（S、E、ED/C、R/W、A）](#21-type-欄位詳解sedc-rw-a)
22. [System Descriptor（S=0）的用途](#22-system-descriptors0的用途)
23. [Privilege Level：CPL、DPL、RPL](#23-privilege-levelcpldplrpl)
24. [五種保護機制](#24-五種保護機制)
25. [Gate Descriptor（閘門描述符）](#25-gate-descriptor閘門描述符)
26. [Conforming Code Segment 到底是什麼？](#26-conforming-code-segment-到底是什麼)
27. [Segment Register 的類型限制](#27-segment-register-的類型限制)
28. [指令執行時的完整流程](#28-指令執行時的完整流程)

**Part 6: Stack 與 TSS**
29. [SS、ESP、EBP 的關係](#29-ssespebp-的關係)
30. [Stack Switching 與 TSS](#30-stack-switching-與-tss)

**Part 7: 現代 OS 還用這些嗎？**
31. [現代 OS 中哪些機制還在用？](#31-現代-os-中哪些機制還在用)

**Part 8: 參考資料**
32. [References](#32-references)

---

# Part 1: 組合語言基礎

---

## 1. Segment Register 為什麼不能直接賦值？

### 問題

```asm
mov ds, 0x0000    ; ❌ 不合法！
mov ds, ax        ; ✅ 必須透過 general-purpose register
```

為什麼 segment register 不能直接用 immediate value 賦值？一定要用 AX 嗎？

### 答案

這是 **CPU ISA（Instruction Set Architecture）的設計**。Intel 從 8086 開始就沒有定義 `mov segment_register, immediate` 這個 opcode。CPU 只認得 `mov segment_register, r/m16`（從暫存器或記憶體載入）。

不是只能用 AX，**任何 16-bit general-purpose register 都可以**：

```asm
mov ax, 0x1000
mov ds, ax        ; ✅ 用 AX

mov bx, 0x1000
mov ds, bx        ; ✅ 用 BX

mov cx, 0x1000
mov ds, cx        ; ✅ 用 CX

mov dx, 0x1000
mov ds, dx        ; ✅ 用 DX
```

也可以從記憶體載入：

```asm
mov ds, [some_memory_location]    ; ✅ 從記憶體載入
```

用 AX 只是慣例，因為 AX 是最常用的累加器。

> **Reference**: Intel 64 and IA-32 Architectures Software Developer's Manual, Volume 2, "MOV" instruction:
> "MOV Sreg, r/m16 — Move r/m16 to segment register."
> 沒有 "MOV Sreg, imm16" 的 encoding。

---

## 2. Segment:Offset 配對總整理

### 預設配對

| Segment Register | 預設 Offset Register | 用途 |
|---|---|---|
| **CS** | **IP/EIP** | 指令提取（Instruction Fetch）— CPU 自動使用，不能手動 `mov cs, xx` |
| **SS** | **SP/ESP, BP/EBP** | 堆疊操作 — `push`、`pop`、`call`、`ret` 及 `[bp+xx]` 定址 |
| **DS** | **SI, BX, DI** 及大部分定址 | 一般資料存取 — `mov ax, [bx+4]`、`mov [si], al` 等 |
| **ES** | **DI**（字串指令目的地） | `movsb`、`stosb`、`scasb` 等字串指令的目的地 |
| **FS** | 任意 | 額外的資料段（386 新增），現代 OS 常用來指向 thread-local storage |
| **GS** | 任意 | 額外的資料段（386 新增），Linux kernel 用它指向 per-CPU data |

### 重要釐清：ES:BX vs ES:DI

ES:DI 是**字串指令**（如 `movsb`、`stosb`）的硬性規定，由 CPU 微架構決定。

但 BIOS `int 0x13` 讀取磁碟時使用的是 **ES:BX**，這是 **BIOS 的軟體慣例**，不是 CPU 硬體規定：

```asm
; BIOS int 0x13, AH=02h — 讀取磁碟
; ES:BX = 資料載入的目標位址（BIOS 的約定）
mov bx, _start_32    ; offset
; ES 已經設為 0
int 0x13             ; BIOS 讀取到 ES:BX = 0x0000:_start_32
```

> **Reference**: Ralph Brown's Interrupt List, INT 13h AH=02h:
> "ES:BX -> buffer for data"

---

## 3. .S 檔案中什麼會變成機器碼？

### 會變成實際 binary 內容的

| 類別 | 範例 | 說明 |
|---|---|---|
| **CPU 指令** | `mov ax, 0`, `jmp label`, `int 0x13` | 組譯為機器碼 |
| **資料指示** | `.byte 0x55, 0xAA`, `.word 0x1234`, `.long`, `.string`, `.fill` | 直接放入 binary |
| **對齊/填充** | `.org 0x1FE, 0x90` | 用指定值填充到指定位置 |

### 不會變成 binary 內容的

| 類別 | 範例 | 說明 |
|---|---|---|
| **Label** | `_start:`, `read_self_all:` | 只是位址的別名，由 assembler/linker 解析 |
| **模式指示** | `.code16`, `.code32` | 告訴 assembler 產生哪種位元寬度的指令 |
| **段落指示** | `.text`, `.data`, `.bss` | 告訴 assembler 把後續內容放在哪個 section |
| **符號指示** | `.global _start` | 告訴 linker 這個符號是全域可見的 |
| **語法指示** | `.intel_syntax noprefix` | 告訴 assembler 使用 Intel 語法 |
| **註解** | `// 這是註解`, `/* */` | 完全忽略 |
| **預處理** | `#include "os.h"`, `#define` | gcc preprocessor 處理，assembler 看不到 |

### 以 start.S 為例

```asm
#include "os.h"              ; ← 預處理器處理，不進 binary
.global _start               ; ← linker 指示，不進 binary
.code16                      ; ← assembler 指示，不進 binary
.intel_syntax noprefix       ; ← assembler 指示，不進 binary
.text                        ; ← section 指示，不進 binary
_start:                      ; ← label，不進 binary（但代表此處位址 0x7C00）
    xor     ax, ax           ; ← ✅ 機器碼：31 C0
    mov     ds, ax           ; ← ✅ 機器碼：8E D8
```

---

## 4. Assembly 中的 Label 是什麼？

Label 就是**位址的別名**。它本身不產生任何機器碼，只是在組譯/連結時被解析為一個數值（位址）。

```asm
; 假設 linker script 指定 . = 0x7C00
_start:                   ; _start = 0x7C00
    xor ax, ax            ; 2 bytes
    mov ds, ax            ; 2 bytes
    ...
_start_32:                ; _start_32 = 0x7C00 + 512 = 0x7E00（在 boot signature 之後）
```

所以當你寫：

```asm
mov bx, _start_32
```

assembler/linker 會把 `_start_32` 替換為它的實際位址值（例如 `0x7E00`），就等同於：

```asm
mov bx, 0x7E00
```

在 flat model（base = 0）下，label 的值 = offset = 最終線性位址。

---

## 5. .code16 / .code32 的作用範圍

`.code16` 和 `.code32` 的作用範圍是**從它出現的位置到下一個 `.code16` 或 `.code32` 為止**。

```asm
.code16                    ; ← 從這裡開始，所有指令產生 16-bit 機器碼
_start:
    xor ax, ax             ; 16-bit 指令
    mov ds, ax             ; 16-bit 指令
    ...
    jmp KERNEL_CODE_SEG:_start_32

    .org 0x1FE, 0x90
    .byte 0x55, 0xAA

    .code32                ; ← 從這裡開始，所有指令產生 32-bit 機器碼
_start_32:
    jmp .                  ; 32-bit 指令
```

它只影響 **assembler 產生的機器碼編碼方式**，不會在 binary 裡產生任何內容。

---

## 6. .org / .byte 的位置與程式碼組織

### 問題

`.org 0x1FE` 和 `.byte 0x55, 0xAA` 卡在 `enter_protected_mode` 和 `.code32` 之間，讀起來很混亂。有沒有更好的寫法？

### 方法一：用 Section 分離

```asm
.section .boot_sig, "a"    ; 獨立的 section
    .org 0x1FE, 0x90
    .byte 0x55, 0xAA

.section .text32, "ax"     ; 32-bit code 獨立 section
    .code32
_start_32:
    jmp .
```

然後在 linker script 裡控制排列順序：

```ld
.text : {
    *(.text)
    *(.boot_sig)
    *(.text32)
}
```

### 方法二：用獨立的 .S 檔案

```
source/
├── boot16.S     ← 16-bit code + boot signature
├── start32.S    ← 32-bit code
├── os.c         ← GDT table
└── os.h         ← 共用常數
```

### 方法三：用清晰的註解區隔（最簡單）

```asm
/* ============================================================
 * Boot Signature — 必須在 offset 0x1FE
 * ============================================================ */
    .org 0x1FE, 0x90
    .byte 0x55, 0xAA

/* ============================================================
 * 32-bit Protected Mode Code — 從第二個 sector 開始
 * ============================================================ */
    .code32
    .text
_start_32:
    jmp .
```

---

# Part 2: BIOS 中斷與磁碟讀取

---

## 7. BIOS 中斷的 AH 慣例

BIOS 中斷（`int` 指令）使用 **AH 暫存器** 來選擇功能編號。每個中斷號碼是一個「服務類別」，AH 是「該類別中的具體功能」。

### INT 0x10 — 螢幕服務

| AH | 功能 | 參數 |
|---|---|---|
| 0x00 | 設定螢幕模式 | AL = 模式編號 |
| 0x0E | Teletype 輸出（印一個字元） | AL = 字元, BH = page |
| 0x13 | 寫字串 | ES:BP = 字串, CX = 長度 |

### INT 0x13 — 磁碟服務

| AH | 功能 | 參數 |
|---|---|---|
| 0x00 | 重置磁碟系統 | DL = 磁碟編號 |
| 0x02 | 讀取磁區 | AL = 磁區數, CH = cylinder, CL = sector, DH = head, DL = drive, ES:BX = buffer |
| 0x03 | 寫入磁區 | 同上 |
| 0x08 | 取得磁碟參數 | DL = drive |

### 為什麼用 AH 而不是其他暫存器？

這是 IBM PC BIOS 從 1981 年定下的慣例。AX 暫存器可以拆成 AH（高 8 位）和 AL（低 8 位），AH 選功能，AL 放附加參數，一個暫存器完成兩件事。

---

## 8. INT 0x13 磁碟讀取詳解

### start.S 中的磁碟讀取程式碼

```asm
read_self_all:
    mov bx, _start_32    ; ES:BX = 載入目標位址（0x0000:0x7E00）
    mov cx, 0x2          ; CH=0（cylinder 0），CL=2（從 sector 2 開始）
    mov ax, 0x240        ; AH=02（讀取功能），AL=0x40（讀 64 個 sectors = 32KB）
    mov dx, 0x80         ; DH=0（head 0），DL=0x80（第一顆硬碟）
    int 0x13
    jc read_self_all     ; CF=1 表示失敗，重試
```

### CHS 定址（Cylinder-Head-Sector）

```
硬碟結構：

      ┌──── 圓盤 (Platter) ────┐
      │                        │
      │   Track 0 ─────╮      │  ← 最外圈
      │   Track 1 ───╮ │      │
      │   Track 2 ─╮ │ │      │
      │            ▼ ▼ ▼       │  ← Head 0（正面）
      │   ┌─┬─┬─┬─┬─┐         │
      │   │ │ │ │ │ │  sectors │  ← 每個 track 分成多個 sector
      │   └─┴─┴─┴─┴─┘         │
      │            ▲ ▲ ▲       │  ← Head 1（反面）
      │   Track 0 ─╯ │ │      │
      │   Track 1 ───╯ │      │
      │   Track 2 ─────╯      │
      └────────────────────────┘

Cylinder = 正面 Track N + 反面 Track N（同一半徑的所有 track）
```

- **Cylinder（磁柱）**：同一半徑上所有磁面的 track 集合（包含正面和反面）
- **Head（磁頭）**：選哪一面（0 = 正面，1 = 反面）
- **Sector（磁區）**：track 上的第幾個區塊（從 **1** 開始，不是 0）
- 每個 sector = **512 bytes**

### 為什麼從 sector 2 開始？

Sector 1 就是 boot sector（已經被 BIOS 載入到 0x7C00 了），所以剩餘的 code 從 sector 2 開始讀取。

### AL = 0x40（64 sectors）會跨 track 嗎？

如果一個 track 只有 63 個 sector（常見設定），讀 64 個 sector 理論上會跨 track。在真實硬體的舊 BIOS 上可能出問題，但在 QEMU 模擬環境中可以正常運作。

### 如果 ES 不是 0 會怎樣？

```
載入位址 = ES × 16 + BX
```

如果 ES = 0x1000、BX = 0x7E00：

```
載入位址 = 0x1000 × 16 + 0x7E00 = 0x10000 + 0x7E00 = 0x17E00
```

資料會載入到錯誤的位址，後面跳轉到 `_start_32`（0x7E00）時，那裡沒有有效的 code，CPU 就當掉了。所以 `_start` 開頭要先把 ES 清零。

---

# Part 3: CPU 演進與模式切換

---

## 9. x86 CPU 演進史：8086 → 286 → 386 → 現代

### 8086（1978）— Real Mode 唯一模式

| 項目 | 規格 |
|---|---|
| 位元寬度 | 16-bit |
| 定址空間 | 1MB（20-bit address bus） |
| 模式 | Real Mode（唯一模式） |
| Segment 機制 | segment × 16 + offset，無保護 |

> **Reference**: Intel iAPX 86/88 User's Manual (1981):
> "The 8086 provides a 20-bit address to memory which locates any byte in a 1 megabyte memory space."

### 80286（1982）— 第一代 Protected Mode

| 項目 | 規格 |
|---|---|
| 位元寬度 | 16-bit |
| 定址空間 | 16MB（24-bit address bus） |
| 新增模式 | Protected Mode |
| Segment Descriptor | 6 bytes（base 24-bit，limit 16-bit） |
| 重大缺陷 | **進入 Protected Mode 後無法返回 Real Mode**（需重啟 CPU） |

> **Reference**: Intel 80286 Programmer's Reference Manual (1985):
> "The 80286 has two modes of operation: real address mode and protected virtual address mode."
> "The 80286 supports 16 megabytes of physical memory space and 1 gigabyte of virtual address space per task."

286 的 Descriptor 是 6 bytes，byte 6 和 byte 7 被標記為 "reserved, must be 0"。386 後來把新增的 bits 就塞進這兩個 reserved bytes，所以 386 的 descriptor 格式看起來很亂——這是歷史包袱。

### 80386（1985）— 32-bit Protected Mode + Paging

| 項目 | 規格 |
|---|---|
| 位元寬度 | 32-bit |
| 定址空間 | 4GB（32-bit address bus） |
| 新增功能 | 32-bit Protected Mode、Paging、V86 Mode |
| Segment Descriptor | 8 bytes（base 32-bit，limit 20-bit + G bit） |
| 修復缺陷 | 可以從 Protected Mode 返回 Real Mode |

> **Reference**: Intel 80386 Programmer's Reference Manual (1986), Section 2.1:
> "The 80386 has three modes of operation: Protected Mode, Real-Address Mode, and Virtual 8086 Mode."

> **Reference**: Intel 80386 Manual, Section 2.5:
> "In protected mode, the 386 can address up to 4 gigabytes of physical memory and 64 terabytes of virtual memory."

### x86-64 / AMD64（2003）— Long Mode

| 項目 | 規格 |
|---|---|
| 位元寬度 | 64-bit |
| 虛擬定址 | 256TB（48-bit，現已擴展到 57-bit） |
| 實體定址 | 依 CPU 實作，通常 40-52 bit |
| Segmentation | **幾乎廢除**，base 強制為 0（FS、GS 除外） |

> **Reference**: AMD64 Architecture Programmer's Manual, Volume 2, Section 1.2:
> "In 64-bit mode, segmentation is disabled. The segment base is treated as zero."

---

## 10. Real Mode → Protected Mode → Long Mode

```
開機
  │
  ▼
Real Mode（16-bit）
  │ CPU 上電時的預設模式
  │ 沒有任何保護
  │ 最大 1MB 記憶體
  │
  │ ← 設定 GDT → 設定 CR0.PE = 1 → Far Jump
  ▼
Protected Mode（32-bit）
  │ 有 segment-level protection
  │ 可用 4GB 記憶體
  │ 可啟用 paging
  │
  │ ← 啟用 PAE → 設定 page tables → 設定 CR0.PG、EFER.LME → Far Jump
  ▼
Long Mode（64-bit）
  │ 虛擬定址 256TB+
  │ segmentation 幾乎廢除
  │ paging 必須啟用
```

Protected Mode 並沒有被「捨棄」或「跳過」。即使目標是 Long Mode，CPU 仍然必須先經過 Protected Mode（至少短暫地）。Long Mode 是 Protected Mode 的**擴展**，不是替代。

---

## 11. UEFI 在開機流程中的角色

### 傳統 BIOS 開機

```
CPU 上電 → Real Mode
  → BIOS 載入 boot sector 到 0x7C00
  → bootloader 自己建 GDT
  → bootloader 自己進 Protected Mode
  → bootloader 自己進 Long Mode
  → 跳到 kernel
```

你需要自己寫所有模式轉換的 code。這就是我們這個專案在做的事。

### UEFI 開機

```
CPU 上電 → Real Mode
  → UEFI firmware 自己完成 Real → Protected → Long Mode
  → UEFI firmware 建立好 page tables、GDT、IDT
  → 載入 EFI application（.efi 檔案）
  → 已經在 64-bit Long Mode，有 flat memory model
  → 呼叫 EFI application 的 entry point
```

UEFI firmware 把所有骯髒的模式切換工作都做完了。EFI application 拿到的是一個已經設定好的 64-bit 環境，還有 UEFI 提供的服務函數（Boot Services）可以呼叫。

---

## 12. 如何進入 Protected Mode

進入 Protected Mode 需要 4 個步驟（Intel 規定的）：

### Step 1: cli — 關閉中斷

```asm
cli
```

為什麼？因為 Real Mode 的中斷向量表（IVT）在 0x0000:0x0000，裡面的 handler 都是 16-bit Real Mode code。如果在切換到 Protected Mode 的過程中發生中斷，CPU 會用 Real Mode 的方式去處理中斷，但 CPU 此時已經處於 Protected Mode，造成整個系統崩潰。

### Step 2: lgdt — 載入 GDT

```asm
lgdt [gdt_desc]
```

告訴 CPU GDT 在哪裡、有多大。`gdt_desc` 是一個 6-byte 的資料結構：

```
gdt_desc:
.word (256*8) - 1    ; 2 bytes: GDT 的 limit
.long gdt_table      ; 4 bytes: GDT 的 base address
```

### Step 3: 設定 CR0.PE = 1

```asm
mov eax, 1
lmsw eax
```

CR0 暫存器的 bit 0 是 PE（Protection Enable）。將它設為 1，CPU 就進入 Protected Mode。

### Step 4: Far Jump — 刷新 Pipeline

```asm
jmp KERNEL_CODE_SEG:_start_32
```

為什麼需要 far jump？兩個原因：

1. **Pipeline Flush**：CPU 的 pipeline 裡可能還有用 Real Mode 方式解碼的指令。far jump 強制 CPU 清空 pipeline，重新用 Protected Mode 方式取指令。
2. **載入 CS**：far jump 會把新的 selector（`KERNEL_CODE_SEG` = 0x08）載入 CS，CPU 會去 GDT 查找對應的 descriptor，把 base、limit、type 等資訊 cache 到 CS 的隱藏部分。

> **Reference**: Intel 80386 Manual, Section 10.3 "Switching to Protected Mode":
> "The instructions that immediately follow the MOV CR0 instruction should be a JMP or CALL."

> **注意**：far jump 的語法是 `jmp segment:offset`，不是 `jmp offset:segment`。

---

## 13. lmsw 與 mov cr0 的差異

### lmsw（Load Machine Status Word）

```asm
mov eax, 1
lmsw eax      ; 只修改 CR0 的低 16 bits
```

`lmsw` 是 286 時代的指令。它只能修改 CR0 的低 16 bits（也就是 286 的 MSW — Machine Status Word）。

**特別限制**：一旦用 `lmsw` 把 PE bit 設為 1，你**不能用 `lmsw` 把它清回 0**。這是 286 設計的遺留——286 無法返回 Real Mode。

### mov cr0（386 新增）

```asm
mov eax, cr0    ; 先讀出 CR0 的完整值
or eax, 1       ; 設定 PE bit
mov cr0, eax    ; 寫回
```

`mov cr0` 是 386 新增的，可以修改 CR0 的全部 32 bits，包括：

| Bit | 名稱 | 說明 |
|---|---|---|
| 0 | PE | Protection Enable |
| 16 | WP | Write Protect（Ring 0 也不能寫 read-only page） |
| 31 | PG | Paging Enable |

用 `mov cr0` 可以把 PE 清回 0（返回 Real Mode），這是 286 做不到的。

我們的 code 用 `lmsw` 是因為只需要設定 PE bit，而且只是進入 Protected Mode（不需要返回），所以 `lmsw` 就夠了。如果之後要啟用 Paging（PG bit 在 bit 31），就必須用 `mov cr0`。

---

# Part 4: Segment Descriptor 與 GDT

---

## 14. Segment Descriptor 結構與歷史包袱

### 8 bytes 的結構

```
Byte 7  Byte 6  Byte 5  Byte 4  Byte 3  Byte 2  Byte 1  Byte 0
┌───────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┐
│Base   │G D 0 A│P DPL S│Base   │Base Address    │Segment Limit  │
│31..24 │  Limit│ Type  │23..16 │15..0           │15..0          │
│       │19..16 │       │       │                │               │
└───────┴───────┴───────┴───────┴───────┴───────┴───────┴───────┘
```

### 為什麼 Base 和 Limit 被拆得這麼散亂？

```
286 的 Descriptor（6 bytes）：
Byte 5  Byte 4  Byte 3  Byte 2  Byte 1  Byte 0
┌───────┬───────┬───────┬───────┬───────┬───────┐
│ 0 0   │Access │Base   │Base   │Limit  │Limit  │
│(rsrvd)│Rights │23..16 │15..0  │15..0  │       │
└───────┴───────┴───────┴───────┴───────┴───────┘
  ↑ ↑
  這兩個 byte 是 reserved, must be 0
```

386 要支援 32-bit base 和 20-bit limit，但又要向後相容 286 的 descriptor 格式，只好把新增的 bits 塞進 286 標記為 reserved 的 byte 5 和 byte 7：

- Byte 6：放 Limit[19:16]（高 4 bit）+ G、D/B、AVL flags
- Byte 7：放 Base[31:24]

所以 386 的 descriptor 看起來 Base 和 Limit 被拆得到處都是——這就是歷史包袱。

> **Reference**: Intel 80386 Manual, Section 5.1:
> "The 80286 descriptor is 8 bytes long, but only 6 bytes are used. The remaining two bytes are reserved and should be zero. The 80386 uses those reserved bytes."

### 對應到 os.c 的 GDT 定義

```c
struct gdt_entry16 {
    uint16_t limit_low;          // bytes 0-1: Limit[15:0]
    uint16_t base_low;           // bytes 2-3: Base[15:0]
    uint16_t base_mid_access;    // byte 4: Base[23:16], byte 5: Access Rights
    uint16_t flags_limit_basehi; // byte 6: G|D|0|AVL|Limit[19:16], byte 7: Base[31:24]
} gdt_table[256] = {
    [KERNEL_CODE_SEG/8] = {0xffff, 0x0000, 0x9a00, 0x00cf},
    [KERNEL_DATA_SEG/8] = {0xffff, 0x0000, 0x9200, 0x00cf},
};
```

以 Kernel Code Segment（0x9a00, 0x00cf）為例拆解：

```
Access byte (0x9A) = 1001 1010
  P=1（Present）
  DPL=00（Ring 0）
  S=1（code/data segment，不是 system descriptor）
  Type=1010（Code, Execute/Read）

Flags + Limit high (0xCF) = 1100 1111
  G=1（4KB granularity）
  D=1（32-bit default operand size）
  0（reserved）
  AVL=0
  Limit[19:16]=1111

完整 Limit = 0xFFFFF，G=1 → 實際大小 = 4GB
完整 Base = 0x00000000 → 從位址 0 開始
```

---

## 15. 286 的 16-bit Limit 為什麼合理？

### 問題

286 有 24-bit base（可以放在 16MB 的任何位置），但 limit 只有 16-bit（最大 64KB）。這不是很不合理嗎？記憶體有 16MB 但每個 segment 只能用 64KB？

### 答案

**因為 286 的 offset register 也是 16-bit**。

```
記憶體存取 = Base + Offset
                      ↑
                 這個 offset 來自 16-bit register（IP、SP、BX、SI、DI...）
                 16-bit register 最大值 = 0xFFFF = 65535
```

即使你把 limit 設成 1MB，程式也沒辦法產生超過 0xFFFF 的 offset。所以 16-bit limit 剛好對應 16-bit offset register 的能力，完全合理。

24-bit base 的意義是：每個 segment（最大 64KB）可以**放在 16MB 記憶體空間中的任意位置**。這讓 OS 可以把不同的程式（每個最大 64KB）分散在 16MB 的不同位址。

到了 386，offset register 變成 32-bit（最大 4GB），所以 limit 也要能表示最大 4GB，因此設計了 20-bit limit + G bit（granularity）的方案。

---

## 16. 4GB 是每個 Segment 的上限還是總共的？

### 答案：兩者皆是，但意義不同

**每個 segment 最大 4GB**：
- 一個 segment 的 Limit 最大可以設為 0xFFFFF、G=1 → 4GB
- 這代表你可以有一個 4GB 大的 segment

**物理記憶體總共 4GB**：
- 386 的 address bus 是 32-bit → 最大定址 4GB 物理記憶體

**Flat Model（現代 OS 的做法）**：
所有 segment 的 base = 0、limit = 4GB。這代表每個 segment 都涵蓋整個 4GB 的位址空間，彼此完全重疊：

```
位址空間：
0x00000000 ┌──────────────┐
           │              │ ← CS 看到的範圍（base=0, limit=4GB）
           │              │ ← DS 看到的範圍（base=0, limit=4GB）
           │              │ ← SS 看到的範圍（base=0, limit=4GB）
           │  全部重疊     │    全部都是同一個 4GB
0xFFFFFFFF └──────────────┘
```

Segmentation 在 flat model 下等於沒有作用，真正的記憶體保護交給 **paging** 來做。

---

## 17. GDT、LDT 與 8192 Descriptors

### 8192 的計算

Selector 的 Index 欄位是 **13 bits**：

```
15                        3  2  1  0
┌────────────────────────┬───┬─────┐
│        Index           │TI │ RPL │
│     (13 bits)          │   │(2b) │
└────────────────────────┴───┴─────┘
```

2^13 = 8192 → 每個 table（GDT 或 LDT）最多 8192 個 entries。

> **Reference**: Intel 80386 Manual, Section 5.1.1:
> "Each descriptor table can hold up to 8192 (2^13) descriptors."

### 64TB 虛擬位址空間

```
GDT: 8192 descriptors × 每個 segment 最大 4GB = 32TB
LDT: 8192 descriptors × 每個 segment 最大 4GB = 32TB
總共: 32TB + 32TB = 64TB
```

> **Reference**: Intel 80386 Manual, Section 5.1:
> "Each task can have a maximum of 16,381 segments... each segment can be as large as 4 gigabytes, tasks can have a logical address space of as much as 64 terabytes."
> （16381 = 8191 from GDT + 8190 from LDT，扣掉 GDT index 0 是 NULL descriptor）

### LDT 是什麼？

GDT = **Global** Descriptor Table，全系統共享一個。
LDT = **Local** Descriptor Table，每個 task/process 可以有自己的。

```
                    ┌─── GDT（全域共享）
                    │    放 kernel segments、TSS、共用 segments
CPU ───selector───→ │
       TI bit=0 ─→ │
       TI bit=1 ─→ └─── LDT（每個 task 私有）
                         放該 task 自己的 code/data segments
```

Selector 的 TI bit 決定去 GDT 還是 LDT 查找。

### 為什麼現代 OS 不用 LDT？

- **Flat model 不需要**：所有 segment base=0、limit=4GB，每個 process 用同樣的 segment descriptors
- **Paging 取代了隔離功能**：每個 process 有自己的 page table，已經完美隔離了記憶體空間
- **複雜度**：維護每個 process 的 LDT 增加 context switch 的成本，沒有實際好處

> **Reference**: Linux kernel source code (`arch/x86/include/asm/mmu_context.h`):
> Linux 只在極少數情況（如 Wine 模擬 Windows 的 segment 行為）才建立 LDT，預設不使用。

---

## 18. GDT Descriptor（gdt_desc）的格式解析

### start.S 中的 gdt_desc

```asm
gdt_desc:
.word (256*8) - 1    ; GDT 的 limit（2 bytes）
.long gdt_table      ; GDT 的 base address（4 bytes）
```

### 為什麼 limit 要 -1？

`lgdt` 載入的 limit 值代表**GDT 的最後一個有效 byte 的 offset**，不是大小。

Intel 的定義是：

```
有效範圍 = base 到 base + limit（包含 limit 這個 byte）
```

所以如果你有 256 個 entries，每個 8 bytes：
- 總大小 = 256 × 8 = 2048 bytes
- 有效的 offset 範圍 = 0 到 2047
- limit = 2047 = 2048 - 1 = (256 × 8) - 1

如果你寫 limit = 2048（不減 1），CPU 會認為有效範圍是 0 到 2048，多出一個 byte。雖然在實際運作中不會造成問題，但不符合 Intel 的規範。

> **Reference**: Intel 80386 Manual, Section 5.1.1:
> "The limit value is the number of bytes from the base address. Since segment selectors are always a multiple of 8... the limit should be 8N - 1."

### .long gdt_table 是什麼？

`gdt_table` 是在 `os.c` 裡定義的 C array 的符號。Linker 會把它解析為 `gdt_table` 的實際記憶體位址，然後寫入這 4 bytes。

---

## 19. Protected Mode 中 CS 是 Selector，那 Offset 是什麼？

### 問題

```asm
jmp KERNEL_CODE_SEG:_start_32
```

`KERNEL_CODE_SEG`（0x08）是 GDT 的 selector（index），不再是 segment base。那 `:` 後面的 `_start_32` 代表什麼？

### 答案

在 Protected Mode 的 flat model 下：

```
線性位址 = Base（從 GDT descriptor 讀出的）+ Offset（指令中給的）
```

1. CPU 拿 selector 0x08 去 GDT 查找 → 得到 descriptor → Base = 0x00000000
2. Offset = `_start_32` 的值（例如 0x7E00）
3. 線性位址 = 0x00000000 + 0x7E00 = 0x7E00

因為 flat model 的 base 都是 0，所以 offset 就等於最終位址。這就是為什麼你可以直接把 label 當 offset 用——label 的值本身就是位址。

---

# Part 5: Segment-Level Protection

---

## 20. 為什麼需要保護？

### Real Mode 的問題

```
程式 A 想寫 0x1234 → OK
程式 B 想寫 0x1234 → OK，A 的資料被蓋掉了
惡意程式想改 OS 的記憶體 → OK，整台電腦被搞爛
惡意程式想執行特權指令 → OK，電腦直接當掉
```

### Protected Mode 的五種保護

1. **Type Checking** — 用對方式了嗎？（不能把資料當 code 跳，不能寫 read-only）
2. **Limit Checking** — 超出範圍了嗎？
3. **Privilege Checking** — 有權限嗎？
4. **Entry Point Restriction** — 走對入口了嗎？（只能透過 Gate 進入 OS）
5. **Instruction Restriction** — 用了不該用的指令嗎？（如 `lgdt`、`hlt`）

全部由 CPU 硬體自動檢查。

> **Reference**: Intel 80386 Manual, Section 6.3:
> "The 80386 has five aspects of protection... segment type checks, limit checks, privilege level checks, restriction of addressable domain, restriction on procedure entry points, and instruction restrictions."

---

## 21. Type 欄位詳解（S、E、ED/C、R/W、A）

### Access Byte 結構

```
Bit 7    Bit 6-5   Bit 4   Bit 3   Bit 2    Bit 1   Bit 0
┌────────┬────────┬───────┬───────┬────────┬───────┬───────┐
│   P    │  DPL   │   S   │   E   │ ED / C │ R / W │   A   │
│Present │ Priv   │System │Exec   │Expand  │Read   │Access │
│        │ Level  │       │       │or Conf │or Wrt │  ed   │
└────────┴────────┴───────┴───────┴────────┴───────┴───────┘
```

### S bit — System or Code/Data

| S | 意義 |
|---|---|
| 1 | 一般的 code 或 data segment |
| 0 | System descriptor（TSS、Gate、LDT 等） |

### 當 S=1 時，E/ED(C)/R(W)/A 的含義

**E = 0（Data Segment）**：

| Type 值 | ED | W | A | 說明 |
|---|---|---|---|---|
| 0000 | 0 | 0 | 0 | Read-Only |
| 0010 | 0 | 1 | 0 | Read/Write |
| 0100 | 1 | 0 | 0 | Read-Only, Expand-Down |
| 0110 | 1 | 1 | 0 | Read/Write, Expand-Down |

**E = 1（Code Segment）**：

| Type 值 | C | R | A | 說明 |
|---|---|---|---|---|
| 1000 | 0 | 0 | 0 | Execute-Only |
| 1010 | 0 | 1 | 0 | Execute/Read |
| 1100 | 1 | 0 | 0 | Execute-Only, Conforming |
| 1110 | 1 | 1 | 0 | Execute/Read, Conforming |

### A bit（Accessed）

CPU 每次存取一個 segment 時會自動把 A bit 設為 1。OS 可以定期清除它，然後觀察哪些 segment 有被存取過——用於記憶體管理的 LRU 演算法。現代 OS 用 paging 的 Accessed bit 做同樣的事。

---

## 22. System Descriptor（S=0）的用途

### 所有 System Descriptor 類型

| Type | 說明 | 現代 OS 還用嗎？ |
|---|---|---|
| 0x1 | Available 286 TSS | ❌ |
| 0x2 | LDT | ❌（幾乎不用） |
| 0x3 | Busy 286 TSS | ❌ |
| 0x4 | Call Gate | ❌（用 syscall/sysenter 取代） |
| 0x5 | Task Gate | ❌ |
| 0x6 | 286 Interrupt Gate | ❌ |
| 0x7 | 286 Trap Gate | ❌ |
| 0x9 | Available 386 TSS | ✅ |
| 0xB | Busy 386 TSS | ✅ |
| 0xC | 386 Call Gate | ❌ |
| 0xE | 386 Interrupt Gate | ✅ |
| 0xF | 386 Trap Gate | ✅ |

### 為什麼 GDT 中 S bit 還在用？

因為 GDT 裡同時存在 S=1 的 descriptor（code/data segment）和 S=0 的 descriptor（TSS descriptor）。CPU 需要 S bit 來區分：

```
GDT:
Index 0: NULL
Index 1: Kernel Code (S=1, E=1) ← 一般 segment
Index 2: Kernel Data (S=1, E=0) ← 一般 segment
Index 3: User Code   (S=1, E=1) ← 一般 segment
Index 4: User Data   (S=1, E=0) ← 一般 segment
Index 5: TSS         (S=0, Type=0x9) ← system descriptor，格式完全不同！
```

### TSS Descriptor 的功能

TSS Descriptor 不直接存放 stack 資訊。它的作用是**指向一塊記憶體中的 TSS 結構**。

```
GDT 裡的 TSS Descriptor → 指向記憶體中的 TSS 結構 → 裡面有 RSP0、IST 等

TSS 結構（64-bit mode 精簡版）：
┌──────────────────┐
│ Reserved         │
│ RSP0             │ ← Ring 3 → Ring 0 時的 stack pointer（最重要！）
│ RSP1             │
│ RSP2             │
│ IST1 ~ IST7      │ ← Interrupt Stack Table（NMI、Double Fault 用）
│ I/O Map Base     │
└──────────────────┘
```

每個 CPU core 有一個 TSS。當發生 system call 或中斷、需要從 Ring 3 切到 Ring 0 時，CPU 從 TSS 讀取 RSP0 作為新的 stack pointer。

> **Reference**: Linux kernel source (`arch/x86/kernel/cpu/common.c`, `cpu_init()`):
> 每個 CPU 在初始化時都會設定自己的 TSS，指定 RSP0 為 kernel stack。

---

## 23. Privilege Level：CPL、DPL、RPL

### CPL（Current Privilege Level）

```
CPL = CS register 的 RPL 欄位（最低 2 bits）
    = 你「現在」的權限等級
```

- CS = 0x08（RPL=0）→ CPL = 0 → Kernel mode
- CS = 0x1B（RPL=3）→ CPL = 3 → User mode

### DPL（Descriptor Privilege Level）

```
DPL = 寫在 descriptor 裡的權限等級
    = 「你至少要有這個等級才能存取我」
```

### RPL（Requestor's Privilege Level）

```
RPL = selector 最低 2 bits
    = 「原始請求者的權限等級」
```

防冒充機制——詳見後面的 [五種保護機制](#24-五種保護機制)。

### 現代 OS 中的使用情況

| 機制 | 還在用嗎？ | 說明 |
|---|---|---|
| **CPL** | ✅ 核心使用 | Paging 依賴 CPL：page table entry 的 U/S bit 就是看 CPL 決定能不能存取 |
| **DPL** | ✅ 簡化使用 | 只用 0 和 3 兩個值。Code/Data segment 的 DPL 配合 flat model |
| **RPL** | ❌ 幾乎不用 | Paging 已經提供了足夠的保護，不需要 RPL 的防冒充機制 |

> **Reference**: Intel SDM Volume 3A, Section 4.6:
> "Every access to a linear address is either a supervisor-mode access or a user-mode access. All accesses performed while CPL < 3 are supervisor-mode accesses."

---

## 24. 五種保護機制

### 24.1 Type Checking

```
把 data segment 載入 CS        → ❌ CS 只接受 code segment
把不可讀 code segment 載入 DS  → ❌ DS 是讀資料的，segment 必須可讀
對 code segment 做寫入操作      → ❌ code segment 永遠不可寫
對唯讀 data segment 做寫入      → ❌ W bit = 0
```

### 24.2 Limit Checking

**Expand-Up（一般 segment）**：有效 offset = 0 到 Limit

**Expand-Down（stack segment）**：有效 offset = (Limit+1) 到 上限

G bit 決定 Limit 的單位（byte 或 4KB page）：

| G | Limit 值 0xFFFFF 的實際大小 |
|---|---|
| 0 | 1MB |
| 1 | 4GB（0xFFFFF × 4096 + 4095 = 0xFFFFFFFF） |

### 24.3 Privilege Checking — Data Access

```
規則：DPL >= MAX(CPL, RPL)

CPL=0, RPL=0, DPL=0 → MAX(0,0)=0, 0>=0 ✅ Kernel 存取 Kernel 資料
CPL=3, RPL=3, DPL=0 → MAX(3,3)=3, 0>=3 ❌ User 不能存取 Kernel 資料
CPL=0, RPL=3, DPL=0 → MAX(0,3)=3, 0>=3 ❌ RPL 防冒充生效
```

### 24.4 Privilege Checking — Code Transfer

**直接 JMP/CALL**：
- Non-conforming：目標 DPL 必須 = CPL（只能跳到同權限）
- Conforming：目標 DPL ≤ CPL（可以跳到更高權限，但 CPL 不變）

**透過 Call Gate**：
- MAX(CPL, RPL) ≤ Gate DPL（你有資格用 Gate 嗎？）
- Target DPL ≤ CPL（目標權限比你高，合理）
- CALL 可以提升 CPL；JMP 不能提升 CPL

### 24.5 Instruction Restriction

只有 CPL = 0 才能執行的指令：

| 指令 | 用途 |
|---|---|
| `lgdt` / `lidt` | 載入 GDT/IDT 暫存器 |
| `lldt` / `ltr` | 載入 LDT/Task Register |
| `lmsw` / `mov crN` | 修改控制暫存器 |
| `hlt` | 停止 CPU |
| `mov drN` | 讀寫 debug 暫存器 |
| `clts` | 清除 Task-Switched flag |

I/O 指令（`in`、`out`、`cli`、`sti`）受 IOPL 控制：CPL ≤ IOPL 才能執行。

---

## 25. Gate Descriptor（閘門描述符）

### 為什麼需要 Gate？

直接 JMP/CALL 不能提升權限。Ring 3 需要呼叫 OS 功能時，必須透過 Gate：

```
沒有 Gate：Ring 3 → jmp Ring 0 任意位址 → ❌ CPU 拒絕
有 Gate：  Ring 3 → call gate → 只能跳到 Gate 指定的入口 → ✅ 安全
```

### Gate 的結構

```
┌────────────────────────────────────────────┐
│ Offset 31..16    （目標函數的 offset 高 16 位）│
│ P | DPL | Type   （Present、權限、類型）      │
│ Dword Count      （要複製幾個參數）            │
│ Selector         （目標 code segment 的 selector）│
│ Offset 15..0     （目標函數的 offset 低 16 位）│
└────────────────────────────────────────────┘
```

重要：CALL 指令裡寫的 offset 會被 CPU 忽略，CPU 只用 Gate 裡寫的 offset。所以你不能跳到 kernel 函數的中間。

### 現代 OS 的替代方案

現代 OS 不用 Call Gate 來做 system call，而是用更快的指令：
- **Intel**: `sysenter` / `sysexit`（Pentium II+）
- **AMD/通用**: `syscall` / `sysret`（x86-64）

這些指令直接在 MSR 裡指定目標 CS 和 entry point，不需要查 GDT 中的 Gate descriptor，速度更快。

---

## 26. Conforming Code Segment 到底是什麼？

### 定義

Conforming segment（C=1）允許**低權限的程式直接 JMP/CALL 進來**，但 CPL 不會改變。

```
非 Conforming（C=0）：
  DPL 必須 = CPL → 只有同權限能跳進來
  不符合就 General Protection Fault

Conforming（C=1）：
  DPL ≤ CPL → 權限比你高或相等都可以跳進來
  但 CPL 維持不變（你的權限不會提升）
```

### 「ignore DPL」vs「abide by DPL」的困惑

有些資料寫：
- C=0 → "ignore descriptor privilege level"
- C=1 → "abide by privilege level"

這不是跟上面說的相反嗎？

**其實是不同角度**：

- **C=0（non-conforming）**：DPL 必須嚴格等於 CPL。如果不等，不管差多少都拒絕。從「DPL 作為門檻」的角度看，DPL 沒有作為一個「門檻」在運作——它只是一個等號比較，所以說「ignore（不把 DPL 當作有意義的門檻）」。
- **C=1（conforming）**：DPL 作為一個**真正的門檻**（DPL ≤ CPL），意思是「你的權限至少要等於 DPL 才能進來」。DPL 發揮了閾值的作用，所以說「abide by（遵守 DPL 作為門檻的規定）」。

### 用途

```
場景：共用的數學函式庫（sin, cos, sqrt）放在 Ring 0 的 conforming segment

Ring 3 的程式呼叫 sin() → ✅ 允許
  CPL 還是 3（不會變成 0）
  sin() 裡面如果嘗試做 Ring 0 的事 → ❌ 被拒絕（CPL 還是 3）

vs Call Gate：
  Call Gate 呼叫 → CPL 真的變成 0（權限提升了）
  Conforming 呼叫 → CPL 維持 3（只是借用程式碼）
```

---

## 27. Segment Register 的類型限制

### 每個 Segment Register 接受的 Descriptor 類型

| Segment Register | 接受的類型 | 原因 |
|---|---|---|
| **CS** | Code segment only（S=1, E=1） | CS 是用來執行指令的，只有 code 能執行 |
| **SS** | Writable data segment only（S=1, E=0, W=1） | Stack 需要 push/pop（讀+寫），不能是 code，不能是 read-only |
| **DS, ES, FS, GS** | Data segment 或 readable code segment | 讀取資料用，code segment 需要 R=1 |

### SS 不能是 System Descriptor

SS 絕對不能是 S=0（system descriptor）。System descriptor 描述的是 TSS、Gate 等特殊結構，不是一塊可以讀寫的記憶體。Stack 需要的是一塊可以 push/pop 的記憶體，只有 writable data segment 才行。

### SS 不能是 Code Segment

Code segment 永遠不可寫（CPU 硬性規定）。Stack 需要寫入（push），所以 SS 不能指向 code segment。

---

## 28. 指令執行時的完整流程

### 階段一：載入 Selector 到 Segment Register

當你執行 `mov ds, ax`（或 far jmp 修改 CS）時：

```
1. CPU 讀取 selector 的 Index 和 TI bit
2. 根據 TI bit 去 GDT（TI=0）或 LDT（TI=1）
3. 檢查 Index 是否在 table limit 範圍內 → 否則 GP Fault
4. 讀取 8-byte descriptor
5. 檢查 P bit（Present）→ P=0 則 Not Present Fault
6. Type Checking → 類型對不對（例如 CS 只接受 code）
7. Privilege Checking → DPL >= MAX(CPL, RPL)
8. 全部通過 → 把 Base、Limit、Access Rights 存入 segment register 的隱藏部分（cache）
```

所有的「重量級檢查」在這一步完成。之後的記憶體存取就用 cache 裡的資訊。

### 階段二：使用 Segment Register 存取記憶體

當你執行 `mov eax, [ds:0x1234]` 時：

```
1. CPU 從 DS 的 cache 讀取 Base、Limit、Type
2. Limit Checking → offset 0x1234 是否在 limit 範圍內
3. Type Checking → 這次操作跟 segment type 相容嗎（讀/寫/執行）
4. 計算線性位址 = Base + Offset
5. 如果啟用 Paging → 線性位址經過 page table 轉換為物理位址
   → Page table 的 U/S bit 檢查（依據 CPL）
   → Page table 的 R/W bit 檢查
6. 最終存取物理記憶體
```

```
                    ┌─────────────┐
                    │ 載入 Selector │
                    └──────┬──────┘
                           │
            ┌──────────────┼──────────────┐
            │              │              │
        GDT 查表     Type Check    Privilege Check
            │              │              │
            └──────────────┼──────────────┘
                           │
                    ┌──────┴──────┐
                    │ Cache 到     │
                    │ Hidden Part  │
                    └──────┬──────┘
                           │
                    ┌──────┴──────┐
                    │ 記憶體存取   │
                    └──────┬──────┘
                           │
            ┌──────────────┼──────────────┐
            │              │              │
       Limit Check    Type Check    Base + Offset
            │              │              │
            └──────────────┼──────────────┘
                           │
                    ┌──────┴──────┐
                    │   Paging    │（如果啟用）
                    │  U/S, R/W  │
                    └──────┬──────┘
                           │
                    ┌──────┴──────┐
                    │  物理記憶體  │
                    └─────────────┘
```

---

# Part 6: Stack 與 TSS

---

## 29. SS、ESP、EBP 的關係

### SS（Stack Segment）

SS 指向 stack 所在的 segment。在 flat model 下，SS 的 base = 0、limit = 4GB，所以 SS 幾乎透明——ESP 的值就是 stack 的線性位址。

### ESP（Extended Stack Pointer）

ESP 指向 **stack 的頂端**（最後一個被 push 的值的位址）。

```
push eax → ESP -= 4，然後把 eax 寫到 [SS:ESP]
pop eax  → 從 [SS:ESP] 讀值到 eax，然後 ESP += 4
```

Stack 是從高位址往低位址長的。push 讓 ESP 變小，pop 讓 ESP 變大。

### EBP（Extended Base Pointer）

EBP 是 **stack frame 的基準點**。在函數呼叫時，EBP 指向當前 function 的 stack frame 底部，用來存取函數參數和區域變數：

```
呼叫 foo(1, 2) 前的 stack：

高位址
┌──────────────┐
│      2       │  [EBP+12]  ← 第二個參數
│      1       │  [EBP+8]   ← 第一個參數
│  Return Addr │  [EBP+4]   ← CALL 自動 push 的
│  舊的 EBP    │  [EBP]     ← 函數開頭 push ebp 保存的
│  local var 1 │  [EBP-4]   ← 區域變數
│  local var 2 │  [EBP-8]   ← 區域變數
│              │  ← ESP（stack 頂端）
低位址
```

典型的函數開頭（function prologue）：

```asm
push ebp          ; 保存呼叫者的 EBP
mov ebp, esp      ; EBP = 當前 stack 頂端（作為基準點）
sub esp, 8        ; 預留 8 bytes 給區域變數
```

函數結尾（function epilogue）：

```asm
mov esp, ebp      ; 回收區域變數的空間
pop ebp           ; 恢復呼叫者的 EBP
ret               ; 返回（pop EIP）
```

### 三者的關係

- **SS** 決定 stack 在哪個 segment（flat model 下不重要）
- **ESP** 隨著 push/pop 動態移動，永遠指向 stack 頂端
- **EBP** 在函數內部固定不動，作為存取參數和區域變數的錨點

---

## 30. Stack Switching 與 TSS

### 為什麼要切換 Stack？

Ring 3 的 stack 是使用者控制的。如果 kernel 繼續用 Ring 3 的 stack：

```
攻擊 1：惡意程式把 stack pointer 指到 kernel 記憶體 → push 會覆蓋 kernel 資料
攻擊 2：惡意程式把 stack 弄到快滿 → kernel push 造成 stack overflow
```

所以每個權限等級需要自己的、被信任的 stack。

### TSS 存放的 Stack 資訊

```
TSS：
┌──────────────┐
│ SS0 : ESP0   │  ← Ring 3 → Ring 0 時用的 stack
│ SS1 : ESP1   │  ← Ring 3 → Ring 1 時用的 stack
│ SS2 : ESP2   │  ← Ring 3 → Ring 2 時用的 stack
└──────────────┘
沒有 SS3:ESP3（Ring 3 是最低權限，不會有更低的權限呼叫它）
```

### Interlevel CALL 的完整步驟

```
Ring 3 透過 Call Gate 呼叫 Ring 0：

Step 1: 從 TSS 讀取 SS0:ESP0
Step 2: 切換到新 stack（SS=SS0, ESP=ESP0）
Step 3: Push 舊的 SS:ESP 到新 stack
Step 4: 複製參數（Gate 的 Dword Count 指定數量）
Step 5: Push 舊的 CS:EIP（返回位址）
Step 6: 載入新的 CS:EIP（Gate 指定的入口）
Step 7: CPL 變成 0

新 stack 的內容：
┌──────────────┐
│  舊 SS       │
│  舊 ESP      │
│  參數 1      │  ← 從舊 stack 複製
│  參數 2      │
│  舊 CS       │  ← 返回位址
│  舊 EIP      │
└──────────────┘ ← ESP
```

返回時（`ret`），CPU 自動反向操作：pop CS:EIP、跳過參數、pop SS:ESP，恢復到 Ring 3 的 stack。

---

# Part 7: 現代 OS 還用這些嗎？

---

## 31. 現代 OS 中哪些機制還在用？

### Segmentation 相關

| 機制 | 還用嗎？ | 說明 |
|---|---|---|
| GDT | ✅ | 必須存在，但只有少數 entries（kernel code/data, user code/data, TSS） |
| LDT | ❌ | 幾乎不用（只有 Wine 等相容需要） |
| Segment base/limit | ❌ | Flat model：base=0, limit=4GB，等於沒作用 |
| FS/GS base | ✅ | FS 用於 thread-local storage，GS 用於 per-CPU data |

### Descriptor 欄位

| 欄位 | 還用嗎？ | 說明 |
|---|---|---|
| **S bit** | ✅ | GDT 裡混合 S=1（code/data）和 S=0（TSS），CPU 必須區分 |
| **E bit** | ✅ | 區分 code 和 data segment（影響 CS 載入行為） |
| **P bit** | ✅ | P=0 觸發 segment not present fault |
| **DPL** | ✅ | 只用 0 和 3，配合 CPL 做基本的 kernel/user 區分 |
| **L bit** | ✅ | Long Mode 新增，L=1 表示 64-bit code segment |
| **D/B bit** | ✅ | 區分 32-bit 和 16-bit 模式 |
| **G bit** | ✅ | 設成 4KB granularity 讓 limit = 4GB |
| **C bit** | ❌ | Conforming segment 幾乎不用 |
| **ED bit** | ❌ | Expand-down 幾乎不用 |
| **R/W bit** | ❌ | Flat model 下 DS/ES/SS 共用同一個 writable data segment |
| **A bit** | ❌ | Paging 的 Accessed bit 取代了 segment 的 A bit |
| **RPL** | ❌ | Paging 的 U/S bit 提供了足夠的保護 |

### System Descriptor

| 類型 | 還用嗎？ | 說明 |
|---|---|---|
| **TSS** | ✅ | 每個 CPU 一個，存放 RSP0（kernel stack pointer）和 IST |
| **Interrupt Gate** | ✅ | IDT 中的中斷處理 entry |
| **Trap Gate** | ✅ | IDT 中的例外處理 entry |
| **Call Gate** | ❌ | 被 `syscall`/`sysenter` 取代 |
| **Task Gate** | ❌ | 硬體 task switch 太慢，OS 用軟體做 context switch |
| **LDT descriptor** | ❌ | 不用 LDT 就不需要 |

### 權限機制

| 機制 | 還用嗎？ | 說明 |
|---|---|---|
| **CPL** | ✅ | 核心機制，paging 的 U/S 檢查依賴 CPL |
| **Ring 0 / Ring 3** | ✅ | Kernel mode / User mode 的區分基礎 |
| **Ring 1 / Ring 2** | ❌ | 從未被主流 OS 使用 |
| **Privilege Instructions** | ✅ | `lgdt`、`mov cr0`、`hlt` 等仍然只有 Ring 0 能執行 |
| **IOPL** | ✅ | 控制 I/O 指令和 cli/sti 的權限 |

### 總結

現代 OS 的保護機制主力是 **Paging**（page table 的 U/S、R/W、NX bit），segmentation 退化為 flat model，只保留最基本的 kernel/user 區分（透過 CPL）。但 GDT、TSS、IDT 等結構因為 CPU 硬體要求，仍然必須存在。

---

# Part 8: 參考資料

---

## 32. References

### Intel 官方手冊

1. **Intel 80386 Programmer's Reference Manual (1986)**
   - Section 2.1: Modes of Operation
   - Section 2.5: Memory Management
   - Section 5.1: Segment Translation / Descriptor Tables
   - Section 6.3: Segment-Level Protection
   - Section 10.3: Switching to Protected Mode
   - Online: https://www.scs.stanford.edu/nyu/04fa/lab/i386/

2. **Intel 64 and IA-32 Architectures Software Developer's Manual**
   - Volume 2: Instruction Set Reference (MOV, LGDT, LMSW)
   - Volume 3A, Chapter 3: Protected-Mode Memory Management
   - Volume 3A, Chapter 4: Paging
   - Volume 3A, Section 4.6: Access Rights
   - Volume 3A, Chapter 7: Task Management (TSS)

3. **Intel 80286 Programmer's Reference Manual (1985)**
   - Real address mode and protected mode descriptions
   - 6-byte descriptor format (with reserved bytes 6-7)

### AMD 手冊

4. **AMD64 Architecture Programmer's Manual, Volume 2**
   - Section 1.2: Long Mode overview
   - "In 64-bit mode, segmentation is disabled. The segment base is treated as zero."

### 其他資源

5. **Ralph Brown's Interrupt List**
   - INT 10h (video services)
   - INT 13h (disk services): "ES:BX -> buffer for data"

6. **Linux Kernel Source Code**
   - `arch/x86/kernel/cpu/common.c`: TSS initialization per CPU
   - `arch/x86/include/asm/mmu_context.h`: LDT usage (minimal)
   - `arch/x86/include/asm/segment.h`: GDT layout definitions

7. **OSDev Wiki** (https://wiki.osdev.org)
   - GDT Tutorial
   - Protected Mode
   - Setting Up Long Mode
