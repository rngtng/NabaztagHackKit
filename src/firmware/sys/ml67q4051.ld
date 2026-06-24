/***********************************************************************/
/*  This file is part of the ARM Compiler package                      */
/*  Copyright KEIL ELEKTRONIK GmbH 1992-2007                           */
/***********************************************************************/
/*                                                                     */
/*  TARGET.LD:  Linker Script File                                     */
/*                                                                     */
/***********************************************************************/
MEMORY
{
  IntROM  (rx) : ORIGIN = 0x08000000, LENGTH = 124k /* Actually 128k, but last sector is config data*/
  IntRAM  (rw) : ORIGIN = 0x10000000, LENGTH = 16k
  ExtROM  (rx) : ORIGIN = 0xC8000000, LENGTH = 0k
  ExtRAM  (rx) : ORIGIN = 0xD0000000, LENGTH = 1024k
}

ENTRY(Reset_Handler)

SECTIONS
{
  /* first section is .text which is used for code */
  .text :
  {
    *(.intvec)
    *(.startup.*)
    *(.text.*)                   /* remaining code */
    *(.glue_7t) *(.glue_7)
  } >IntROM
  . = ALIGN(4);

  /* .rodata section which is used for read-only data (constants) */
  .rodata :
  {
    *(.rodata.*)
  } >IntROM
  . = ALIGN(4);

  /* Bytecode */
  .bytecode :
  {
    *(.bytecode.*)
  } >IntROM
  _etext = . ;
  PROVIDE (etext = .);

  /* .data section which is used for initialized data */
  .data : AT (_etext)
  {
    _data = . ;
    *(.data.*)
      . = ALIGN(4);
    *(.ramfunc)
    SORT(CONSTRUCTORS)
  } >IntRAM
  . = ALIGN(4);
  _edata = . ;
   PROVIDE (edata = .);

  /* .bss section which is used for uninitialized data */
  .bss (NOLOAD):
  {
    __bss_start = . ;
    __bss_start__ = . ;
    *(.bss)
    *(COMMON)
    } >IntRAM
  . = ALIGN(4);
  __bss_end__ = . ;

  /* Stack */
  . = ALIGN(4);
  PROVIDE(__stack_start__ = ALIGN(ORIGIN(IntRAM)+LENGTH(IntRAM),8));
  /* Check in 76405x_cstartup for the stack size) */
  PROVIDE(__stack_end__ = ALIGN(__stack_start__ - 0x1680 +8 ,8));

  /* Heap */
  . = ALIGN(4);
  PROVIDE(__heap_start__ = .);
  PROVIDE(__heap_end__ = ALIGN(__stack_end__ - 0x100  ,8));

  _end = .;
  PROVIDE (end = .);

  /* Stabs debugging sections.  */
  .stab          0 : { *(.stab) }
  .stabstr       0 : { *(.stabstr) }
  .stab.excl     0 : { *(.stab.excl) }
  .stab.exclstr  0 : { *(.stab.exclstr) }
  .stab.index    0 : { *(.stab.index) }
  .stab.indexstr 0 : { *(.stab.indexstr) }
  .comment       0 : { *(.comment) }
  /* DWARF debug sections.
     Symbols in the DWARF debugging sections are relative to the beginning
     of the section so we begin them at 0.  */
  /* DWARF 1 */
  .debug          0 : { *(.debug) }
  .line           0 : { *(.line) }
  /* GNU DWARF 1 extensions */
  .debug_srcinfo  0 : { *(.debug_srcinfo) }
  .debug_sfnames  0 : { *(.debug_sfnames) }
  /* DWARF 1.1 and DWARF 2 */
  .debug_aranges  0 : { *(.debug_aranges) }
  .debug_pubnames 0 : { *(.debug_pubnames) }
  /* DWARF 2 */
  .debug_info     0 : { *(.debug_info .gnu.linkonce.wi.*) }
  .debug_abbrev   0 : { *(.debug_abbrev) }
  .debug_line     0 : { *(.debug_line) }
  .debug_frame    0 : { *(.debug_frame) }
  .debug_str      0 : { *(.debug_str) }
  .debug_loc      0 : { *(.debug_loc) }
  .debug_macinfo  0 : { *(.debug_macinfo) }
  /* SGI/MIPS DWARF 2 extensions */
  .debug_weaknames 0 : { *(.debug_weaknames) }
  .debug_funcnames 0 : { *(.debug_funcnames) }
  .debug_typenames 0 : { *(.debug_typenames) }
  .debug_varnames  0 : { *(.debug_varnames) }
}

