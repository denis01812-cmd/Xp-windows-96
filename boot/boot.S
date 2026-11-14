/* BIOS stage 1: load the 4 KiB second stage from the IDE disk. */
.code16
.section .text
.global _start
_start:
    ljmp $0x0000, $start
start:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7c00, %sp
    cld
    sti
    movb %dl, boot_drive

    /* Require BIOS EDD, rather than silently assuming CHS geometry. */
    movw $0x55aa, %bx
    movb $0x41, %ah
    int $0x13
    jc disk_error
    cmpw $0xaa55, %bx
    jne disk_error
    testw $1, %cx
    jz disk_error

    movb boot_drive, %dl
    movw $disk_packet, %si
    movb $0x42, %ah
    int $0x13
    jc disk_error
    movb boot_drive, %dl
    ljmp $0x0000, $0x8000

disk_error:
    movw $error_text, %si
print_error:
    lodsb
    testb %al, %al
    jz halt
    movb $0x0e, %ah
    xorw %bx, %bx
    int $0x10
    jmp print_error
halt:
    cli
    hlt
    jmp halt

.balign 4
disk_packet:
    .byte 0x10, 0
    .word 8
    .word 0x8000, 0x0000
    .quad 1
boot_drive: .byte 0
error_text: .asciz "XP96: IDE / BIOS EDD boot required."
.org 510
.word 0xaa55
.section .note.GNU-stack,"",@progbits
