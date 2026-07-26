/**
 * @file stubs/ml674061.h
 * @brief Host stand-in for the ML67Q4051 register map (#246, #252).
 *
 * Placed FIRST on the include path for host tests, so it shadows
 * sys/inc/ml674061.h. The real header's peripheral registers are absolute
 * addresses; common.h dereferences them through volatile casts, which
 * segfaults off-target. Here the same names resolve to offsets into an
 * ordinary RAM array, so a driver that only touches registers - hal/i2c.c -
 * runs natively and its control flow can be asserted.
 *
 * This models ADDRESSES, not behaviour: nab_regs is passive, so a transfer
 * driven against it never sees its completion flags set and takes the
 * timeout path. That is exactly the path the wedged-bus and interrupt-masking
 * tests care about. Anything needing a live I2C peer belongs on hardware.
 *
 * Only the registers hal/i2c.c uses are modelled - deliberately. A stub that
 * mirrored the whole 1068-line header would rot silently against it; this one
 * fails to compile the moment a driver reaches for something new.
 */
#ifndef _ML674061_H_
#define _ML674061_H_

#include <stdint.h>

/* The fake peripheral window. Word-typed so every sub-register is naturally
 * aligned for the byte/half/word accessors in common.h. */
extern volatile uint32_t nab_regs[64];
#define NAB_REG_BASE ((uintptr_t)nab_regs)

/* Port mux (init_i2c's pin setup) */
#define PORTSEL1        (NAB_REG_BASE + 0x00)

/* I2C block - offsets mirror the real header's I2C_BASE + n */
#define I2C_BASE        (NAB_REG_BASE + 0x20)
#define I2CSADR         (I2C_BASE + 0x00)  /* slave address     */
#define I2CCTL          (I2C_BASE + 0x04)  /* control           */
#define I2CSR           (I2C_BASE + 0x08)  /* status            */
#define I2CDR           (I2C_BASE + 0x0C)  /* data              */
#define I2CBC           (I2C_BASE + 0x14)  /* bus speed counter */

/* Control bits (values copied from sys/inc/ml674061.h) */
#define I2CCTL_I2CMD        0x00000003
#define I2CCTL_I2CRSTA      0x00000004
#define I2CCTL_I2CTXAK      0x00000008
#define I2CCTL_I2CMTX       0x00000010
#define I2CCTL_I2CMSTA      0x00000020
#define I2CCTL_I2CAASIE     0x00000040
#define I2CCTL_I2CMEN       0x00000080

/* Status bits */
#define I2CSR_I2CRXAK       0x00000001
#define I2CSR_I2CMIF        0x00000002
#define I2CSR_I2CSRW        0x00000004
#define I2CSR_I2CDR_LD      0x00000008
#define I2CSR_I2CMAL        0x00000010
#define I2CSR_I2CMBB        0x00000020
#define I2CSR_I2CMAAS       0x00000040
#define I2CSR_I2CMCF        0x00000080
#define I2CSR_I2CSTP        0x00000200

#endif
