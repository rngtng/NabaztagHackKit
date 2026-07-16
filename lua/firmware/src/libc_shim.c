/**
 * @file libc_shim.c
 * @brief Tiny local replacements for newlib symbols whose library versions
 *        drag the stdio FILE layer into flash.
 *
 * The vendored 802.11/WPA stack calls rand() (WEP IV in ieee80211.c, EAPOL
 * nonces in eapol.c). Newlib's rand() lives in an archive member whose
 * companion code asserts, and newlib's __assert_func prints via fiprintf -
 * so one rand() reference links vfprintf + fvwrite + findfp + fflush + the
 * fopen/getc cluster: ~9 KB of flash for a PRNG (measured on lua.elf:
 * 122,844 -> 113,792 B text with this file). Defining rand/srand/
 * __assert_func here keeps all of that out of the image; --gc-sections
 * drops this file from apps that never reference the symbols.
 *
 * PRNG quality is unchanged: newlib's rand() is also a plain LCG, and no
 * caller ever seeded it (srand() had no referent in this firmware), so
 * EAPOL nonces were deterministic per boot before and after this shim.
 * Callers that want per-boot variation can srand(counter_timer) once a
 * tick source is running.
 */
#include <stdlib.h>

static unsigned long rand_state = 0x2545f491UL;

int rand(void)
{
  /* Same LCG family as newlib (C99 sample generator); RAND_MAX span. */
  rand_state = rand_state * 1103515245UL + 12345UL;
  return (int)((rand_state >> 16) & 0x7fff);
}

void srand(unsigned int seed)
{
  rand_state = seed ? seed : 1;
}

/* Newlib's __assert_func formats to stderr (the whole stdio chain) and
 * abort()s. Nothing on this target can catch either; park instead. Our own
 * code never asserts - this exists only so no libc archive member's assert
 * can re-pull stdio. */
void __assert_func(const char *file, int line, const char *func,
                   const char *expr)
{
  (void)file;
  (void)line;
  (void)func;
  (void)expr;
  for (;;)
    ;
}
