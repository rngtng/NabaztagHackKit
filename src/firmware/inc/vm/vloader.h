/**
 * @file vloader.h
 * @author Sylvain Huet - 2006 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief VLISP Virtual Machine - Loader functions
 */
#ifndef _LOADER_
#define _LOADER_

/** @todo Find the meaning of theses defines, document it */
#define SYS_CBPLAY  0   /**< @brief FIXME */
#define SYS_CBUDP   1   /**< @brief FIXME */
#define SYS_CBTCP   2   /**< @brief FIXME */
#define SYS_CBLOOP  3   /**< @brief FIXME */
#define SYS_ENV     4   /**< @brief FIXME */
#define SYS_CBREC   5   /**< @brief FIXME */
#define SYS_NB      6   /**< @brief FIXME */

/**
 * Macro to improve readability, when accessing bytecode array directly
 */
#define _bytecode ((uint8_t*)_vmem_heap)

extern uint8_t *_bc_tabfun;
extern uint16_t _bc_nbfun;
extern int32_t _sys_start;
extern int32_t _global_start;

/**
 * @brief Macro to get a single byte from src array
 *
 * @param [in]  *src  Source address
 */
//#define loaderGetByte(src)  ((int32_t)(src[0]&0xFF))
/**
 * @brief Macro to get a Short from src array
 *
 * @param [in]  *src  Source address
 */
//#define loaderGetShort(src) (((int32_t)src[0])

/**
 * @brief Macro to get an Integer from src array
 *
 * @param [in]  *src  Source address
 */
//#define loaderGetInt(src)   (*(uint32_t*)(src))
int32_t loaderGetByte(uint8_t *s);
int32_t loaderGetShort(uint8_t *s);
int32_t loaderGetInt(uint8_t *s);

int32_t loaderFunstart(int32_t funnumber);

void loaderInit(uint8_t *src);

#endif

