OUTPUT_FORMAT("elf32-i386")
ENTRY(_start16)
SECTIONS {
  . = 0x10000;
  .entry16 : { KEEP(*(.entry16)) }
  .lowdata : ALIGN(16) { *(.lowdata) }
  . = 0x11000;
  .options : { KEEP(*(.options)) }
  .text : ALIGN(16) { *(.text*) }
  .rodata : ALIGN(16) { *(.rodata*) }
  .data : ALIGN(16) { *(.data*) }
  __bss_start = .;
  .bss (NOLOAD) : ALIGN(16) { *(.bss*) *(COMMON) }
  __bss_end = .;
  ASSERT(__bss_end < 0x70000, "guest overlaps stack")
  /DISCARD/ : { *(.comment*) *(.note*) *(.eh_frame*) }
}
