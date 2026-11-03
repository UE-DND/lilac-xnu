SECTIONS
{
    . = 0x80080000;
    .text : {
        *(.text.head)
        *(.text*)
        *(.rodata*)
        *(.data*)
        *(.bss*)
    }
    . = ALIGN(4096);
    .payload : {
        _payload_start = .;
    }
}
