OUTPUT_FORMAT("elf32-sh-linux")
OUTPUT_ARCH(sh)
ENTRY(xdj700_lossless_gate_dispatch)

SECTIONS
{
  /* Independently audited XDJ-700 v1.15 decoded-application cave.  The
     original 512 bytes and adjacent clean 256 bytes form one 768-byte gate. */
  . = 0x080FB800;
  __xdj700_lossless_gate_start = .;
  .text : ALIGN(4)
  {
    KEEP(*(.text.xdj700_lossless_gate))
  }
  __xdj700_lossless_gate_code_end = .;

  . = 0x080FBAEC;
  .xdj700_lossless_gate_state : ALIGN(4)
  {
    KEEP(*(.xdj700_lossless_gate_state))
  }

  __xdj700_lossless_gate_end = .;

  .bss (NOLOAD) : ALIGN(4)
  {
    __xdj700_lossless_gate_bss_start = .;
    *(.bss .bss.*)
    *(COMMON)
    __xdj700_lossless_gate_bss_end = .;
  }

  ASSERT(__xdj700_lossless_gate_code_end <= 0x080FBAE0,
         "XDJ-700 lossless gate code shares the mutable state cache line")
  ASSERT(__xdj700_lossless_gate_end == 0x080FBB00,
         "XDJ-700 lossless gate must fill the 768-byte audited cave")

  /* A second independently audited 128-byte span holds the shared reader and
     exact-IDLE validation utility.  The ambiguous BB00..BB80 gap is skipped. */
  . = 0x080FBB80;
  __xdj700_lossless_gate_satellite_start = .;
  .xdj700_lossless_gate_satellite : ALIGN(4)
  {
    KEEP(*(.xdj700_lossless_gate_satellite))
  }
  __xdj700_lossless_gate_satellite_code_end = .;
  . = 0x080FBC00;
  __xdj700_lossless_gate_satellite_end = .;

  ASSERT(__xdj700_lossless_gate_satellite_code_end <= 0x080FBC00,
         "XDJ-700 lossless gate satellite exceeds its audited span")
  ASSERT(__xdj700_lossless_gate_satellite_end == 0x080FBC00,
         "XDJ-700 lossless gate satellite span changed")

  /* Direct-playback builds use the first 128 bytes of the independently
     audited erased completion cave for completed lossless cache retirement.
     Diagnostic builds leave this section empty because their completion
     ingress already owns the same cave. */
  . = 0x080FC100;
  __xdj700_lossless_cache_gate_start = .;
  .xdj700_lossless_cache_gate : ALIGN(4)
  {
    KEEP(*(.xdj700_lossless_cache_gate))
  }
  __xdj700_lossless_cache_gate_code_end = .;

  ASSERT(__xdj700_lossless_cache_gate_code_end <= 0x080FC180,
         "XDJ-700 lossless cache gate exceeds its audited span")
  ASSERT(__xdj700_lossless_gate_bss_end == __xdj700_lossless_gate_bss_start,
         "XDJ-700 lossless tail gate must not use BSS")

  /DISCARD/ : { *(.comment .note .note.*) }
}
