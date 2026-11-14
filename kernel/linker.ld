OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

PHDRS
{
    code PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x10000;
    .text : { KEEP(*(.entry)) *(.text*) } :code
    .rodata ALIGN(16) : { *(.rodata*) } :code
    .data ALIGN(4096) : { *(.data*) } :data
    __kernel_file_end = .;

    /* Large software framebuffers live above the BIOS / VGA memory hole. */
    .bss 0x100000 (NOLOAD) :
    {
        __bss_start = .;
        *(.bss*) *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    } :data
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}

ASSERT(__kernel_file_end <= 0x1fe00, "Kernel exceeds the 127-sector BIOS read budget")
ASSERT(__bss_end < 0x1000000, "Kernel requires too much memory for this bootstrap")
