.arm

.equ I_Bit,   0x80
.equ F_Bit,   0x40
.equ T_Bit,   0x20

;@FIQ register definition
.equ FIQ,     0x78000008    ;@ FIQ register
.equ FIQRAW,  0x7800000C    ;@ FIQRAW register
.equ FIQEN,   0x78000010    ;@ FIQEN register

;@**************************************************************************************************
;@*      SWI Handler                                                                               *
;@**************************************************************************************************
.global swi_handler

swi_handler:
        STMFD   sp!, {r1-r12,lr}        ;@ save registers
        MRS     r1, spsr                ;@ move SPSR into general purpose register
        TST     r1, #T_Bit              ;@ check ARM or Thumb
        LDRNEH  r1, [lr,#-2]            ;@ get instruction code(Thumb)
        BICNE   r1, r1, #0xffffff00     ;@ decode SWI number(Thumb)
        LDREQ   r1, [lr,#-4]            ;@ get instruction code(ARM)
        BICEQ   r1, r1, #0xff000000     ;@ decode SWI number(ARM)
        BIC     r2, r1, #0x0000000f     ;@ decode SWI_Jump_Table number
        AND     r1, r1, #0x0000000f
        ADR     r3, SWI_Jump_Table_Table;@ get address of jump table table
        LDR     r4, [r3,r2,LSR #2]      ;@ get address of jump table
        LDR     pc, [r4,r1,LSL #2]      ;@ refer to jump table and branch.

SWI_Jump_Table_Table:
        .word     SWI_Jump_Table
        .word     SWI_PM_Jump_Table

SWI_Jump_Table:  ;@ normal SWI jump table
        .word     SWI_irq_en
        .word     SWI_irq_dis
        .word     SWI_fiq_en
        .word     SWI_fiq_dis

SWI_PM_Jump_Table:       ;@ SWI jump table for power management
        .word     SWI_pm_if_recov
        .word     SWI_pm_if_dis
        .word     SWI_pm_wfi

SWI_irq_en:                   ;@ enable IRQ
        MRS     r0, SPSR        ;@ get SPSR
        BIC     r1, r0, #I_Bit  ;@ I_Bit clear
        AND     r0, r0, #I_Bit  ;@ set the return value
        MSR     SPSR_c, r1      ;@ set SPSR
        B       EndofSWI

SWI_irq_dis:                  ;@ disable IRQ
        MRS     r0, SPSR        ;@ get SPSR
        ORR     r1, r0, #I_Bit  ;@ I_Bit set
        AND     r0, r0, #I_Bit  ;@ set the return value
        MSR     SPSR_c, r1      ;@ set SPSR
        B       EndofSWI

SWI_fiq_en:      ;@ enable FIQ
        MRS     r0, SPSR        ;@ load SPSR
        BIC     r3, r0, #F_Bit  ;@ F_Bit clear
        AND     r0, r0, #F_Bit  ;@ return value
        MSR     SPSR_c, r3      ;@ set SPSR
        LDR     r0, =FIQEN      ;@ load FIQEN register
        MOV     r1, #0x1
        STRH    r1, [r0]        ;@ enable FIQ
        B       EndofSWI

SWI_fiq_dis:     ;@ disable FIQ
        MRS     r0, SPSR        ;@ load SPSR
        ORR     r3, r0, #F_Bit  ;@ F_Bit Set
        AND     r0, r0, #F_Bit  ;@ return value
        MSR     SPSR_c, r3      ;@ set SPSR
        B       EndofSWI

SWI_pm_if_recov:         ;@ recover CPSR
        MSR     SPSR_c, r0      ;@ Set SPSR
        B       EndofSWI

SWI_pm_if_dis:           ;@ mask IRQ and FIQ. and return pre-masked CPSR
        MRS     r0, SPSR        ;@ Get SPSR & Return value
        ORR     r3, r0, #I_Bit|F_Bit
        MSR     SPSR_c, r3      ;@ Set SPSR
        B       EndofSWI

SWI_pm_wfi:
        MOV     r0, #0
        MCR     p15, 0, r0, c7, c0, 4;@ wait for interrupt
        B       EndofSWI

EndofSWI:
        LDMFD   sp!, {r1-r12,pc}^       ;@ restore registers

.end
