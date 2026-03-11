/**
 * Common header file shared between assembly (.S) and C (.c) sources.
 * Using .S (uppercase) allows #include to work because gcc runs the
 * C preprocessor on .S files before passing them to the assembler.
 */
#ifndef OS_H
#define OS_H
#define KERNEL_CODE_SEG 0x8
#define KERNEL_DATA_SEG 0x10
#endif // OS_H
