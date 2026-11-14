/* BIOS stage 2, linked at 0x8000. Kernel is loaded at physical 0x10000. */
#ifndef KERNEL_SECTORS
#error KERNEL_SECTORS must be supplied by tools/build.py
#endif
.code16
.section .text
.global _start
_start:
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7c00, %sp
    cld
    movb %dl, boot_drive

    movw $kernel_packet, %si
    movb $0x42, %ah
    int $0x13
    jc disk_error

    /* VBE 0x115: 800x600 true color, with a linear framebuffer. */
    xorw %ax, %ax
    movw %ax, %es
    movw $0x5200, %di
    movw $0x4f01, %ax
    movw $0x115, %cx
    int $0x10
    cmpw $0x004f, %ax
    jne video_error
    movw 0x5200, %ax
    andw $0x0091, %ax
    cmpw $0x0091, %ax
    jne video_error
    cmpw $800, 0x5212
    jne video_error
    cmpw $600, 0x5214
    jne video_error
    cmpb $6, 0x521b
    jne video_error
    cmpb $8, 0x521f
    jne video_error
    cmpb $8, 0x5221
    jne video_error
    cmpb $8, 0x5223
    jne video_error
    movb 0x5219, %al
    cmpb $24, %al
    je mode_valid
    cmpb $32, %al
    jne video_error
mode_valid:
    /* BootInfo layout is shared with kernel/hardware.hpp. */
    movl $0x58503936, 0x5000
    movl 0x5228, %eax
    movl %eax, 0x5004
    movzwl 0x5210, %eax
    movl %eax, 0x5008
    movzwl 0x5212, %eax
    movl %eax, 0x500c
    movzwl 0x5214, %eax
    movl %eax, 0x5010
    movzbl 0x5219, %eax
    movl %eax, 0x5014
    movzbl 0x5220, %eax
    movl %eax, 0x5018
    movzbl 0x5222, %eax
    movl %eax, 0x501c
    movzbl 0x5224, %eax
    movl %eax, 0x5020

    movw $0x4f02, %ax
    movw $0x4115, %bx
    int $0x10
    cmpw $0x004f, %ax
    jne video_error

    /* Enable A20, install flat segments, and enter protected mode. */
    cli
    inb $0x92, %al
    orb $0x02, %al
    andb $0xfe, %al
    outb %al, $0x92
    lgdt gdt_pointer
    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0
    ljmpl $0x08, $protected_entry

disk_error:
    movw $disk_message, %si
    jmp print_error
video_error:
    movw $video_message, %si
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

.balign 8
gdt:
    .quad 0x0000000000000000
    .quad 0x00cf9a000000ffff
    .quad 0x00cf92000000ffff
gdt_end:
gdt_pointer:
    .word gdt_end - gdt - 1
    .long gdt
.balign 4
kernel_packet:
    .byte 0x10, 0
    .word KERNEL_SECTORS
    .word 0x0000, 0x1000
    .quad 9
boot_drive: .byte 0
disk_message: .asciz "XP96: kernel read failed."
video_message: .asciz "XP96: 800x600 VBE true color is required. Use QEMU -vga std."

.code32
protected_entry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs
    movl $0x90000, %esp
    cld
    movl $0x10000, %eax
    jmp *%eax
.section .note.GNU-stack,"",@progbits
