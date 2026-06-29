;@---------------------------------------------------------------
;@ Macros and definitions
;@---------------------------------------------------------------

;@ Standard definitions of Mode bits and Interrupt (I & F) flags in PSRs
.equ I_Bit,             0x80   ;/* when I bit is set, IRQ is disabled */
.equ F_Bit,             0x40   ;/* when F bit is set, FIQ is disabled */

.equ Mode_USR,          0x10  ;@ User
.equ Mode_FIQ,          0x11  ;@ FIQ Interrupt
.equ Mode_IRQ,          0x12  ;@ IRQ Interrupt
.equ Mode_SVC,          0x13  ;@ Supervisor
.equ Mode_ABT,          0x17  ;@ Abort
.equ Mode_UND,          0x1B  ;@ Undef
.equ Mode_SYS,          0x1F  ;@ System

;@ Stack sizes
.equ UND_Stack_Size,    0x00000080
.equ SVC_Stack_Size,    0x00000080
.equ ABT_Stack_Size,    0x00000080
.equ FIQ_Stack_Size,    0x00000100
.equ IRQ_Stack_Size,    0x00000400
.equ USR_Stack_Size,    0x00001000
;@ total 0x1680

;@Clock control register
.equ CLKCNT,            0xb7000010

;@System timer setting
.equ TMEN,              0xb8001004
.equ TMOVF,             0xb8001010
.equ TMRLR,             0xb8001008
.equ TMRCYC,            10
.equ RINGOSC,           16
.equ VALUE_OF_TMRLR,    65536 - (TMRCYC * RINGOSC * 1000) /16
.equ CHANGE_CLK_VALUE,  0xfffffcff

;@External memory controller
.equ BWC,               0x78100000
.equ RAMAC,             0x78100008
.equ IO0AC,             0x7810000C
.equ BWC_VALUE,         0xA0
.equ RAMAC_VALUE,       0x2
.equ IO0AC_VALUE,       0x1

;@==============================================================================
.section .intvec
.global  Reset_Handler
.global  undef_handler, swi_handler, prefetch_handler, \
         data_handler, irq_handler, fiq_handler
.arm  ;@ Always ARM mode after reset
.org    0x00
  ldr  pc,=Reset_Handler    ;@ Reset
.org    0x04
  ldr  pc,=undef_handler    ;@ Undef
.org    0x08
  ldr  pc,=swi_handler      ;@ Software Interrupt
.org    0x0c
  ldr  pc,=prefetch_handler ;@
.org    0x10
  ldr  pc,=data_handler     ;@ Data Abort
.org    0x14
  nop                       ;@ Reserved
.org    0x18
  ldr  pc,=irq_handler      ;@ IRQ interrupts
.org    0x1c
  ldr  pc,=fiq_handler      ;@ Fast IRQ interrupts
.org    0x20 ;@ Constant table entries (for ldr pc)
;@==============================================================================
.section .startup.cstartup
.global main
.global __stack_start__

.arm
Reset_Handler:
  ;@ Ring oscillator is srcsel?
  LDR  R0,=CLKCNT
  LDR  R1,[R0]
  AND  R1,R1,#0x300
  CMP  R1,#0x100
  BNE  bypass_tmr_init

  ;@ System timer
  MOV  R0,#0x0
  LDR  R1,=TMEN
  STR  R0,[R1]
  MOV  R0,#0x1
  LDR  R1,=TMOVF
  STR  R0,[R1]
  ;@ Set 10msec cycle
  LDR  R0,=VALUE_OF_TMRLR
  LDR  R1,=TMRLR
  STR  R0,[R1]
  ;@ Start system timer
  MOV  R0,#0x1
  LDR  R1,=TMEN
  STR  R0,[R1]

bypass_tmr_init:
  LDR     R0, =__stack_start__
  ;@ Enter Undefined Instruction Mode and set its Stack Pointer
  MSR     CPSR_c, #Mode_UND|I_Bit|F_Bit
  MOV     SP, R0
  SUB     R0, R0, #UND_Stack_Size

  ;@ Enter Abort Mode and set its Stack Pointer
  MSR     CPSR_c, #Mode_ABT|I_Bit|F_Bit
  MOV     SP, R0
  SUB     R0, R0, #ABT_Stack_Size

  ;@ Enter FIQ Mode and set its Stack Pointer
  MSR     CPSR_c, #Mode_FIQ|I_Bit|F_Bit
  MOV     SP, R0
  SUB     R0, R0, #FIQ_Stack_Size

  ;@ Enter IRQ Mode and set its Stack Pointer
  MSR     CPSR_c, #Mode_IRQ|I_Bit|F_Bit
  MOV     SP, R0
  SUB     R0, R0, #IRQ_Stack_Size

  ;@ Enter Supervisor Mode and set its Stack Pointer
  MSR     CPSR_c, #Mode_SVC|I_Bit|F_Bit
  MOV     SP, R0
  SUB     R0, R0, #SVC_Stack_Size

  ;@ Enter User Mode and set its Stack Pointer
  MSR     CPSR_c, #Mode_USR|I_Bit|F_Bit
  MOV     SP, R0

  ;@ Setup a default Stack Limit (when compiled with "-mapcs-stack-check")
  SUB     SL, SP, #USR_Stack_Size

;@ Ring oscillator is srcsel?
  LDR  R0,=CLKCNT
  LDR  R1,[R0]
  AND  R1,R1,#0x300
  CMP  R1,#0x100
  BNE timer_init_ok

;@ Timer overflow wait
_wait_ovf:
  LDR  R1,=TMOVF
  LDR  R0,[R1]
  CMP  R0,#0x1
  BNE  _wait_ovf

  ;@ Change to main clk from ring oscillator
  LDR  R1,=CLKCNT
  LDR  R0,[R1]
  LDR  R1,=CHANGE_CLK_VALUE
  AND  R0,R0, R1
  LDR  R1,=CLKCNT
  STR  R0,[R1]

timer_init_ok:
  ;@ Init external memory controller
  ;@ set BWC
  LDR  R0,=BWC_VALUE
  LDR  R1,=BWC
  STR  R0,[R1]
  ;@ set RAMAC
  LDR  R0,=RAMAC_VALUE
  LDR  R1,=RAMAC
  STR  R0,[R1]
  ;@ set IO0AC
  LDR  R0,=IO0AC_VALUE
  LDR  R1,=IO0AC
  STR  R0,[R1]

  ;@ Relocate .data section (Copy from ROM to RAM)
  LDR     R1, =_etext
  LDR     R2, =_data
  LDR     R3, =_edata
LoopRel:        CMP     R2, R3
  LDRLO   R0, [R1], #4
  STRLO   R0, [R2], #4
  BLO     LoopRel

  ;@ Clear .bss section (Zero init)
  MOV     R0, #0
  LDR     R1, =__bss_start__
  LDR     R2, =__bss_end__
LoopZI:         CMP     R1, R2
  STRLO   R0, [R1], #4
  BLO     LoopZI

;@ Continue to main
  ldr     lr,return_main
  ldr     r0,=main
  bx      r0
return_main:
  b return_main

;@==============================================================================
undef_handler:
  b undef_handler

prefetch_handler:
  b prefetch_handler

data_handler:
  b data_handler

  .end
