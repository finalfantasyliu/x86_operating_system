# 01b - Segment Register Initialization & Far Jump

BIOS 把 boot sector 載入到 physical address `0x7C00` 後，CS:IP 的組合**因 BIOS 實作而異**。
本課透過實驗理解 far jump 的作用，並完成 segment register 的初始化與 stack 設置。

---

## Table of Contents

1. [問題：BIOS 跳入 bootloader 時 CS:IP 不確定](#1-問題bios-跳入-bootloader-時-csip-不確定)
2. [解法：Far Jump 歸零 CS](#2-解法far-jump-歸零-cs)
3. [Segment Register 初始化](#3-segment-register-初始化)
4. [Stack Pointer 設置](#4-stack-pointer-設置)
5. [實驗：故意設錯 CS 觀察行為](#5-實驗故意設錯-cs-觀察行為)
6. [實驗：用 print_string 驗證程式碼是否被執行](#6-實驗用-print_string-驗證程式碼是否被執行)
7. [完整程式碼（含註解）](#7-完整程式碼含註解)

---

## 1. 問題：BIOS 跳入 bootloader 時 CS:IP 不確定

BIOS 載入 boot sector 到 physical address `0x7C00`，但跳過來的方式有兩種：

| BIOS 實作 | CS | IP | Physical Address |
|---|---|---|---|
| 方式 A | `0x0000` | `0x7C00` | `0x0000 * 16 + 0x7C00 = 0x7C00` |
| 方式 B | `0x07C0` | `0x0000` | `0x07C0 * 16 + 0x0000 = 0x7C00` |

兩種方式算出來的 physical address 一樣，但 CS 的值不同。如果後續程式碼依賴 CS 為某個特定值（例如用 CS 做 segment override），就會出問題。

### Real Mode 定址公式

```
Physical Address = Segment * 16 + Offset
```

Segment register 左移 4 bits（乘以 16）再加上 offset，得到 20-bit physical address。

---

## 2. 解法：Far Jump 歸零 CS

用 far jump 同時設定 CS 和 IP，把 CS 強制歸零：

```asm
_start:
    jmp 0:_offset      // far jump: CS = 0, IP = _offset
_offset:
    // 此時 CS 確定是 0
```

### Near Jump vs Far Jump

| 類型 | 語法 | 效果 |
|---|---|---|
| Near Jump | `jmp _offset` | 只改 IP，CS 不變 |
| Far Jump | `jmp segment:offset` | 同時設定 CS 和 IP |

Far jump 的格式是 `jmp segment:offset`，冒號左邊載入到 CS，右邊載入到 IP。

### 機器碼分析

far jump 編譯後的機器碼（從反組譯觀察）：

```
7c00:  ea 05 7c 00 00    // opcode=EA, offset=0x7C05 (little-endian), segment=0x0000
```

- `EA` = far jump opcode
- `05 7C` = offset（little-endian → `0x7C05`，即 `_offset` 的 linker 地址）
- `00 00` = segment（`0x0000`）

跳完後：CS = `0x0000`，IP = `0x7C05`，physical = `0x0000 * 16 + 0x7C05` = `0x7C05`。

---

## 3. Segment Register 初始化

BIOS 不保證 DS、ES、SS、FS、GS 的初始值，必須手動歸零：

```asm
_offset:
    xor     ax, ax      // ax = 0
    mov     ds, ax      // Data Segment = 0
    mov     es, ax      // Extra Segment = 0
    mov     ss, ax      // Stack Segment = 0
    mov     fs, ax      // FS = 0
    mov     gs, ax      // GS = 0
```

不能直接 `mov ds, 0`，x86 不支援 immediate 直接寫入 segment register，必須透過通用暫存器中轉。

### 為什麼用 `xor ax, ax` 而不是 `mov ax, 0`？

- `xor ax, ax` 編譯成 2 bytes（`31 C0`）
- `mov ax, 0` 編譯成 3 bytes（`B8 00 00`）

在 boot sector 只有 512 bytes 的限制下，每個 byte 都很珍貴。

---

## 4. Stack Pointer 設置

```asm
    mov     esp, _start     // esp = 0x7C00
```

Stack 從 `0x7C00` 往下生長（push 時 ESP 遞減），利用 bootloader 下方的記憶體空間。

```
Memory Layout:
0x0000 ┌─────────────────┐
       │  IVT + BDA      │  ← BIOS 資料區，不能覆蓋
0x0500 ├─────────────────┤
       │  Free Memory    │  ← Stack 往下長到這裡
       │       ↓         │
0x7C00 ├─────────────────┤  ← ESP 初始值 / _start
       │  Boot Sector    │  ← 512 bytes
0x7E00 └─────────────────┘
```

注意：Stack 不能無限往下長，`0x0000`~`0x04FF` 是 Interrupt Vector Table 和 BIOS Data Area，覆蓋會導致中斷處理異常。

---

## 5. 實驗：故意設錯 CS 觀察行為

把 far jump 改成 `jmp 0x7C00:_offset`，故意將 CS 設為 `0x7C00`：

```asm
_start:
    jmp 0x7C00:_offset   // CS = 0x7C00, IP = 0x7C05
```

此時 physical address 計算：

```
CS * 16 + IP = 0x7C00 * 16 + 0x7C05 = 0x7C000 + 0x7C05 = 0x83C05
```

`0x83C05` 不是 bootloader 的位置（bootloader 在 `0x7C00`~`0x7DFF`），CPU 跑去執行未初始化的 RAM 內容。

### GDB 驗證

```
(gdb) info registers cs eip
cs    0x7c00
eip   0x7c07
(gdb) print /x 0x7C00*16 + $eip
$1 = 0x83c07
```

觀察到 physical address 是 `0x83C07`，確認 CPU 不在 bootloader 程式碼區域。

### 為什麼沒有立刻 crash？

因為 bootloader 預期行為就是 `jmp .`（無窮迴圈，畫面不動）。CPU 跑到 `0x83C07` 執行垃圾指令，如果剛好也卡在某處（HLT、loop、或碰巧的指令組合），從外面看起來一模一樣——都是畫面不動。兩種行為無法用肉眼區分。

---

## 6. 實驗：用 print_string 驗證程式碼是否被執行

為了**可觀察地**證明 far jump 設錯 CS 會導致後續程式碼不被執行，加入 BIOS `int 0x10` 印字功能：

```asm
    mov si, offset msg
    call print_string
    jmp .

print_string:
    push si             // 保存 caller 的暫存器狀態
    push ax
    push bx             // bh = active page，不保存可能影響其他 BIOS 功能

.loop:
    lodsb               // 從 DS:SI 讀一個 byte 到 AL，SI++
    or al, al           // 檢查是否為 null terminator
    jz .done

    mov ah, 0x0E        // BIOS teletype output function
    mov bh, 0           // active page number（通常為 0）
    int 0x10            // 呼叫 BIOS 中斷，印出 AL 的字元

    jmp .loop

.done:
    pop bx
    pop ax
    pop si
    ret

msg:
    .string "Hello world"
```

### 實驗結果

| Far Jump | 螢幕結果 | 原因 |
|---|---|---|
| `jmp 0:_offset` | 印出 "Hello world" | CS=0, physical address 正確，程式碼正常執行 |
| `jmp 0x7C00:_offset` | 什麼都沒印出 | CS=0x7C00, physical 跑到 0x83C05，print_string 根本沒被執行 |

這就是可觀察的證據：**segment 設錯，程式碼不會被執行到。**

### BIOS int 0x10, AH=0x0E 說明

- **AH = 0x0E**：Teletype Output，印一個字元到螢幕，光標自動右移
- **AL**：要印的字元 ASCII 碼
- **BH**：Active page number，一般為 0（如果設成 1，寫到不可見的頁面，螢幕上看不到）

---

## 7. 完整程式碼（含註解）

```asm
/**
 * 16-bit and 32-bit mixed startup code
 */
#include "os.h"

.global _start
.code16
.intel_syntax noprefix

.text
_start:
    // jmp 0x7c00:_offset  // 測試用：故意設錯 CS，證明會跳到錯誤位置
    jmp 0:_offset           // far jump: 歸零 CS，確保 segment 一致

_offset:
    xor     ax, ax          // ax = 0
    mov     ds, ax          // Data Segment = 0
    mov     es, ax          // Extra Segment = 0
    mov     ss, ax          // Stack Segment = 0
    mov     fs, ax          // FS = 0
    mov     gs, ax          // GS = 0
    mov     esp, _start     // Stack 從 0x7C00 往下長

    jmp     .               // 無窮迴圈

    .org 0x1FE, 0x90        // 填充 NOP 到 offset 0x1FE
    .byte 0x55, 0xAA        // Boot signature
```
