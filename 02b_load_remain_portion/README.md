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

**Part 8: GAS Intel Syntax 的陷阱**
33. [OFFSET 關鍵字與 Label 的語意差異](#33-offset-關鍵字與-label-的語意差異)

**Part 9: 記憶體管理演進：從 Segmentation 到 Paging**
34. [8086 到 386 的記憶體管理演進](#34-8086-到-386-的記憶體管理演進)
35. [External Fragmentation 與 Segmentation 的局限](#35-external-fragmentation-與-segmentation-的局限)
36. [OS/2 的 Segment 管理 API](#36-os2-的-segment-管理-api)
37. [Memory Model 與 Pointer 類型](#37-memory-model-與-pointer-類型)
38. [Paging 如何解決 Segmentation 的問題](#38-paging-如何解決-segmentation-的問題)

**Part 10: 參考資料**
39. [References](#39-references)

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
    jc read_self_all     ; CF=1 表示失敗，重試。這個指令是jump if carry CF=1
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
  │ ← 啟用 PAE → 設定 page tables → 設定 CR0.PG、EFER.LME → Far Jump //CR0的paging必須要啟用pe，不然沒有效果
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

# Part 8: GAS Intel Syntax 的陷阱

---

## 33. OFFSET 關鍵字與 Label 的語意差異

### 問題背景

在除錯本專案時，發現進入 Protected Mode 後 GDT 未正確載入（GDTR base = 0x000000）。經過逐步排查，確認 INT 0x13 磁碟讀取的目標位址為 0，導致第二個 sector 的資料被載入到 0x0000 而不是 0x7E00。

### 根本原因：GAS Intel Syntax 的 Label 語意

GAS 的 `.intel_syntax noprefix` 模式模仿 MASM 的行為：**裸 label 被解讀為記憶體參考（dereference），而非立即值**。

```asm
mov bx, _start_32       ; GAS 解讀為：mov bx, [_start_32]（讀取記憶體）
                         ; 機器碼：8B 1E xx xx（memory load）

mov bx, OFFSET _start_32 ; GAS 解讀為：mov bx, _start_32 的地址值
                         ; 機器碼：BB xx xx（immediate load）
```

這與 NASM 的行為相反。在 NASM 中，裸 label 就是立即值，要 dereference 必須加 `[]`（[NASM vs MASM syntax reference](https://www.nasm.us/xdoc/2.16.03/html/nasmdoc2.html)）。

### 受影響的指令

本專案中有兩行受影響：

```asm
mov esp, _start          ; ❌ 讀取 [0x7C00] 的值，而非載入 0x7C00
mov bx, _start_32        ; ❌ 讀取 [0x7E00] 的值，而非載入 0x7E00
```

修正：

```asm
mov esp, OFFSET _start       ; ✅ esp = 0x7C00
mov bx, OFFSET _start_32    ; ✅ bx = 0x7E00
```

### 不需要 OFFSET 的情況

並非所有指令都需要加 OFFSET：

| 指令 | 需要 OFFSET？ | 原因 |
|---|---|---|
| `mov reg, label` | **需要** | 否則變成 `mov reg, [label]` |
| `jmp label` | 不需要 | jmp 的操作數永遠是地址 |
| `lgdt [label]` | 不需要 | lgdt 本來就是從記憶體讀取 |
| `call label` | 不需要 | call 的操作數永遠是地址 |

### 除錯過程

1. 使用 `objdump -d` 查看反組譯，發現 `mov bx, _start_32` 生成了 `8b 1e`（memory load opcode），而非 `bb`（immediate load opcode）
2. 在 GDB 中用 `x/6xb 0x7E10` 檢查 gdt_desc 位址，發現全為零
3. 確認 INT 0x13 因 BX=0 而將資料載入到 0x0000，而非 0x7E00
4. 原始教材（02a）有相同的 bug，但因為 02a 載入後直接 `jmp .` 不使用載入的資料，所以 bug 未被觸發

---

# Part 9: 記憶體管理演進：從 Segmentation 到 Paging

---

## 34. 8086 到 386 的記憶體管理演進

8086 的 CPU 就具備 segment 機制（CS、DS、ES、SS），但此時為 Real Mode，segment register 的值直接參與 physical address 的計算（segment × 16 + offset），沒有 descriptor 也沒有 paging，因此程式的記憶體操作直接對應 physical memory。offset 是 16-bit，最大值 0xFFFF = 64KB，所以即使 8086 有 1MB 的 address space，每次只能透過一個 segment register 看到其中 **64KB 的窗口**，要看另一段就必須更換 segment register 的值。在 8086 搭配 DOS 的時期，系統僅做單任務，所以多任務引起的 memory fragmentation 問題並不明顯（[Chen, 2020](https://devblogs.microsoft.com/oldnewthing/20200728-00/?p=104012)）。

80286 的 CPU 引入了 Protected Mode 與 descriptor（GDT/LDT）機制，使得 segment 具備了 base、limit、access rights 等屬性，並且可執行多任務。理論上任何程式都認為其可以操作 GDT + LDT 的記憶體（(8192 + 8192) × 64KB = 1GB），descriptor 中的 P bit（Present）允許 segment 暫時不在實體記憶體中，實現了以 segment 為單位的 virtual memory。但實際上此 segment 機制搭配多任務後，external fragmentation 的問題會嚴重放大（見 [§35](#35-external-fragmentation-與-segmentation-的局限)）。

80386 引入了 paging 機制，將記憶體以固定大小（4KB page）為單位管理，從根本上解決了 segmentation 的 external fragmentation 問題。386 團隊的設計調查發現「所有客戶都討厭 8086 的 segmented memory scheme，並且認為 286 是一個錯失的機會」（[XtoF, 2023](https://www.xtof.info/intel80386.html)），因此 386 將支援 flat memory 視為最高優先級。

### 演進總覽

| CPU | 年份 | 記憶體管理 | Virtual Memory |
|---|---|---|---|
| 8086 | 1978 | segment × 16 + offset | 無 |
| 80286 | 1982 | descriptor（base + limit），segment 為單位 | 有（P bit，以 segment 為單位 swap） |
| 80386 | 1985 | descriptor + paging（4KB page） | 有（以 page 為單位 swap） |

---

## 35. External Fragmentation 與 Segmentation 的局限

### External Fragmentation

在 segmentation 架構下，每個 segment 必須佔用**連續的 physical memory**（一個 base + 一段連續的 offset 範圍）。當多個不同大小的 segment 被分配和釋放後，physical memory 中會出現大小不一的空洞：

```
記憶體（external fragmentation）：
  ┌──────┬──────┬──────┬──────┬──────┬──────┐
  │ Seg A│ 空洞  │ Seg C│ 空洞 │ Seg E│ 空洞  │
  │ 30KB │ 10KB │ 20KB │ 5KB  │ 40KB │ 15KB │
  └──────┴──────┴──────┴──────┴──────┴──────┘
```

當程式需要連續的 50KB 時，空洞合計雖有 30KB（10 + 5 + 15），但沒有任一個連續空洞 ≥ 50KB，因此無法分配。即使做記憶體重整（compaction）將所有 segment 往一端靠攏，性能開銷也非常大（[Intel, 1987, §6.3](https://pdos.csail.mit.edu/6.828/2017/readings/i386/s06_03.htm)）。

### Internal Fragmentation

相對地，paging 使用固定大小的 page（4KB），每次分配都是 4KB 的倍數。如果程式只需要 5KB，就要分配 2 個 page（8KB），浪費 3KB。這種「分配單元內部的浪費」稱為 internal fragmentation。但因為 page 很小（4KB），浪費的量遠小於 segmentation 的問題。

### Segmentation 的根本局限

Segment 的設計天然要求每個 segment 佔用連續的 physical memory。這意味著：

1. **分配困難**：需要找到夠大的連續空間
2. **擴展困難**：segment 後面可能已被其他 segment 佔用，無法就地擴展
3. **大小不一**：每個 segment 的 limit 不同，加劇了碎片化
4. **超過 64KB 的資料需要切片**：程式必須自己處理跨 segment 的存取（見 [§36](#36-os2-的-segment-管理-api)）

---

## 36. OS/2 的 Segment 管理 API

在 286 的 Protected Mode 下，OS 實際操作 LDT 中的 descriptor 來管理記憶體。以 OS/2 為例，提供了完整的 segment 管理 API。

### DosAllocSeg — 分配單一 Segment

[DosAllocSeg](https://www.edm2.com/index.php/DosAllocSeg) 接受 1~65536 bytes 的 Size 參數，OS 會在 LDT 中建立對應的 descriptor 並設定精確的 limit。

```c
// API 簽名
DosAllocSeg(Size, Selector, AllocFlags)
// Size: 1~65535（0 代表 65536）
// Selector: 回傳分配到的 selector
// AllocFlags: 共享/可丟棄等屬性
```

Intel 規格明確指出 descriptor 的 limit 欄位「defines the size of the segment」，且「the value of the limit is one less than the size (expressed in bytes) of the segment」（[Intel, 1987, §5.1](https://pdos.csail.mit.edu/6.828/2005/readings/i386/s05_01.htm)）。如果存取超過 limit 的 offset，CPU 觸發 General Protection Exception（[Intel, 1987, §6.3](https://pdos.csail.mit.edu/6.828/2017/readings/i386/s06_03.htm)）。osFree 專案的原始碼中可以看到實際的 descriptor limit 設定為 `(size - 1)`，而非固定 64KB（[osfree-project/os3, kal.c](https://github.com/osfree-project/os3)）：

```c
desc.limit_lo = (size - 1) & 0xffff;
desc.limit_hi = (size - 1) >> 16;
```

### DosReallocSeg — 調整 Segment 大小

[DosReallocSeg](http://www.osfree.org/doku/doku.php?id=en:docs:fapi:dosreallocseg) 能事後調整 segment 的大小，OS 會更新 descriptor 的 limit 欄位。因此 segment 的 limit 是**浮動的**——但最大值受限於 286 descriptor 的 16-bit limit 欄位，故上限為 64KB。

### DosAllocHuge — 分配超過 64KB 的記憶體

當程式需要超過 64KB 的連續資料時，必須使用多個 segment 來拼接。OS/2 提供了 [DosAllocHuge](https://www.edm2.com/index.php/DosAllocHuge) 來分配多個等間距的 segment。

```c
DosAllocHuge(NumSeg, Size, Selector, MaxNumSeg, AllocFlags)
// NumSeg: 完整 64KB segment 的數量
// Size: 最後一個 segment 的 bytes 數（"last (non-65536-byte) segment"）
// Selector: 回傳第一個 segment 的 selector
```

以分配 200KB 為例（[Letwin, 1988, §9.2.2](https://gunkies.org/wiki/Inside_OS/2)）：

```
DosAllocHuge(3, 8192, ...)

Selector n+0i  →  64KB  ← 第 0 個 segment
Selector n+1i  →  64KB  ← 第 1 個 segment
Selector n+2i  →  64KB  ← 第 2 個 segment
Selector n+3i  →   8KB  ← 最後一個 segment（limit = 8191）

i = 等間距值，每次開機可能不同，透過 DosGetHugeShift 取得
```

Gordon Letwin 在 *Inside OS/2* 中指出，這個 huge segment 的算法與 8086 的 segment 算術本質相同：在 Real Mode 下 `i` 固定為 4096（因為 segment × 16，4096 × 16 = 64KB），在 Protected Mode 下 `i` 由 OS 提供。程式碼的計算方式完全一樣，只是底層機制不同——Real Mode 的 selector 值直接對應 physical address，Protected Mode 的 selector 只是 LDT 的 index，由 OS 刻意安排等間距（[Letwin, 1988, §9.2.2](https://gunkies.org/wiki/Inside_OS/2)）。

### 跨 Segment 存取的範例

以下為 Microsoft KB Q73187 提供的 OS/2 MASM 程式碼範例，在 huge segment 中每隔 10000 個 byte 寫入 1（[Microsoft, 1991](https://jeffpar.github.io/kbarchive/kb/073/Q73187/)）：

```asm
.model huge, pascal, OS_OS2

invoke DosAllocHuge, 1, 34464, addr selector, 0, SEG_NONSHARED
invoke DosGetHugeShift, addr ShiftCount
mov cx, ShiftCount
shl i, cl                    ; i = selector 間距值

mov ax, selector
mov es, ax                   ; es = 第一個 segment 的 selector
mov bx, 0
mov dl, 1

.while cx <= 10
    mov es:[bx], dl          ; 寫入 1 byte
    add bx, 10000            ; offset += 10000
    inc cx                   ; inc 不影響 CF
    jnc testcx               ; CF=0（未溢位）→ 跳過 segment 切換
    mov ax, es
    add ax, i                ; 切到下一個 segment
    mov es, ax
testcx:
.endw
```

當 `add bx, 10000` 使 offset 溢位超過 0xFFFF 時，CF 被設為 1，程式將 `es` 加上間距值 `i` 以切換到下一個 segment。`inc cx` 不影響 CF（x86 設計），因此 `jnc` 判斷的是 `add` 的進位。

---

## 37. Memory Model 與 Pointer 類型

### Pointer 類型

16-bit x86 有三種指標類型，其區別在於如何處理 segment:offset 定址（[Chen, 2020](https://devblogs.microsoft.com/oldnewthing/20200728-00/?p=104012)）：

| 類型 | 大小 | 內容 | 跨 segment？ |
|---|---|---|---|
| **near** | 16-bit | offset only | 不能，限於當前 segment 的 64KB |
| **far** | 32-bit | segment + offset | 能跨 segment，但 offset 溢位時不自動處理 |
| **huge** | 32-bit | segment + offset（正規化） | 能跨 segment，自動處理 offset 溢位 |

Huge pointer 的正規化：將盡可能多的 bits 放進 segment，使 offset 永遠 < 16（[Chen, 2020](https://devblogs.microsoft.com/oldnewthing/20200728-00/?p=104012)）。例如 physical address `0x179B8`：

```
far pointer:  1234:5678  → 0x12340 + 0x5678 = 0x179B8（非唯一表示）
huge pointer: 179B:0008  → 0x179B0 + 0x0008 = 0x179B8（唯一表示）
```

正規化確保每個 physical address 只有一種表示法，使指標比較正確。但 Raymond Chen 指出：「pointer arithmetic with huge pointers was computationally expensive, so you didn't use them much」（[Chen, 2020](https://devblogs.microsoft.com/oldnewthing/20200728-00/?p=104012)）。

### Memory Model

Memory model 決定程式**預設**使用哪種 pointer。這是**編譯器層面的決定**，不是 OS 決定的。OS 不知道也不在乎程式用了哪個 model（[Chen, 2020](https://devblogs.microsoft.com/oldnewthing/20200728-00/?p=104012)）。

核心是一個 2×2 矩陣：

| | Data = near | Data = far |
|---|---|---|
| **Code = near** | **Small** | **Compact** |
| **Code = far** | **Medium** | **Large** |

加上兩個特殊的：
- **Tiny**：Code + Data 共用同一個 64KB segment（`.COM` 檔案格式）
- **Huge**：跟 Large 相同（far code + far data），但額外支援單一資料結構超過 64KB

不論選擇哪個 model，程式都可以手動 override 單一變數的 pointer 類型。例如在 Small model 裡宣告 `far` 指標（[Bumbershoot Software, 2022](https://bumbershootsoft.wordpress.com/2022/08/05/memory-models-and-far-pointers/)）。

### Far Pointer 比較的陷阱

當時的 16-bit C 編譯器（Turbo C、Microsoft C）對 far pointer 的 `<`、`<=`、`>`、`>=` **只比較 offset，忽略 segment**。只有 `==` 和 `!=` 才比較完整的 segment:offset。這導致 bounds checking 完全失效（[Chen, 2020](https://devblogs.microsoft.com/oldnewthing/20200728-00/?p=104012)）：

```c
// p = 1000:0100（buffer 起點），N = 0x200
// q = 2000:0200（完全不在 buffer 裡！physical 差了 64KB）
if (p <= q && q <= p + N) {
    // 編譯器只比 offset：0x0100 <= 0x0200 <= 0x0300 → 全部通過！
    // 但 q 根本不在 buffer 裡
}
```

### 為什麼 386 Paging 消滅了 Memory Model

386 的 flat model（base=0, limit=4GB）加上 paging 後，所有 pointer 都是 32-bit near pointer，統一指向同一個 4GB address space。不再需要區分 near/far/huge，也不再需要選擇 memory model。Raymond Chen 提到，`windows.h` 至今仍定義著 `NEAR` 和 `FAR` 巨集，但它們被定義為空白，只為了舊程式碼的向後相容（[Chen, 2020](https://devblogs.microsoft.com/oldnewthing/20200728-00/?p=104012)）。

---

## 38. Paging 如何解決 Segmentation 的問題

### 核心差異：連續 vs 非連續

| | Segmentation | Paging |
|---|---|---|
| 分配單位 | 可變大小的 segment | 固定大小的 page（4KB） |
| Physical memory | 必須連續 | **不需要連續** |
| 碎片類型 | External fragmentation（嚴重） | Internal fragmentation（輕微，最多浪費 4095 bytes） |
| 虛擬 → 實體映射 | segment base + offset | page table 逐 page 映射 |

### 為什麼 Page 不需要連續的 Physical Memory？

Page table 為每個 virtual page 提供獨立的映射。例如程式認為自己擁有連續的 128KB 記憶體，但 physical memory 可以是分散的：

```
Virtual Address          Page Table          Physical Address
┌────────────┐         ┌──────────┐
│ Page 0     │ ───────→│ Frame 7  │ ──→ 0x7000
│ 0x0000     │         │          │
├────────────┤         ├──────────┤
│ Page 1     │ ───────→│ Frame 2  │ ──→ 0x2000
│ 0x1000     │         │          │
├────────────┤         ├──────────┤
│ Page 2     │ ───────→│ Frame 15 │ ──→ 0xF000
│ 0x2000     │         │          │
└────────────┘         └──────────┘

程式看到連續的 0x0000~0x2FFF
Physical memory 實際分散在 0x7000、0x2000、0xF000
```

這就是 paging 消除 external fragmentation 的根本原因——不再需要尋找連續的 physical memory 空間。

---

## 39. Paging 之後的 Fragmentation——為什麼問題沒有消失

### Paging 解決的是哪一層？

Paging 解決的是 **OS 分配 physical frame 給 process** 這一層的 external fragmentation。Page table 讓每個 4KB virtual page 可以映射到任意 physical frame，所以 OS 不需要找連續的 physical memory。但程式不會每次需要記憶體時都向 OS 要一整個 4KB page——中間還有一層 **heap allocator**（`malloc`/`free`）。

### 兩層架構

```
應用程式:   malloc(24)  malloc(64)  free(...)  malloc(128)
              │
         ┌────▼─────────────────────────────┐
 Layer 2 │  Heap Allocator (malloc/free)    │  ← 在 virtual address space 內
         │  管理 page 內部的 bytes 級分配     │     管理次級分配
         └────┬─────────────────────────────┘
              │  需要更多 page 時才呼叫
         ┌────▼─────────────────────────────┐
 Layer 1 │  OS Paging (mmap/brk)           │  ← 以 4KB page 為單位
         │  virtual page → physical frame   │     無 external fragmentation
         └──────────────────────────────────┘
```

### Heap-Level External Fragmentation

在 Layer 2 中，allocator 拿到的是連續的 virtual address space，它要在裡面切割出不同大小的 block 給應用程式。經過多次 `malloc`/`free` 後：

```
某個 page 的 virtual address 空間內部：
┌──────┬──────┬──────┬──────┬──────┬──────┐
│ 24B  │ free │ 64B  │ free │ 32B  │ free │
│ used │ 40B  │ used │ 16B  │ used │ 8B   │
└──────┴──────┴──────┴──────┴──────┴──────┘
```

free 總共有 64B（40+16+8），但如果需要連續的 50B，沒有任何一個 free block 夠大。這就是 **heap-level external fragmentation**——跟 §35 描述的 segmentation 問題在本質上相同，只是發生的層級不同。

### Paging 為什麼管不到這一層？

Paging 的映射粒度是 4KB page。它保證 OS 可以把任意分散的 physical frame 拼成連續的 virtual space，但它不管 page 內部的 bytes 級別怎麼切。Heap allocator 在 virtual address space 裡做的事，本質上就是一個 variable-size allocation 問題——跟 segmentation 面對的問題結構完全一樣，只是：

| | Segmentation 時代 | Paging 時代 |
|---|---|---|
| 發生層級 | Physical memory | Virtual address space（heap） |
| 管理者 | OS | Allocator（`malloc`） |
| 單位 | Segment（KB~MB） | Block（bytes~KB） |
| 嚴重程度 | 嚴重（涉及 physical memory） | 較輕（只影響 virtual space，可以跟 OS 要更多 page） |

Segmentation 時代的 fragmentation 是致命的——physical memory 就那麼多，碎片化後真的無法分配。Paging 時代的 heap fragmentation 嚴重程度較低，因為 allocator 空間不夠時可以向 OS 要更多 page（`brk`/`mmap`），virtual address space 通常遠大於 physical memory（32-bit 有 4GB，64-bit 有 128TB），且 physical memory 不足時可以透過 page swap 到 disk。但問題並沒有消失——在長時間運行的程式（如 server、database）中，heap fragmentation 仍然是實際的工程問題，這也是 `jemalloc`、`tcmalloc` 等專門 allocator 存在的原因。

---

## 40. Virtual Address Space Fragmentation 的意義

### Physical 不連續不是問題，那 virtual 碎片化在說什麼？

Physical memory 在 paging 架構下本來就不連續——page table 的設計就是接受這件事。所以 physical 不連續是正常狀態，不是問題。Virtual address space fragmentation 說的是另一件事：**有足夠的 free space 卻分配不出來**。

程式呼叫 `malloc(100MB)` 時，回傳的 pointer 必須指向一段 **virtual address 連續**的空間，因為程式要用 pointer arithmetic 存取：

```c
int* arr = malloc(1000 * sizeof(int));
arr[500] = 42;   // 編譯器算成 *(arr + 500)
                  // 必須 virtual address 連續才能算
```

Physical 可以散在任何 frame，page table 會處理。但 virtual address 必須連續——這是 pointer arithmetic 的前提。Heap allocator 管理的就是這段 virtual address space，用久了之後碎片化的結果是：明明 free bytes 總量夠，卻找不到一段夠大的連續 virtual address range。

### 為什麼不直接跟 OS 要新的 page？

可以。Allocator 空間不夠時可以用 `brk`/`mmap` 要更多 page，這就是為什麼 heap fragmentation 不致命。但代價是：

**1. 浪費 physical memory**

舊的 page 裡有零碎的 used block 散布，整個 page 無法歸還 OS：

```
Page A（4KB）：
┌──────────┬──────────────────────────────────┐
│ 24B used │          4072B free              │
└──────────┴──────────────────────────────────┘
  ↑ 只剩這一小塊 used，但整個 page 不能還給 OS
    因為這 24B 的 virtual address 還在被程式持有
```

每個這樣的 page 都佔一個 physical frame。Fragmentation 越嚴重，被「釘住」的 page 越多，浪費越多 RAM。

**2. 32-bit 系統的 virtual address space 會用完**

32-bit process 的 virtual space 只有 2~3GB。大量碎片化後，即使 free bytes 總量夠，也找不到連續的 virtual range：

```
32-bit virtual space（2GB）：
┌─┬───┬─┬───┬─┬───┬─┬───┬─┬───┐
│U│ F │U│ F │U│ F │U│ F │U│ F │   U=used, F=free
└─┴───┴─┴───┴─┴───┴─┴───┴─┴───┘
  free 總量 = 800MB
  最大連續 free = 200MB
  → malloc(500MB) 失敗
```

64-bit 系統的 virtual space 有 128TB，這個問題幾乎不存在。

**3. 性能下降**

碎片化代表同樣的資料分散在更多 page 上 → 佔用更多 TLB entry → 更多 TLB miss。

### 總結

```
fragmentation ≠ 不連續
fragmentation = 有足夠的 free space 卻分配不出來

physical 不連續？→ 正常，paging 就是這樣設計的，不是問題
virtual 不連續？ → 是問題，因為 pointer arithmetic 需要 virtual 連續
```

在 64-bit 系統上，virtual address space fragmentation 幾乎不是問題（空間太大了）。真正痛的是 physical memory 被碎片化的 page 釘住而浪費——這也是 buddy system（[§43](#43-buddy-system)）和 memory compaction（[§44](#44-對抗-kernel-level-fragmentation)）要解決的事。

---

## 41. 跨 Page 的存取

### MMU 自動處理跨 Page Boundary 的存取

當一筆存取（例如讀取一個 4-byte `int`）剛好落在 page boundary 上，CPU 需要做兩次 TLB lookup：

```
         Page N                    Page N+1
    ┌───────────────┐         ┌───────────────┐
    │       ... [2B]│         │[2B] ...       │
    └───────────────┘         └───────────────┘
    Physical Frame 7          Physical Frame 200
```

完整流程：

```
CPU 執行 mov eax, [0x1FFE]     （讀取 4 bytes，從 0x1FFE 到 0x2001）
  │
  ▼
MMU 檢測到這次存取跨越了 page boundary
  │  （0x1FFE~0x1FFF 在 Page 1，0x2000~0x2001 在 Page 2）
  │
  ▼
MMU 自動拆成兩次 physical memory 存取：
  ├─ Page 1 offset 0xFFE → 查 page table → Frame 7   → 讀 0x7FFE~0x7FFF（2 bytes）
  └─ Page 2 offset 0x000 → 查 page table → Frame 200 → 讀 0xC8000~0xC8001（2 bytes）
  │
  ▼
硬體拼接結果，放入 eax
```

程式寫 `int x = *(int*)0x1FFE;` 完全不需要知道 page boundary 在哪。OS 需要做的只有一件事：確保 Page 1 和 Page 2 的 page table entry 都是 valid 的。如果其中一個 page 被 swap 到 disk（Present bit = 0），CPU 會觸發 page fault，OS 才介入把那個 page 載回 physical memory。

### 性能問題

雖然硬體透明處理了跨 page 存取，但性能損失是真實的：

**TLB miss**：每切換一個 page 就需要一次 TLB lookup。TLB 是有限的（典型 64~1024 entries），如果資料量大，會產生 TLB miss，要做 page table walk（走多級 page table），代價很高。即使 virtual address 是連續的，如果 physical frame 分散，也會降低 cache locality——DRAM 的 row buffer 預取是基於 physical address 的連續性。

這是 Linux kernel 提供 **huge page**（2MB / 1GB）的原因——用更大的 page 來減少 TLB entries 的消耗。Allocator 和 compiler 也會做 **alignment**，盡量讓資料結構不跨 page boundary。

---

## 42. Kernel 為什麼需要 Physically Contiguous Memory

Paging 的透明轉換靠的是 MMU。但有些場景 MMU 介入不了。

### DMA——硬體繞過 MMU

```
CPU 存取記憶體：
  CPU → Virtual Addr → [MMU/Page Table] → Physical Addr → DRAM
                        ^^^^^^^^^^^^^^^^
                        有翻譯，不需要連續

DMA 設備存取記憶體：
  網卡/磁碟控制器 → Physical Addr → DRAM
                    ^^^^^^^^^^^^
                    沒有 MMU，直接用 physical address
```

網卡要把收到的封包 DMA 寫入一塊 buffer。它拿到的是 physical address，不經過 page table。如果 OS 給它的 buffer 是 physical 不連續的 Frame 7 和 Frame 200，網卡不知道怎麼「跳」到 Frame 200——它只會從 Frame 7 一路往 Frame 8 寫下去，覆蓋別人的資料。所以 DMA buffer 必須 physically contiguous（現代有 IOMMU 可以幫設備做地址翻譯，也有 scatter-gather DMA 可以給設備一個 physical fragment list，但不是所有硬體都支援，且有額外開銷）。

### Kernel 的 Direct Mapping

Linux kernel 把整個 physical memory 做了一個 identity mapping（直接映射）：

```
Kernel Virtual Address Space:
┌─────────────────────────────────┐ 0xFFFFFFFF
│  vmalloc 區域（非連續映射）        │
├─────────────────────────────────┤
│  Direct Mapping 區域             │
│  virtual = physical + PAGE_OFFSET│
│  例如：virtual 0xC0001000        │
│      = physical 0x00001000       │
├─────────────────────────────────┤ PAGE_OFFSET (e.g. 0xC0000000)
│  User Space                     │
└─────────────────────────────────┘ 0x00000000
```

在 direct mapping 區域中，virtual 連續 = physical 連續。`kmalloc` 從這個區域分配，所以 virtual-to-physical 轉換只需要加減一個固定常數，極快，且不需要額外的 page table entry 操作——但代價就是要求 physical 連續。

### kmalloc vs vmalloc

Kernel 有兩套分配器。`kmalloc` 從 direct mapping 區域分配，physical 連續，分配速度快（buddy system 直接拿），VA↔PA 轉換只需加減常數，TLB 壓力低（direct mapping 用大頁），且 DMA 可用。`vmalloc` 透過 page table 做非連續映射，physical 不需要連續，但分配慢（要修改 page table、flush TLB），每次 VA↔PA 轉換要走 page table walk，TLB 壓力高（每個 page 需要獨立 TLB entry），且不能直接用於 DMA。

| | `kmalloc` | `vmalloc` |
|---|---|---|
| Physical 連續 | 是 | 否 |
| 分配速度 | 快（buddy system 直接拿） | 慢（要修改 page table、flush TLB） |
| VA ↔ PA 轉換 | 加減常數 | 走 page table walk |
| TLB 壓力 | 低（direct mapping 用大頁） | 高（每個 page 需要獨立 TLB entry） |
| DMA 可用 | 可以 | 不行（除非 scatter-gather） |

Kernel 是整個系統最頻繁操作記憶體的部分，如果每次分配都要改 page table 和 flush TLB，性能損失太大。所以小型、頻繁的 kernel 分配都走 `kmalloc`。而 `kmalloc` 要求 physical 連續，就退回到了需要找 contiguous physical frame 的老問題——用 **buddy system** 管理（見 [§43](#43-buddy-system)）。

---

## 43. Buddy System

### 核心思想

所有 block 的大小都是 **2 的冪次個 page**（1, 2, 4, 8, ... 1024 pages），用一組 free list 管理：

```
free_list[0]  →  1 page 的 free blocks
free_list[1]  →  2 pages 的 free blocks
free_list[2]  →  4 pages 的 free blocks
free_list[3]  →  8 pages 的 free blocks
...
free_list[10] →  1024 pages (4MB) 的 free blocks
```

這些 free list 對應一棵二元樹，root 是整塊 physical memory，每次對半切：

```
                        ┌─────────────────────────────────┐
            order 4     │           0 ~ 15                │  16 pages
                        └────────────────┬────────────────┘
                                         │ split
                        ┌────────────────┴────────────────┐
            order 3     │     0 ~ 7      │     8 ~ 15     │  8 pages
                        └───────┬────────┴────────┬───────┘
                                │ split           │ split
                        ┌───────┴───────┐  ┌──────┴───────┐
            order 2     │  0~3  │  4~7  │  │ 8~11 │ 12~15 │  4 pages
                        └──┬────┴──┬────┘  └──┬───┴───┬───┘
                           │       │          │       │
                        ┌──┴──┐ ┌──┴──┐  ┌────┴─┐ ┌───┴──┐
            order 1     │ 0~1 │ │ 2~3 │  │ 4~5  │ │ 6~7  │   ...
                        └─┬─┬─┘ └──┬──┘  └──┬───┘ └──────┘
                          │ │      │        │
            order 0     [0] [1]  [2] [3]  [4] [5]  ...        1 page
```

同一個 parent 切出來的兩半互為 **buddy**。

### 分配：向上取 2 的冪次，往上找，往下切

需要的 page 數向上取最近的 2 的冪次：

```
需要的 pages    向上取 2 的冪次    order
─────────────────────────────────────────
  1               1               0
  2               2               1
  3               4               2
  4               4               2
  5               8               3
  6               8               3
  7               8               3
  8               8               3
  9              16               4
```

所以需要 3 pages 就分配 4 pages，浪費 1 page。需要 5 pages 就分配 8 pages，浪費 3 pages。這就是 buddy system 的 **internal fragmentation**——分配單位只能是 2 的冪次，不是剛好需要的大小。Linux 用 **slab allocator** 來緩解這個問題：先用 buddy system 拿整頁，再把頁內切成固定大小的小物件（32B, 64B, 128B...），減少浪費。

以下用一個 16-page 的例子走完分配流程。

**分配 A = 3 pages（需要 order 2，即 4 pages）：**

`free_list[2]` 是空的，往上找 `free_list[3]` 也空，找到 `free_list[4]`：

```
Step 1: 從 free_list[4] 取出唯一的 16-page block

  ┌─────────────────────────────────────────────────┐
  │ 0   1   2   3   4   5   6   7  ···  15          │  ← 取出
  └─────────────────────────────────────────────────┘

Step 2: split 成兩個 8-page block

  ┌───────────────────────┐  ┌───────────────────────┐
  │ 0   1   2   3  4 5 6 7│  │ 8   9  10  11 ··· 15  │
  └───────────────────────┘  └───────────────────────┘
            ↓                          ↓
         繼續切                  放回 free_list[3]

Step 3: 左半再 split 成兩個 4-page block

  ┌───────────┐  ┌───────────┐
  │ 0  1  2  3│  │ 4  5  6  7│
  └───────────┘  └───────────┘
       ↓               ↓
   分配給 A ✓     放回 free_list[2]
```

分配完的全域狀態：

```
free_list[4]:  (空)
free_list[3]:  [ 8~15 ]
free_list[2]:  [ 4~7  ]
free_list[1]:  (空)
free_list[0]:  (空)

記憶體長這樣：
  ┌──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┐
  │▓▓│▓▓│▓▓│▓▓│  │  │  │  │  │  │  │  │  │  │  │  │
  └──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┘
   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15

   ▓▓ = A 佔用       (空白) = free
```

**再分配 B = 1 page（需要 order 0）：**

`free_list[0]` 空 → `free_list[1]` 空 → `free_list[2]` 有 [4~7]，取出來切：

```
Step 1: 從 free_list[2] 取出 4~7，split

  ┌───────────┐
  │ 4  5  6  7│  ← 取出
  └───────────┘
        ↓ split
  ┌─────┐  ┌─────┐
  │ 4  5│  │ 6  7│
  └─────┘  └─────┘
    ↓         ↓
  繼續切   放回 free_list[1]

Step 2: 左半再 split

  ┌──┐  ┌──┐
  │ 4│  │ 5│
  └──┘  └──┘
   ↓      ↓
  B ✓   放回 free_list[0]
```

現在的記憶體：

```
  ┌──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┐
  │▓▓│▓▓│▓▓│▓▓│██│  │  │  │  │  │  │  │  │  │  │  │
  └──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┘
   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15

   ▓▓ = A        ██ = B        (空白) = free

free_list[3]:  [ 8~15 ]
free_list[1]:  [ 6~7  ]
free_list[0]:  [ 5    ]
```

### 釋放：XOR 找 Buddy，往上合併

釋放一個 block 時，檢查它的 buddy（同一個 parent 切出來的另一半）是不是也是 free 的。如果是，合併回 parent，然後繼續往上檢查。

**怎麼找到 buddy？** 因為 block 大小都是 2 的冪次，buddy 的位址只差一個 bit：

```
公式：buddy 起始 frame = 自己的起始 frame XOR (1 << order)
```

例如 A 是 order 2（4 pages），起始 frame = 0：

```
buddy = 0 XOR (1 << 2)
      = 0 XOR 4

  0 的二進位：  000
  4 的二進位：  100
  XOR 結果：    100  = 4

  → buddy 起始 frame = 4，也就是 Frame 4~7
```

反過來也成立，站在 Frame 4 的角度找 buddy：

```
buddy = 4 XOR (1 << 2)
      = 4 XOR 4

  4 的二進位：  100
  4 的二進位：  100
  XOR 結果：    000  = 0

  → buddy 起始 frame = 0，也就是 Frame 0~3
```

XOR 之所以能做到，是因為 buddy system 永遠對半切，切的位置剛好對應二進位的某一個 bit：

```
order 0（1 page）：buddy 差在 bit 0
  Frame 4 = 100
  Frame 5 = 101   ← 翻轉 bit 0，互為 buddy。XOR 1

order 1（2 pages）：buddy 差在 bit 1
  Frame 4 = 100
  Frame 6 = 110   ← 翻轉 bit 1，互為 buddy。XOR 2

order 2（4 pages）：buddy 差在 bit 2
  Frame 0 = 000
  Frame 4 = 100   ← 翻轉 bit 2，互為 buddy。XOR 4
```

**釋放 A（Frame 0~3，order 2）：**

```
釋放 A，看它的 buddy 能不能合併：

  A 的 buddy = 0 XOR (1<<2) = 0 XOR 4 = Frame 4
  Frame 4 是 B，正在使用 → 不能合併

  ┌──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┐
  │  │  │  │  │██│  │  │  │  │  │  │  │  │  │  │  │
  └──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┘
   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
   ╰─────────╯  ↑
   放回 free     B 擋住，無法合併

free_list[3]:  [ 8~15 ]
free_list[2]:  [ 0~3  ]         ← A 放回這裡
free_list[1]:  [ 6~7  ]
free_list[0]:  [ 5    ]
```

**釋放 B（Frame 4，order 0）——連鎖合併：**

```
Round 1:  釋放 Frame 4（order 0）
          buddy = 4 XOR (1<<0) = 4 XOR 1 = 5
          Frame 5 free ✓ → 合併！

          ┌──┬──┐
          │ 4│ 5│ → 合成 order 1 block
          └──┴──┘

Round 2:  現在有 Frame 4~5（order 1）
          buddy = 4 XOR (1<<1) = 4 XOR 2 = 6
          Frame 6~7 free ✓ → 合併！

          ┌──┬──┬──┬──┐
          │ 4│ 5│ 6│ 7│ → 合成 order 2 block
          └──┴──┴──┴──┘

Round 3:  現在有 Frame 4~7（order 2）
          buddy = 4 XOR (1<<2) = 4 XOR 4 = 0
          Frame 0~3 free ✓ → 合併！

          ┌──┬──┬──┬──┬──┬──┬──┬──┐
          │ 0│ 1│ 2│ 3│ 4│ 5│ 6│ 7│ → 合成 order 3 block
          └──┴──┴──┴──┴──┴──┴──┴──┘

Round 4:  現在有 Frame 0~7（order 3）
          buddy = 0 XOR (1<<3) = 0 XOR 8 = 8
          Frame 8~15 free ✓ → 合併！

          ┌──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┐
          │ 0│ 1│ 2│ 3│ 4│ 5│ 6│ 7│ 8│ 9│..│..│..│..│..│15│
          └──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┘
          → 合成 order 4 block，完全還原

free_list[4]:  [ 0~15 ]    ← 回到初始狀態
```

### struct page——Allocator 怎麼知道每個 block 的位置和大小

Linux 在開機時，對每個 physical frame 建立一個 `struct page`，用陣列存：

```
Physical Memory:
┌──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┐
│ 0│ 1│ 2│ 3│ 4│ 5│ 6│ 7│ 8│ 9│..│..│..│..│..│15│
└──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┘
  │  │  │  │  │
  ▼  ▼  ▼  ▼  ▼

struct page 陣列（mem_map）：
┌────────┬────────┬────────┬────────┬────────┬───
│ page[0]│ page[1]│ page[2]│ page[3]│ page[4]│...
│ order=2│        │        │        │ order=0│
│ 是首頁  │        │        │        │ 是首頁  │
└────────┴────────┴────────┴────────┴────────┴───
```

分配 A（Frame 0~3，order 2）時，在 `page[0]` 記下 `order = 2`。分配 B（Frame 4，order 0）時，在 `page[4]` 記下 `order = 0`。每次 split，parent 的 order 被覆蓋，兩個 child 的首頁各記自己的新 order：

```
初始：16-page block，order 4 記在 page[0]

page:  [0]  [1]  [2]  [3]  [4]  [5]  [6]  [7] ... [15]
order:  4    -    -    -    -    -    -    -        -

Split 1:  order 4 → 兩個 order 3

page:  [0]  [1]  [2]  [3]  [4]  [5]  [6]  [7]  [8] ... [15]
order:  3    -    -    -    -    -    -    -    3       -
        ╰──── 左半 ────╯                       ╰── 右半，放回 free_list[3]

Split 2:  左半 order 3 → 兩個 order 2

page:  [0]  [1]  [2]  [3]  [4]  [5]  [6]  [7]  [8] ... [15]
order:  2    -    -    -    2    -    -    -    3       -
        ╰─ A 分配出去 ─╯   ╰─ 放回 free_list[2] ╯
```

釋放時，`kmalloc` 回傳的 virtual address 可以直接反推 frame number（因為 kernel direct mapping 是固定偏移：`frame = (virtual_addr - PAGE_OFFSET) / 4096`），然後查 `page[frame].order` 就知道這個 block 的大小，再用 XOR 找 buddy，O(1) 完成。`struct page` 永遠只記當前這個 block 的 order，不記歷史——split 就改小，merge 就改大。

### 為什麼能減少但無法消除 Fragmentation

**能減少**：2 的冪次對齊讓合併判斷極快（一個 XOR），只要 buddy 都 free 就能一路合併回大 block，不會留下零碎的小空洞。

**無法消除**：只要有一個 page 被佔住，它的 buddy 就無法合併，連鎖導致整條合併鏈斷掉：

```
16 pages 中只有 Frame 4 被佔住：

  ┌──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┐
  │  │  │  │  │██│  │  │  │  │  │  │  │  │  │  │  │
  └──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┘
   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15

  5 想跟 4 合併？ → 4 occupied ✗
    → 4~5 無法形成 → 4~7 無法形成 → 0~7 無法形成 → 0~15 無法形成

  最大可用連續塊：
  ┌──┬──┬──┬──┐  ┌──┐  ┌──┬──┐  ┌──┬──┬──┬──┬──┬──┬──┬──┐
  │  │  │  │  │  │██│  │  │  │  │  │  │  │  │  │  │  │  │
  └──┴──┴──┴──┘  └──┘  └──┴──┘  └──┴──┴──┴──┴──┴──┴──┴──┘
   order 2 (4)    佔用   order 1   order 3 (8) ← 最大只有這塊

  一個 page 卡在中間，最大連續塊從 16 降到 8
```

---

## 44. 對抗 Kernel-Level Fragmentation

Buddy system 無法消除 fragmentation 的根本原因是：一個被佔住的 page 會卡住整條合併鏈。Linux 在 buddy system 之上加了兩個機制來對抗。

### Anti-Fragmentation Grouping——從源頭隔離

把 physical page 分成三類，各自佔不同區域，避免互相卡住：

```
Physical Memory:
┌──────────────────┬──────────────────┬──────────────────┐
│   MOVABLE 區域    │  UNMOVABLE 區域   │ RECLAIMABLE 區域  │
│  user process     │  kernel 內部結構   │  file cache       │
│  可以搬走          │  搬不動           │  可以直接丟掉      │
└──────────────────┴──────────────────┴──────────────────┘
```

Unmovable page 只會在自己的區域內佔位，不會跑到 movable 區域卡住別人的合併鏈。

### Memory Compaction——強制搬走

如果 movable 區域裡的 buddy 被佔住，OS 可以把那個 page 的內容搬到別的 frame，然後更新 page table 指向新位置：

```
搬之前：
  ┌──┬──┬──┬──┐
  │  │  │██│  │   ← Frame 2 擋住合併
  └──┴──┴──┴──┘

OS 把 Frame 2 的內容複製到別處，改 page table：
  ┌──┬──┬──┬──┐
  │  │  │  │  │   ← 四個都 free，可以合併成 order 2
  └──┴──┴──┴──┘
```

但這只對 movable page 有效。Kernel 自己用的 unmovable page 搬不了——kernel direct mapping 是固定偏移，搬了 physical 位置就對不上了。

### 根本限制

Paging 不是消滅 fragmentation 的銀彈。它做的是把問題從致命降級為可管理：physical level fragmentation 被 MMU + page table 解決，大部分場景不再需要 physical 連續。但 heap allocator level 的 fragmentation 問題結構不變；跨 page 的性能損失是真實的（TLB miss、cache locality 下降）；真正需要 physical 連續的場景（DMA 等）fragmentation 問題依然存在；unmovable page 造成的 fragmentation 無解，只能靠隔離來降低影響。

---

# Part 10: 參考資料

---

## 45. References

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

### 記憶體管理與 Memory Model 相關

8. **Raymond Chen, "A look back at memory models in 16-bit MS-DOS" (2020)**
   - Memory model 的 2×2 矩陣、near/far/huge pointer、far pointer 比較的陷阱
   - https://devblogs.microsoft.com/oldnewthing/20200728-00/?p=104012

9. **Gordon Letwin, *Inside OS/2*, Microsoft Press (1988)**
   - Section 9.2.2: Huge Memory — DosAllocHuge 的設計、selector 等間距排列、Real Mode vs Protected Mode 的對比
   - Online: https://gunkies.org/wiki/Inside_OS/2
   - Internet Archive: https://archive.org/details/InsideOS2GordonLetwin

10. **Bumbershoot Software, "Memory Models and Far Pointers" (2022)**
    - 6 種 memory model 的表格、Amiga 上的類似模式
    - https://bumbershootsoft.wordpress.com/2022/08/05/memory-models-and-far-pointers/

11. **XtoF, "Intel 80386, a revolutionary CPU" (2023)**
    - 386 團隊的設計調查：「所有客戶都討厭 segmented memory scheme」
    - https://www.xtof.info/intel80386.html

### OS/2 API 文件

12. **EDM2 — DosAllocSeg**
    - Size 參數 1~65536 bytes，OS 設定精確的 descriptor limit
    - https://www.edm2.com/index.php/DosAllocSeg

13. **EDM2 — DosAllocHuge**
    - 分配多個等間距 segment，最後一個 segment 為 "non-65536-byte segment"
    - https://www.edm2.com/index.php/DosAllocHuge

14. **osFree Wiki — DosReallocSeg**
    - 事後調整 segment 大小，OS 更新 descriptor limit
    - http://www.osfree.org/doku/doku.php?id=en:docs:fapi:dosreallocseg

15. **Microsoft KB Q73187 — Using Huge Memory Model and Huge Arrays in MASM**
    - OS/2 MASM 程式碼範例，跨 segment 存取
    - https://jeffpar.github.io/kbarchive/kb/073/Q73187/

16. **osfree-project/os3 (GitHub)**
    - `shared/app/os2app/kal/kal.c`: descriptor limit 設定為 `(size - 1)`，證明 limit 是浮動的
    - https://github.com/osfree-project/os3

### GAS Syntax 相關

17. **NASM Documentation, Section 2**
    - NASM vs MASM 的 label 語意差異
    - https://www.nasm.us/xdoc/2.16.03/html/nasmdoc2.html

18. **Microsoft MASM .MODEL directive**
    - Memory model 的正式規格
    - https://learn.microsoft.com/en-us/cpp/assembler/masm/dot-model?view=msvc-170

### Kernel 記憶體管理與 Buddy System 相關

19. **Mel Gorman, *Understanding the Linux Virtual Memory Manager* (2004)**
    - Chapter 6: Physical Page Allocation — Buddy allocator 的完整描述
    - Chapter 10: Page Frame Reclaiming — Memory compaction 與 anti-fragmentation
    - https://www.kernel.org/doc/gorman/html/understand/

20. **Linux Kernel Source Code — Buddy System**
    - `mm/page_alloc.c`: buddy allocator 核心實作（`__alloc_pages`、`__free_one_page`）
    - `include/linux/mmzone.h`: `struct zone` 中的 `free_area[MAX_ORDER]` free list 定義
    - `include/linux/mm_types.h`: `struct page` 定義（含 `private` 欄位存 order）

21. **Linux Kernel Source Code — Memory Compaction**
    - `mm/compaction.c`: memory compaction 實作
    - `mm/migrate.c`: page migration（搬移 movable page）

22. **Vlastimil Babka, "Memory Compaction" (2014)**
    - Linux kernel memory compaction 的設計動機與 anti-fragmentation grouping（MOVABLE / UNMOVABLE / RECLAIMABLE）
    - https://lwn.net/Articles/591998/

23. **Jonathan Corbet, "A deep dive into CMA" (2012)**
    - Contiguous Memory Allocator：啟動時預留連續區域給 DMA 使用
    - https://lwn.net/Articles/486301/
