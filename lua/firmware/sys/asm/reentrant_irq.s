.arm

;@ Global definitions
.equ Mode_USR,  0x10
.equ Mode_IRQ,  0x12
.equ Mode_SVC,  0x13
.equ Mode_SYS,  0x1F

.equ I_Bit,     0x80
.equ F_Bit,     0x40

.equ GPCTL,     0xb7000000  ;@ address of GPCTL

.equ IRQSIZE ,  64          ;@ number of IRQ interrupt factor.

.equ IRQ,       0x78000000
.equ FIQ,       0x78000008
.equ IRN,       0x78000014
.equ CILCL,     0x78000028

.global  IRQ_HANDLER_TABLE
.global  FIQ_handler
.global  fiq_handler
.global  irq_handler

;@==============================================================================
;@ IRQ Mode with IRQ disabled
irq_handler:
  sub     LR,       #4

  ;@ Stack some pointers
  stmfd   SP!,      {r1-r6, LR}
  ;@ Get SPSR
  mrs     r4,       SPSR

  ;@ If IRQ interrupts disabled, exit IRQ Handler
  tst     r4,       #I_Bit
  bne     exit_irq_handler

  ;@ Stack SPSR
  stmfd   SP!,      {r4}

  ;@ Get IRQ number from IRN register
  ldr     r5,       =#IRN
  ldr     r6,       [r5]

  ;@ Switch to SYS Mode, with IRQ disabled, keep FIQ status
  tst     r4,       #F_Bit          ;@ Test FIQ Status
  moveq   r3,       #Mode_SYS       ;@ Enabled
  movne   r3,       #Mode_SYS|F_Bit ;@ Disabled
  msr     CPSR_c,   r3              ;@ Switch to SYS Mode

;@==============================================================================
;@ SYS Mode with IRQ enabled

  ;@ Check if IRQN is valid
  cmp     r6,       #IRQSIZE
  bcs     return_irq_handler

 ;@ Stack some registers
  stmfd   SP!,      {r0, r12, LR}

  ;@ Get Address of IRQ Handlers
  ldr     r1,       =IRQ_HANDLER_TABLE
  ldr     r2,       [r1, r6, lsl #2]
  mov     LR,       PC
  bx      r2

  ldmfd   sp!, {r0, r12, lr};@ R0, R12 and link register is restored.
return_irq_handler:

  ;@ Switch to SYS Mode, with IRQ disabled, keep FIQ status
  tst     r4,       #F_Bit                ;@ Test FIQ Status
  moveq   r3,       #Mode_IRQ|I_Bit       ;@ Enabled
  movne   r3,       #Mode_IRQ|I_Bit|F_Bit ;@ Disabled
  msr     CPSR_c,   r3              ;@ Switch to IRQ Mode

;@==============================================================================
;@ IRQ Mode with IRQ disabled
  ;@ Any write to CILCL clears the most significant '1' bit of CIL register
  ldr     r5,       =#CILCL
  str     r2,       [r5]

  ;@ Restore SPSR
  ldmfd   sp!, {r4}
  msr     spsr_cf,  r4       ;@ Restore SPSR of IRQ Mode

exit_irq_handler:
  ;@ Restore inital registers
  ldmfd   sp!, {r1-r6, pc}^

;@==============================================================================
;@ FIQ Mode
fiq_handler:
    sub     LR,     #4 ;@ Return address
    stmfd   sp!,    {LR} ;@ store low registers to FIQ stack

    ldr     r2,     =FIQ_handler
    mov     LR,     PC
    bx      r2

    ldmfd   SP!,    {PC}^  ;@ restore registers & return from FIQ

.weak FIQ_handler
.thumb
.thumb_func
FIQ_handler:
  b FIQ_handler

.end
