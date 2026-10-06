SECTIONS {
  . = TEXT_BASE;
  .text : { *(.text .text.* .rodata .rodata.* .literal .literal.* .init .fini) }
  .dynro : { *(.dynsym .dynstr .hash .gnu.hash .rela.dyn .rela.plt .rel.dyn .rel.plt .init_array .fini_array) }
  . = ARENA_BASE;
  .rw : { *(.dynamic) *(.data.rel.ro .data.rel.ro.* .got .got.* .got.plt .data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON) }
}
