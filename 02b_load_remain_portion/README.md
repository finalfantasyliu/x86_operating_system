# 02b - 80386 Segment-Level Protection 完全解析

從 Real Mode 進入 Protected Mode 後，CPU 會自動對每一次記憶體存取做保護檢查。本篇從零開始，由淺入深解釋 Intel 386 的 Segment-Level Protection 機制。

參考來源：[Intel 80386 Programmer's Reference Manual - Section 6.3](https://www.scs.stanford.edu/nyu/04fa/lab/i386/s06_03.htm)

---

## Table of Contents

1. [為什麼需要保護？](#1-為什麼需要保護)
2. [Segment Descriptor（段描述符）](#2-segment-descriptor段描述符)
3. [Descriptor Table（描述符表格）](#3-descriptor-table描述符表格)
4. [Privilege Level（權限等級）](#4-privilege-level權限等級)
5. [五種保護機制詳解](#5-五種保護機制詳解)
6. [Gate Descriptor（閘門描述符）](#6-gate-descriptor閘門描述符)
7. [Stack Switching（堆疊切換）](#7-stack-switching堆疊切換)
8. [Instruction Restriction（指令限制）](#8-instruction-restriction指令限制)
9. [Pointer Validation Instructions（指標驗證指令）](#9-pointer-validation-instructions指標驗證指令)
10. [完整的大圖](#10-完整的大圖)

---

## 1. 為什麼需要保護？

### 1.1 沒有保護的世界：Real Mode

在 Real Mode 下，任何程式都可以存取任何記憶體位址，沒有任何限制：

```
程式 A 想寫 0x1234 → OK
程式 B 想寫 0x1234 → OK，A 的資料被蓋掉了
惡意程式想改 OS 的記憶體 → OK，整台電腦被搞爛
惡意程式想執行特權指令（例如關掉 CPU）→ OK，電腦直接當掉
```

這在早期只跑一個程式的電腦（如 DOS）還可以接受，但現代 OS 要同時跑很多程式，如果每個程式都能任意存取所有記憶體，那就是災難。

### 1.2 Protected Mode 的目標

Intel 386 引入 Protected Mode，讓 CPU 硬體自動做保護。每一次記憶體存取，CPU 都會自動檢查：

1. **你有權限嗎？**（一般程式不能碰 OS 的記憶體）
2. **你超出範圍了嗎？**（不能存取超過這塊記憶體的大小）
3. **你用對方式了嗎？**（不能把唯讀的當可寫的、不能把資料當程式碼執行）
4. **你走對入口了嗎？**（呼叫 OS 功能必須走指定入口，不能亂跳）
5. **你用了不該用的指令嗎？**（某些指令只有 OS 能用）

任何一項檢查不通過，CPU 就立刻觸發例外（exception），程式被強制停下來。這一切都是硬體自動做的，不需要 OS 用軟體去慢慢檢查。

---

## 2. Segment Descriptor（段描述符）

### 2.1 Real Mode vs Protected Mode 的 Segment

Real Mode：

```asm
mov ds, 0x1000    ; DS 直接就是一個「基底位址的簡寫」
; 存取 [ds:0x0050] = 0x1000 × 16 + 0x0050 = 0x10050
```

DS 裡面的值直接參與位址計算，非常簡單，但沒有任何保護資訊。CPU 不知道這塊記憶體有多大、是程式碼還是資料、誰能存取。

Protected Mode 完全不一樣：

```asm
mov ds, 0x10      ; 這不是位址！這是一個「索引」（叫 selector）
; CPU 拿 0x10 去一個表格（GDT）裡查找
; 查到的 entry 叫做「Segment Descriptor」
; 裡面記錄了：基底位址、大小、類型、權限...全部資訊
```

### 2.2 Segment Descriptor 的結構

Segment Descriptor 是一個 8 bytes（64 bits）的資料結構，描述一塊記憶體的所有資訊：

```
一個 Segment Descriptor（8 bytes = 64 bits）：

Byte 7  Byte 6  Byte 5  Byte 4  Byte 3  Byte 2  Byte 1  Byte 0
┌───────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┐
│Base   │ Flags │Access │Base   │Base Address    │Segment Limit  │
│31..24 │ Limit │Rights │23..16 │15..0           │15..0          │
│       │19..16 │       │       │                │               │
└───────┴───────┴───────┴───────┴───────┴───────┴───────┴───────┘
```

裡面包含的欄位：

| 欄位 | 大小 | 說明 |
|------|------|------|
| **Base Address** | 32 bits | 這塊記憶體的起始位址（被拆成三段分散在 descriptor 裡） |
| **Limit** | 20 bits | 這塊記憶體的大小上限（也被拆成兩段） |
| **Type** | 4 bits | 這是什麼類型的 segment（程式碼？資料？可讀？可寫？） |
| **DPL** | 2 bits | 需要什麼權限等級才能存取（0~3） |
| **G（Granularity）** | 1 bit | Limit 的單位（byte 或 4KB） |
| **P（Present）** | 1 bit | 這個 segment 是否真的在記憶體裡 |
| **S（System）** | 1 bit | 是系統用的 descriptor 還是一般的 code/data |
| **D/B** | 1 bit | 預設運算元大小（16-bit 或 32-bit） |

為什麼 Base 和 Limit 要被拆開分散在不同位置？因為這個格式是從 286 的 descriptor（只有 base 16-bit、limit 16-bit）擴展來的，為了向下相容，386 只能把新增的 bits 塞在旁邊，所以看起來很亂。這是歷史包袱。

### 2.3 Base Address — 這塊記憶體從哪開始

32-bit 的基底位址被拆成三塊散佈在 descriptor 裡：

```
Base 完整位址 = Base[31:24] | Base[23:16] | Base[15:0]
               (byte 7)      (byte 4)      (byte 2-3)
```

例如 Base = `0x00100000`，代表這個 segment 從實體記憶體的 1MB 處開始。

在 Protected Mode 裡，CPU 計算位址的方式變成：

```
實體位址 = Base（從 descriptor 裡讀的） + Offset（程式給的）
```

而不再是 Real Mode 的 `segment × 16 + offset`。

### 2.4 Limit — 這塊記憶體有多大

Limit 是 20 bits，但實際能表示的大小取決於 G bit（Granularity）：

**G = 0（byte 為單位）：**

```
Limit 的值就是最大的 offset
20 bits → 最大值 0xFFFFF = 1,048,575
所以 segment 最大 = 1MB
```

**G = 1（4KB page 為單位）：**

```
CPU 會自動把 Limit 左移 12 位，並把低 12 位填 1
也就是 Limit 變成 (Limit << 12) | 0xFFF

20 bits 左移 12 位 = 32 bits
最大值 = 0xFFFFF << 12 | 0xFFF = 0xFFFFFFFF = 4GB
```

舉例：

```
Limit = 0xFFFFF, G = 1
實際 Limit = 0xFFFFF × 4096 + 4095 = 0xFFFFFFFF = 4GB

Limit = 0x00001, G = 1
實際 Limit = 0x00001 × 4096 + 4095 = 8191 = 8KB
```

為什麼這樣設計？因為 20 bits 只能表示 1MB，但 386 是 32-bit CPU、能定址 4GB。加一個 G bit 讓它乘以 4096，就能用 20 bits 表達最大 4GB 的範圍。代價是 G=1 時最小單位變成 4KB。

### 2.5 Type 欄位 — 這塊記憶體是什麼類型

Type 是 4 bits，意義取決於 S bit（System bit）：

**S = 1（一般的 code/data segment）：**

```
Type 的 4 個 bit 分別是：

Bit 3: 0 = Data segment, 1 = Code segment

如果是 Data segment（Bit 3 = 0）：
  Bit 2 (E): Expand direction, 0 = 向上長, 1 = 向下長（用於 stack）
  Bit 1 (W): 0 = 唯讀, 1 = 可讀可寫
  Bit 0 (A): Accessed（CPU 自動設，表示有被存取過）

如果是 Code segment（Bit 3 = 1）：
  Bit 2 (C): Conforming, 0 = 一般, 1 = conforming（特殊權限行為）
  Bit 1 (R): 0 = 只能執行不能讀, 1 = 可執行也可讀
  Bit 0 (A): Accessed
```

用表格列出來：

| Type 值 | 說明 |
|---------|------|
| `0000` | Data：唯讀 |
| `0010` | Data：可讀寫 |
| `0100` | Data：唯讀，expand-down |
| `0110` | Data：可讀寫，expand-down |
| `1000` | Code：只能執行 |
| `1010` | Code：可執行、可讀 |
| `1100` | Code：只能執行，conforming |
| `1110` | Code：可執行、可讀，conforming |

重點理解：

- **可寫（W）**：Data segment 可以設定是否允許寫入。如果 W=0，任何寫入操作都會讓 CPU 報錯。Code segment 永遠不能寫入。
- **可讀（R）**：Code segment 可以設定是否允許讀取裡面的資料（例如讀常數）。如果 R=0，只能執行，不能用 `mov` 去讀裡面的值。
- **Conforming（C）**：允許低權限的程式「借用」高權限的程式碼，但不會改變呼叫者的權限。

CPU 什麼時候做 Type 檢查？兩個時機：

1. **載入 selector 到 segment register 時**：例如你不能把一個 data segment 的 selector 載入 CS（CS 只接受 code segment）。你也不能把一個不可讀的 code segment 載入 DS（DS 是拿來讀資料的）。
2. **指令實際使用 segment register 時**：例如你不能對一個 code segment 做寫入操作，即使你用了 segment override。

**S = 0（System descriptor）：**

S=0 時，Type 欄位代表的是系統用的特殊 descriptor：

| Type | 說明 |
|------|------|
| `0x0` | 保留 |
| `0x1` | Available 286 TSS |
| `0x2` | LDT |
| `0x3` | Busy 286 TSS |
| `0x4` | Call Gate |
| `0x5` | Task Gate |
| `0x6` | 286 Interrupt Gate |
| `0x7` | 286 Trap Gate |
| `0x9` | Available 386 TSS |
| `0xB` | Busy 386 TSS |
| `0xC` | 386 Call Gate |
| `0xE` | 386 Interrupt Gate |
| `0xF` | 386 Trap Gate |

這些是給 OS 用的特殊結構，後面講 Gate 時會用到。

---

## 3. Descriptor Table（描述符表格）

### 3.1 GDT（Global Descriptor Table）

所有的 Segment Descriptor 存在一個表格裡，這個表格叫 GDT。它就是記憶體裡的一個陣列，每個 entry 是 8 bytes。

```
記憶體中的 GDT：

位址             內容
──────────────────────────────────
GDT Base+0x00:  Index 0: NULL Descriptor（保留，不能用）
GDT Base+0x08:  Index 1: 例如 Kernel Code Segment
                  Base=0, Limit=4GB, Type=Code 可讀, DPL=0
GDT Base+0x10:  Index 2: 例如 Kernel Data Segment
                  Base=0, Limit=4GB, Type=Data 可讀寫, DPL=0
GDT Base+0x18:  Index 3: 例如 User Code Segment
                  Base=0, Limit=4GB, Type=Code 可讀, DPL=3
...以此類推
```

GDT 的位址和大小存在一個特殊暫存器 GDTR 裡，用特權指令 `lgdt` 來設定：

```asm
lgdt [gdt_descriptor]    ; 告訴 CPU：GDT 在記憶體的哪裡、有多大
```

GDT 本身也有一個 limit（存在 GDTR 裡），用來表示「GDT 表格的最大有效位址」。因為每個 descriptor 是 8 bytes，如果有 N 個 descriptor，limit = N × 8 - 1。CPU 在查表時會檢查你給的 index 有沒有超過這個 limit。

### 3.2 LDT（Local Descriptor Table）

除了全域的 GDT，還有 LDT——每個 task（process）可以有自己的 LDT。GDT 是全系統共享的，LDT 是各 task 私有的。

```
GDT ← 所有 task 共用（裝 kernel 的 segment、共享的 segment）
LDT ← 每個 task 各自擁有（裝自己私有的 segment）
```

不過現代 OS 幾乎不用 LDT，因為 paging 已經能做到隔離了。

### 3.3 Selector（選擇子）

存在 segment register 裡的值叫 selector，它不是位址，而是一個結構化的 16-bit 值：

```
15                        3  2  1  0
┌────────────────────────┬───┬─────┐
│        Index           │TI │ RPL │
│     (13 bits)          │   │(2b) │
└────────────────────────┴───┴─────┘

Index: 在 GDT 或 LDT 裡的第幾個 entry（13 bits → 最多 8192 個 entry）
TI:    Table Indicator
       0 = 去 GDT 找
       1 = 去 LDT 找
RPL:   Requestor's Privilege Level（請求者權限，後面會詳細講）
```

舉例：

```
selector = 0x08 = 0000 0000 0000 1000
  Index = 1, TI = 0（GDT）, RPL = 0

selector = 0x10 = 0000 0000 0001 0000
  Index = 2, TI = 0（GDT）, RPL = 0

selector = 0x1B = 0000 0000 0001 1011
  Index = 3, TI = 0（GDT）, RPL = 3
```

### 3.4 Segment Register 的隱藏部分

每個 segment register 其實有「看得到」和「看不到」兩個部分：

```
DS 的完整結構：

  看得到的部分（16 bits）：
  ┌──────────┐
  │ Selector │  ← 你用 mov ds, ax 設的值
  └──────────┘

  看不到的部分（hidden/cached，CPU 內部自動填入）：
  ┌──────────────────────────────────────┐
  │ Base Address (32 bits)               │  ← 從 descriptor 載入的
  │ Limit (32 bits)                      │  ← 從 descriptor 載入的
  │ Access Rights (Type, DPL, etc.)      │  ← 從 descriptor 載入的
  └──────────────────────────────────────┘
```

為什麼要 cache？因為如果 CPU 每次存取記憶體都要先去 GDT 查表，太慢了。所以當你 `mov ds, ax` 時，CPU 會一次查好，把結果存在 segment register 的隱藏部分。之後每次用 DS 存取記憶體，CPU 直接看 cache 裡的資訊做保護檢查，不需要額外的時鐘週期。

---

## 4. Privilege Level（權限等級）

### 4.1 四個權限等級

CPU 定義了 4 個權限等級，用數字 0~3 表示，數字越小權限越大：

```
         ┌───────────────┐
         │   Ring 0      │  ← 最高權限（OS Kernel）
         │ ┌───────────┐ │
         │ │  Ring 1    │ │  ← OS 驅動程式（很少用）
         │ │ ┌───────┐  │ │
         │ │ │Ring 2 │  │ │  ← OS 服務（很少用）
         │ │ │┌─────┐│  │ │
         │ │ ││Ring3││  │ │  ← 最低權限（一般應用程式）
         │ │ │└─────┘│  │ │
         │ │ └───────┘  │ │
         │ └───────────┘ │
         └───────────────┘
```

實際上大多數 OS（Linux、Windows）只用兩個：

- **Ring 0** = Kernel mode
- **Ring 3** = User mode

Ring 1 和 Ring 2 幾乎沒人用。如果你的系統只需要一個層級（沒有保護需求），用 Ring 0 就好。如果要兩個層級，用 Ring 0 和 Ring 3。

### 4.2 三個關鍵的權限值

CPU 在每次記憶體存取或控制轉移時，會比較三個權限值：

#### CPL（Current Privilege Level）— 你現在是誰

```
CPL = 目前正在執行的程式的權限等級
```

它存在 CS register 的最低 2 bits（就是 selector 的 RPL 欄位）。

```
如果 CS = 0x08（RPL = 0）→ CPL = 0 → 你是 Kernel
如果 CS = 0x1B（RPL = 3）→ CPL = 3 → 你是一般程式
```

正常情況下，CPL 等於目前執行的 code segment 的 DPL。只有在 conforming segment 的情況下，CPL 可能跟 DPL 不同。

#### DPL（Descriptor Privilege Level）— 門上寫的等級

```
DPL = 寫在 descriptor 裡的權限等級
    = 「你至少要有這個等級才能存取我」
```

例如一個 data segment 的 DPL = 0，代表只有 Ring 0 的程式能存取。

#### RPL（Requestor's Privilege Level）— 請求者聲稱的等級

```
RPL = 寫在 selector 最低 2 bits 的權限值
    = 「原始請求者的權限等級」
```

RPL 存在的意義是防止冒充。用一個例子解釋：

```
場景：OS 提供一個「讀檔案」的 system call
       FREAD(file_id, n_bytes, buffer_ptr)

正常使用（Ring 3 的程式）：
  buffer_ptr 指向自己的記憶體 → OK，正常讀檔

攻擊嘗試：
  Ring 3 的惡意程式呼叫 FREAD
  但 buffer_ptr 故意指向 OS 的 file table 記憶體
  如果 OS 不小心用 Ring 0 的權限去寫這個 buffer_ptr...
  → OS 的 file table 就被惡意程式覆蓋了！
```

RPL 就是為了防止這種事。當 Ring 3 傳一個 pointer 給 Ring 0 時：

```
Ring 3 傳入的 selector，RPL = 3（標記：「這個 pointer 來自 Ring 3」）

OS（Ring 0）用這個 selector 存取記憶體時，CPU 檢查：
  MAX(CPL, RPL) ≤ DPL ?
  MAX(0, 3) = 3
  3 ≤ 0 ?  → 不是！CPU 拒絕存取

即使 OS 是 Ring 0，但因為 RPL = 3，CPU 知道這個 pointer 來自 Ring 3，
所以用 Ring 3 的標準來檢查，保護了 OS 的記憶體。
```

還有一個專門的指令 ARPL（Adjust RPL）來幫 OS 做這件事：它確保 selector 的 RPL 至少跟呼叫者的 CPL 一樣大，防止權限提升。

---

## 5. 五種保護機制詳解

### 5.1 Type Checking（類型檢查）

CPU 會檢查你有沒有「用錯方式」存取 segment：

```
把 data segment 的 selector 載入 CS        → CS 只接受 code segment
把不可讀的 code segment 的 selector 載入 DS → DS 是拿來讀資料的，segment 必須可讀
對 code segment 做寫入操作                  → code segment 永遠不可寫
對唯讀 data segment 做寫入操作              → W bit = 0，不允許寫入
把 data segment 拿來執行                    → 只有 code segment 可以執行
```

以上任何一項都會讓 CPU 觸發 General Protection Fault。

### 5.2 Limit Checking（範圍檢查）

CPU 會確保你的 offset 沒有超出 segment 的大小。

**一般 segment（Expand-Up，向上擴展）：**

```
有效 offset 範圍：0 到 Limit

例如 Limit = 0xFFFF：
  mov al, [ds:0x0000]   → OK
  mov al, [ds:0xFFFF]   → OK（offset = limit，最後一個 byte）
  mov al, [ds:0x10000]  → General Protection Fault！超出 limit
```

精確規則（因為不同大小的存取會跨越多個 byte）：

- byte 存取：offset > limit → fault
- word（2 bytes）存取：offset >= limit → fault
- dword（4 bytes）存取：offset >= (limit - 2) → fault

**Expand-Down segment（向下擴展，用於 stack）：**

Stack 是從高位址往低位址長的，所以 expand-down segment 的有效範圍剛好相反：

```
有效 offset 範圍：(Limit + 1) 到 上限

上限取決於 B bit：
  B = 0 → 上限是 0xFFFF（64KB）
  B = 1 → 上限是 0xFFFFFFFF（4GB）
```

```
例如 Expand-Down，B=0，Limit = 0x7FFF：
  有效範圍 = 0x8000 ~ 0xFFFF

  mov al, [ss:0x8000]   → OK
  mov al, [ss:0xFFFF]   → OK
  mov al, [ss:0x7FFF]   → 超出（在 limit 以下）
  mov al, [ss:0x0000]   → 超出
```

為什麼 expand-down 要這樣設計？因為 stack 需要動態成長。當你需要更大的 stack 時，只要減小 limit 的值，有效範圍就自動變大，不需要搬移 stack 裡已有的資料，也不需要更新 stack 裡的 pointer。

**各種 E、G、B 組合的完整表格：**

| Expand | G | B | 有效下界 | 有效上界 | 最大 size | 最小 size |
|--------|---|---|---------|---------|-----------|-----------|
| Up | 0 | X | 0 | Limit | 64KB | 0 |
| Up | 1 | X | 0 | Limit×4096+4095 | 4GB-4KB | 4KB |
| Down | 0 | 0 | Limit+1 | 0xFFFF | 64KB | 0 |
| Down | 1 | 1 | Limit×4096+4096 | 0xFFFFFFFF | 4GB | 4KB |

### 5.3 Privilege Checking — 存取 Data

當你想把一個 selector 載入 DS、ES、FS、GS（存取資料用的 segment register）時，CPU 做以下檢查：

```
規則：DPL >= MAX(CPL, RPL)

白話：目標 segment 的權限等級（DPL）必須 >= 你的權限等級和請求者權限等級中較大的那個
（記住：數字越大 = 權限越小）
```

舉例：

```
情況 1：CPL=0, RPL=0, 目標 DPL=0
  MAX(0,0) = 0,  DPL(0) >= 0  → Kernel 存取 Kernel 資料 ✓

情況 2：CPL=0, RPL=0, 目標 DPL=3
  MAX(0,0) = 0,  DPL(3) >= 0  → Kernel 存取 User 資料 ✓

情況 3：CPL=3, RPL=3, 目標 DPL=0
  MAX(3,3) = 3,  DPL(0) >= 3? → User 不能存取 Kernel 資料 ✗

情況 4：CPL=0, RPL=3, 目標 DPL=0
  MAX(0,3) = 3,  DPL(0) >= 3? → 雖然是 Kernel 在跑，
                                  但 RPL=3 表示 pointer 來自 User，
                                  所以 CPU 拒絕（防冒充機制）✗
```

不同 CPL 能看到的資料範圍：

```
CPL = 0 → 可存取 DPL = 0, 1, 2, 3 的資料（全部）
CPL = 1 → 可存取 DPL = 1, 2, 3 的資料
CPL = 2 → 可存取 DPL = 2, 3 的資料
CPL = 3 → 可存取 DPL = 3 的資料（只有自己的）
```

### 5.4 讀取 Code Segment 裡的資料

有時候 code segment 裡會放常數（例如查找表），有三種方法可以讀取：

**方法 1：把 code segment 的 selector 載入 data segment register**

```asm
mov ds, code_selector    ; 把 code segment 當作 data 來讀
mov al, [ds:0x100]       ; 讀 code segment 裡 offset 0x100 的值
```

前提：code segment 必須是可讀的（R bit = 1），而且非 conforming 的 code segment 適用標準的 data segment 權限檢查規則。

**方法 2：Conforming code segment**

如果 code segment 是 conforming 的，任何權限等級都可以把它的 selector 載入 data segment register 來讀（不做 DPL 檢查）。

**方法 3：用 CS override prefix**

```asm
mov al, [cs:0x100]    ; 用 CS: 前綴直接讀當前 code segment 的資料
```

這永遠合法，因為你已經在執行這個 code segment 了，它的 DPL 一定等於你的 CPL。

### 5.5 Control Transfer — JMP 和 CALL 的權限規則

**Near transfer（段內跳轉）：**

```asm
jmp short label    ; 在同一個 code segment 裡跳
call near_func     ; 在同一個 code segment 裡呼叫
```

因為沒有離開當前 segment，所以只做 limit 檢查（確保目標 offset 沒超出 segment 大小），不做權限檢查。而且 limit 已經 cache 在 CS register 裡了，所以不需要額外的時鐘週期。

**Far transfer — 直接跳到另一個 code segment：**

```asm
jmp 0x08:some_offset     ; 跳到另一個 code segment
call far_ptr             ; 呼叫另一個 segment 的函數
```

規則：

```
非 conforming code segment：
  目標的 DPL 必須 = CPL（只能跳到同權限的 segment）

Conforming code segment：
  目標的 DPL 必須 <= CPL（可以跳到權限 >= 自己的 segment）
  但 CPL 不會改變！你借用了高權限的程式碼，但還是以自己的權限在跑。
```

重要：直接跳轉不能提升權限。如果你是 Ring 3，你不能直接 JMP 或 CALL 到 Ring 0 的非 conforming segment。要提升權限，必須透過 Gate。

### 5.6 Conforming Segment 到底是什麼

Conforming segment 是一種特殊的 code segment，允許低權限的程式呼叫它，但不會改變呼叫者的權限等級。

```
場景：一個數學函式庫（sin、cos、sqrt）放在 Ring 0 的 conforming segment

Ring 3 的程式呼叫 sin() → 允許
  但 CPL 還是 3（不會變成 0）
  sin() 裡面如果嘗試做 Ring 0 的事 → 還是被拒絕

這跟 Call Gate 的差別：
  Call Gate 呼叫 → CPL 真的變成 0（權限提升了）
  Conforming 呼叫 → CPL 維持 3（只是借用程式碼，沒提升權限）
```

適用場景：exception handler、共用的數學函式庫等——程式碼本身不需要高權限，但需要被各種權限等級的程式呼叫。

---

## 6. Gate Descriptor（閘門描述符）

### 6.1 為什麼需要 Gate

直接 JMP/CALL 不能提升權限。但 Ring 3 的程式總是需要呼叫 OS 的功能（讀檔案、網路、分配記憶體...），怎麼辦？

答案：透過 Call Gate。它就像一個受控的入口，讓低權限程式可以安全地呼叫高權限程式碼。

```
沒有 Gate：
  Ring 3 → jmp 到 Ring 0 的任意位址 → CPU 拒絕

有 Gate：
  Ring 3 → call gate → 只能跳到 Gate 裡指定的入口點 → 安全

類比：
  你不能自己走進銀行金庫
  但你可以透過「服務窗口」（Gate）請行員幫你操作
  而且只能做窗口允許的事
```

### 6.2 Gate 的四種類型

| 類型 | 說明 |
|------|------|
| Call Gate | 程式碼間的權限轉移 |
| Trap Gate | 同步例外處理（本篇不展開） |
| Interrupt Gate | 中斷處理（本篇不展開） |
| Task Gate | 任務切換（本篇不展開） |

本節只討論 Call Gate。

### 6.3 Call Gate 的結構

Call Gate 也是一個 descriptor（8 bytes），但格式跟 segment descriptor 不同：

```
一個 Call Gate Descriptor（8 bytes）：

┌────────────────────────────────────────────┐
│ Offset 31..16    （目標函數的 offset 高 16 位）│
│ P | DPL | Type   （Present、權限、類型）      │
│ Dword Count      （要複製幾個參數）            │
│ Selector         （目標 code segment 的 selector）│
│ Offset 15..0     （目標函數的 offset 低 16 位）│
└────────────────────────────────────────────┘
```

| 欄位 | 說明 |
|------|------|
| **Selector** | 指向目標 code segment 的 selector（例如 Ring 0 的 kernel code segment） |
| **Offset** | 目標函數在該 segment 裡的精確入口點 |
| **DPL** | 誰有資格使用這個 Gate（例如 DPL=3 → Ring 3 可以用） |
| **Dword Count** | 跨權限呼叫時，要從舊 stack 複製幾個 dword 參數到新 stack（0~31） |

Gate 就像一個安全的函數指標：它同時指定「目標在哪」和「誰能用」。

重要：你的 CALL 指令裡寫的 offset 會被 CPU 忽略，CPU 只用 Gate 裡寫的 offset。所以你不能跳到 kernel 函數的中間去執行。

### 6.4 使用 Call Gate 時的四重權限檢查

透過 Call Gate 做 CALL 時，CPU 會同時檢查四個權限值：

```
1. CPL        — 你現在的權限等級
2. RPL        — selector 裡的 RPL
3. Gate DPL   — Gate 本身的 DPL（「誰能用這個 Gate」）
4. Target DPL — 目標 code segment 的 DPL（「目標的權限等級」）

CALL 的規則：
  MAX(CPL, RPL) <= Gate DPL     （你有資格使用這個 Gate 嗎？）
  AND
  Target DPL <= CPL              （目標權限必須 >= 你的權限，即只能提升不能降低）
```

舉例：

```
場景：Ring 3 的程式透過 Call Gate 呼叫 Ring 0 的 kernel 函數

CPL = 3, RPL = 3, Gate DPL = 3, Target DPL = 0

檢查 1：MAX(3, 3) = 3 <= 3（Gate DPL）→ 你可以用這個 Gate ✓
檢查 2：0（Target DPL）<= 3（CPL）    → 目標權限比你高，合理 ✓
→ 允許，CPL 變成 0
```

```
場景：Ring 3 的程式想用一個只給 Ring 1 用的 Gate

CPL = 3, Gate DPL = 1

檢查 1：MAX(3, ?) <= 1 → 3 > 1，你沒資格用這個 Gate ✗
→ General Protection Fault
```

**JMP 透過 Gate 的規則不同：**

JMP 不能提升權限。透過 Gate 做 JMP 只能跳到 DPL = CPL 的 segment，或者 conforming segment。想提升權限必須用 CALL。

```
CALL 透過 Gate → 可以提升權限（CPL 變小）
JMP 透過 Gate  → 不能提升權限（目標 DPL 必須 = CPL，或 conforming）
```

---

## 7. Stack Switching（堆疊切換）

### 7.1 為什麼要切換 Stack

當 Ring 3 透過 Call Gate 進入 Ring 0 時，CPU 會自動切換到一個新的 stack。

為什麼？因為 Ring 3 的 stack 是使用者控制的。如果 kernel 繼續用 Ring 3 的 stack：

```
攻擊場景 1：
  惡意程式把自己的 stack pointer 指到一個奇怪的位址
  呼叫 system call 進入 kernel
  Kernel 做 push → 寫到惡意程式指定的位址 → 記憶體被覆蓋！

攻擊場景 2：
  惡意程式把 stack 弄到只剩 2 bytes
  Kernel 一 push 就 stack overflow → 當機或被利用
```

所以每個權限等級都需要自己的、被信任的 stack。

### 7.2 TSS（Task State Segment）

每個權限等級的 stack pointer 存在 TSS（Task State Segment）裡：

```
TSS 裡相關的部分：

┌──────────────┐
│ SS0 : ESP0   │  ← Ring 0 的 stack（SS 和 ESP）
│ SS1 : ESP1   │  ← Ring 1 的 stack
│ SS2 : ESP2   │  ← Ring 2 的 stack
│ ...其他欄位   │
└──────────────┘

注意：沒有 SS3:ESP3
因為 Ring 3 是最低權限，不會有「從更低權限呼叫 Ring 3」的情況。
Ring 3 的 stack 就是程式自己的 stack。
```

每個 task（process）有自己的 TSS，所以每個 process 有自己的 Ring 0 stack。TSS 由 OS 在建立 task 時設定好。這些初始 stack pointer 是唯讀的——CPU 只讀取，不會在執行中修改它們。

### 7.3 完整的 Interlevel CALL 過程

當 Ring 3 透過 Call Gate 呼叫 Ring 0 的函數時，CPU 自動執行以下步驟：

```
Step 1: 確定新的權限等級
  新 CPL = 目標 code segment 的 DPL = 0

Step 2: 從 TSS 載入新的 stack
  新 SS = SS0（TSS 裡的）
  新 ESP = ESP0（TSS 裡的）
  CPU 會檢查新 stack segment 的 DPL 是否 = 新 CPL
  如果不是 → Stack Fault

Step 3: 驗證新 stack 有足夠空間
  需要空間 = 舊 SS:ESP (8 bytes) + 參數 (N×4 bytes) + 返回 CS:EIP (8 bytes)
  如果不夠 → Stack Fault (error code 0)

Step 4: 把舊的 SS:ESP 壓到新 stack
  ┌────────────┐
  │  舊 SS     │  push（保存，以便 RET 時切回）
  │  舊 ESP    │  push
  └────────────┘

Step 5: 複製參數
  把 Call Gate 的 Dword Count 指定數量的參數，從舊 stack 複製到新 stack
  ┌────────────┐
  │  舊 SS     │
  │  舊 ESP    │
  │  參數 1    │  ← 從舊 stack 複製來的
  │  參數 2    │
  │  ...       │
  └────────────┘

Step 6: 壓入返回位址
  ┌────────────┐
  │  舊 SS     │
  │  舊 ESP    │
  │  參數 1    │
  │  參數 2    │
  │  舊 CS     │  ← 返回位址
  │  舊 EIP    │  ← CALL 的下一條指令
  └────────────┘ ← 最終的 ESP

Step 7: 載入新的 CS:EIP
  CS = Gate 裡指定的 selector
  EIP = Gate 裡指定的 offset
  CPL = 0（切換到 Ring 0 了！）
```

如果參數超過 31 個 dword 怎麼辦？Call Gate 的 count 欄位只有 5 bits，最多 31 個 dword。如果需要更多參數，被呼叫的函數可以透過新 stack 上保存的「舊 SS:ESP」去舊 stack 上讀取剩餘的參數。

重要：CPU 不驗證參數的值。它只是盲目複製。被呼叫的 kernel 函數必須自己檢查每個參數是否合法。

### 7.4 Interlevel RET（返回）

當 Ring 0 的函數執行 `ret` 要回到 Ring 3 時：

```
Step 1: 從 stack 彈出 CS:EIP
  CPU 看到 CS 的 RPL = 3 > 當前 CPL = 0 → 這是一個 interlevel return

Step 2: 大量的權限檢查（見下表）

Step 3: 從 stack 彈出 SS:ESP，恢復到 Ring 3 的 stack

Step 4: 檢查 DS、ES、FS、GS
  如果這些 register 裡的 selector 指向 DPL < 新 CPL 的 segment
  （也就是指向比你新權限更高的 segment）
  → 自動清成 NULL selector

  為什麼？因為你已經降回 Ring 3 了，如果還留著 Ring 0 的 data selector，
  Ring 3 的程式就能用它存取 Ring 0 的資料 → 安全漏洞。
  CPU 主動幫你清掉，避免洩漏。
```

RET 只能往低權限返回（數字變大），不能往高權限返回。你不能透過 RET 來提升權限。

**Interlevel RET 的完整檢查清單：**

| 檢查內容 | 失敗時觸發 |
|---------|-----------|
| ESP 在當前 SS 範圍內 | Stack Fault |
| ESP+7 在當前 SS 範圍內 | Stack Fault |
| 返回 CS 的 RPL > 當前 CPL | GP Fault |
| 返回 CS selector 不是 null | GP Fault |
| 返回 CS 在 descriptor table 範圍內 | GP Fault |
| 返回 CS descriptor 是 code segment | GP Fault |
| 返回 CS segment present = 1 | Not Present |
| 非 conforming：DPL = RPL；conforming：DPL <= RPL | GP Fault |
| ESP+N+15 在 SS 範圍內（N = ret 指令的 immediate） | Stack Fault |
| 恢復的 SS selector 不是 null | GP Fault |
| 恢復的 SS 在 descriptor table 範圍內 | GP Fault |
| 恢復的 SS 是可寫的 data segment | GP Fault |
| 恢復的 SS segment present = 1 | Stack Fault |
| 恢復的 SS DPL = 返回 CS 的 RPL | GP Fault |
| 恢復的 SS selector RPL = SS DPL | GP Fault |

---

## 8. Instruction Restriction（指令限制）

### 8.1 Privileged Instructions（特權指令）

以下指令只有 CPL = 0 時才能執行，否則 CPU 觸發 General Protection Fault：

| 指令 | 用途 |
|------|------|
| `LGDT` | 載入 GDT 暫存器（設定 GDT 的位址和大小） |
| `LIDT` | 載入 IDT 暫存器（設定中斷向量表） |
| `LLDT` | 載入 LDT 暫存器 |
| `LTR` | 載入 Task Register |
| `LMSW` | 載入 Machine Status Word |
| `MOV CRn` | 讀寫控制暫存器（CR0, CR2, CR3, CR4） |
| `MOV DRn` | 讀寫 Debug 暫存器 |
| `MOV TRn` | 讀寫 Test 暫存器 |
| `CLTS` | 清除 Task-Switched Flag |
| `HLT` | 停止 CPU 執行 |

為什麼限制？因為這些指令可以改變整個系統的行為。如果 Ring 3 的程式能執行 `lgdt`，它就能替換 GDT，讓自己的 segment 變成 Ring 0 權限，整個保護機制就瓦解了。

### 8.2 Sensitive Instructions（敏感指令）

還有另一類指令（I/O 相關的，如 `in`、`out`、`cli`、`sti`），不是只限 Ring 0，而是透過 IOPL（I/O Privilege Level）來控制。EFLAGS 裡有一個 2-bit 的 IOPL 欄位，只有 CPL <= IOPL 時才能執行這些指令。

---

## 9. Pointer Validation Instructions（指標驗證指令）

這些指令讓 OS 可以在執行前先檢查 pointer 是否合法，避免觸發 fault。

### 9.1 LAR（Load Access Rights）

```asm
lar eax, selector    ; 把 selector 指向的 descriptor 的 access rights 載入 eax
```

檢查你有沒有權限看這個 descriptor。如果有，把 descriptor 的 access rights byte 載入目標暫存器，並設 ZF=1。如果沒權限，ZF=0，暫存器不改。

檢查規則：MAX(CPL, RPL) <= DPL（跟一般的資料存取檢查一樣）。Conforming code segment 例外：任何權限都能查。

所有有效的 descriptor 類型都支援 LAR 檢查。

### 9.2 LSL（Load Segment Limit）

```asm
lsl eax, selector    ; 把 selector 指向的 descriptor 的 limit 載入 eax
```

載入 segment 的 limit（已經根據 G bit 換算成 byte 為單位的 32-bit 值）。ZF 的行為跟 LAR 一樣。

只能用在 segment 類型的 descriptor：

| Type | 能用 LSL？ |
|------|-----------|
| Code/Data segment | YES |
| Available 286/386 TSS | YES |
| Busy 286/386 TSS | YES |
| LDT | YES |
| Call Gate | NO |
| Task Gate | NO |
| Interrupt/Trap Gate | NO |

### 9.3 VERR / VERW（Verify for Reading / Writing）

```asm
verr selector    ; 如果這個 segment 在當前權限下可讀，ZF = 1
verw selector    ; 如果這個 segment 在當前權限下可寫，ZF = 1
```

不會觸發任何 fault。只是設 ZF 告訴你結果。

**VERR 的檢查：**
1. selector 在 GDT/LDT 範圍內
2. 指向 code 或 data segment（不是 system descriptor）
3. segment 是可讀的
4. 權限檢查通過（DPL >= MAX(CPL, RPL)，conforming 除外）

**VERW 的檢查：**
1. 同上 1、2
2. segment 是可寫的 data segment
3. 權限檢查通過

Code segment 永遠不可寫，所以 VERW 對 code segment 一定回 ZF=0。

### 9.4 ARPL（Adjust RPL）

```asm
arpl selector_reg, ax    ; 如果 selector_reg 的 RPL < ax 的 RPL，
                         ; 就把 selector_reg 的 RPL 調高到 ax 的 RPL
                         ; 調整了 → ZF=1；沒調整 → ZF=0
```

這是 RPL 防冒充機制的實作工具。OS 收到使用者傳來的 selector 後：

```asm
; OS 從 stack 取得呼叫者的 CS（知道呼叫者的 CPL）
; 然後對使用者傳來的 selector 執行 ARPL
arpl user_selector, caller_cs

; 效果：確保 user_selector 的 RPL >= 呼叫者的 CPL
; 如果使用者給了一個 RPL=0 的 selector（想冒充 kernel），
; ARPL 會把它調高成 RPL=3（呼叫者真正的權限）
; 這樣之後用這個 selector 時，CPU 會用 RPL=3 來做權限檢查
```

---

## 10. 完整的大圖

```
程式要存取記憶體
    │
    ▼
segment register 裡有 selector
    │
    ▼
CPU 查看 selector 的 Index → 去 GDT/LDT 找到 descriptor
    │
    ▼
┌─────────────────────────────────────────────┐
│ 檢查 1: Type Check                          │
│   segment 類型對嗎？                          │
│   （不能把 data 載入 CS，不能寫 code segment） │
│                                              │
│ 檢查 2: Privilege Check                      │
│   你有權限嗎？                                │
│   DPL >= MAX(CPL, RPL)？                     │
│                                              │
│ 檢查 3: Limit Check                          │
│   offset 在有效範圍內嗎？                      │
│   不能超過 Limit                              │
└─────────────────────────────────────────────┘
    │                    │
    ▼                    ▼
  全部通過             任一失敗
    │                    │
    ▼                    ▼
  正常存取            CPU 觸發 Exception
  Base + Offset       （GP Fault、Stack Fault、
  = 實體位址            或 Not Present Fault）
```

這就是 80386 Segment-Level Protection 的完整機制。每一次記憶體存取、每一次 JMP/CALL/RET，CPU 都在背後做這些檢查。全部由硬體自動完成，不需要一行軟體程式碼。
