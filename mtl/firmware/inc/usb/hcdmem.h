/**
 * @file hcdmem.h
 * @author Oki Electric Industry Co.,Ltd. - 2003 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief HCD dynamic memory management
 */
#ifndef _HCDMEM_H_
#define _HCDMEM_H_

/* Memory allocator */
int8_t hcd_malloc_init(int32_t, uint32_t, uint32_t, uint8_t);
void *hcd_malloc(uint32_t, int8_t,int32_t);
int8_t hcd_free(void *);
void hcd_malloc_info(int32_t);
int8_t hcd_malloc_check(void *);
int32_t hcd_malloc_rest(uint8_t);

#endif /* _HCDMEM_H_ */
