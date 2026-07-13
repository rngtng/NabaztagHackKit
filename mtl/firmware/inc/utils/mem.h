/**
 * @file mem.h
 * @author Oki Electric Industry Co., LTD. - 2005 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief Memory Flash related functions
 */
#ifndef _MEM_H_
#define _MEM_H_

//#define SRAM_BASE   (0xD0000000)    /* base address of external SRAM */
#define SRAM_BASE   (0xD0010000)    /* base address of external SRAM */

/* Memory shared between ARM and USB chips */
/* The allocator is set up in hcd.c, not here */
#define COMRAM 0

/* Memory on the ARM chip */
#define EXTRAM 1
#define EXTRAM_ADDR 0xD0008000
#define EXTRAM_SIZE 0x7FFF


void setup_ext_sram_rom(void);
void setup_malloc(void);
void read_uc_flash(uint32_t address, uint8_t *data, uint32_t nb_byte);
__attribute__ ((section(".ramfunc"))) void init_uc_flash(void);
__attribute__ ((section(".ramfunc"))) void write_uc_flash(uint32_t address, uint8_t *data, uint32_t nb_byte, uint8_t *temp);
__attribute__ ((section(".ramfunc"))) void flash_uc(uint8_t *data, int32_t nb_byte, uint8_t *temp);
void reset_uc(void);

#endif
