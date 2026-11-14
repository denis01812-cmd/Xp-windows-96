.code32
.section .entry,"ax"
.global _start
.extern kernel_main
.extern __bss_start
.extern __bss_end
_start:
    cli
    cld
    movl $0x90000, %esp
    xorl %ebp, %ebp
    movl $__bss_start, %edi
    movl $__bss_end, %ecx
    subl %edi, %ecx
    xorl %eax, %eax
    rep stosb
    call kernel_main
1:
    cli
    hlt
    jmp 1b

.section .text
.global fault_stub
.extern kernel_fault
fault_stub:
    cli
    cld
    andl $-16, %esp
    call kernel_fault
    jmp 1b
.global timer_stub
.extern timer_tick
timer_stub:
    cld
    pushal
    movl %esp, %ebp
    andl $-16, %esp
    call timer_tick
    movl %ebp, %esp
    popal
    iret
.section .note.GNU-stack,"",@progbits
