SECTIONS {
  . = 0x080FBB00;
  .text : { *(.text) }
  /DISCARD/ : { *(.comment) *(.note*) }
  ASSERT(. <= 0x080FBB80, "early reply helper exceeds verified erased gap")
}
