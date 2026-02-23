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
    // jmp 0x7c00:_offset  // 測試用：故意設錯 CS，證明會跳到錯誤位置，這邊就是將cs設為0x7c00，這樣之後如果要執行的話，會是0x7c00*16+_offset，所以你的print_string根本print不出來
    jmp 0:_offset
_offset:
    xor     ax, ax
    mov     ds, ax    // 這邊要把 ds register 設為 0，因為 BIOS 一開始初始化的時候不一定為 0
    mov     es, ax
    mov     ss, ax
    mov     fs, ax
	mov     gs, ax
	mov     esp, _start //這邊壓到0x7c00之下，要注意不要寫到0x0000的地方，因為那邊是BIOS的資料區，寫了可能會有問題，然後esp的位置是往下長是cpu寫死的

	// mov si, offset msg
	// call print_string
	jmp .

// print_string:
// 	push si //這邊要push的原因，為暫存call function前的狀態，不然會到char \0的address
// 	push ax //這個同上
// 	push bx //如果不push，有可能之前是第1頁，但被改動，導致之前的function可能要print screen但print不到
//
// .loop:
// 	lodsb
// 	or al, al
// 	jz .done
//
// 	mov ah, 0x0E //這是teletype output function，會在螢幕上印出 al 的內容，並且光標會往右移動一格
// 	mov bh, 0 //這是active page number，通常是0，如果你設1就寫示不出來了
// 	int 0x10
//
// 	jmp .loop
//
// .done:
// 	pop bx
// 	pop ax
// 	pop si
// 	ret
// msg:
// 	.string "Hello world"

    .org 0x1FE, 0x90
    .byte 0x55, 0xAA
