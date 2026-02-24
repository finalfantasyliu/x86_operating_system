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
    xor     ax, ax
    mov     ds, ax    
    mov     es, ax
    mov     ss, ax
    mov     fs, ax
	mov     gs, ax
	mov     esp, _start 
read_self_all:
    mov bx, _start_32 //載入位置，0x7C00+512=0x7E00
    mov cx, 0x2 //因為第一個sector已經在0x7C00了，所以從0x7E00開始讀取
    mov ax, 0x240 //這邊是ah+al會是 0000 0010 0100 0000 高8位是指讀取哪個儲存介質2是platter，低8位是sector數量
    mov dx, 0x80 //這邊是dx，因為讀取的是第一個硬碟，所以是0x80
    int 0x13
    jc read_self_all
    jmp .


    .org 0x1FE, 0x90
    .byte 0x55, 0xAA
_start_32:
    .fill 64*1024, 1, 0x35
