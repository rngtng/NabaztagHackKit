/**
 * @file bugrepro.c
 * @brief Native reproduction harness for firmware VM bugs (issue #66).
 *
 * Links the *real* firmware VM sources (../src/vm, ../src/net) together with
 * the testvm sim stubs and drives the actual buggy code paths. Built with
 * AddressSanitizer so out-of-bounds accesses abort with a precise report.
 *
 * Each scenario is selected by argv[1] so a single out-of-bounds abort does
 * not mask the others. A scenario "reproduces" when ASan reports the access;
 * once the underlying bug is fixed the same scenario runs clean.
 *
 *   ./bugrepro syscmp    -> vlog.c sysCmp()      : OOB read  (issue: strcmp overread)
 *   ./bugrepro store     -> vinterp.c OPstore    : OOB write (issue: || guard tautology)
 *   ./bugrepro setstruct -> vinterp.c OPsetstruct: OOB write (issue: || guard tautology)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "vm/vmem.h"
#include "vm/vinterp.h"
#include "vm/vloader.h"
#include "vm/vbc.h"

/* --- symbols the firmware objects expect the "board" layer to provide --- */
int32_t  _vmem_heap[VMEM_LENGTH];
uint32_t counter_timer, counter_timer_s;
uint8_t  push_button_value(void) { return 0; }
void     reset_uc(void)          { }
void     dump(uint8_t *src, int32_t len) { (void)src; (void)len; }

/* defined in vlog.c (firmware source under test) */
int32_t sysCmp(uint8_t *dst, int32_t i, int32_t ldst,
               uint8_t *src, int32_t j, int32_t lsrc, int32_t len);

/* ---------------------------------------------------------------------------
 * Scenario 1: sysCmp() out-of-bounds read.
 *
 * sysCmp only clamps len when *both* operands overrun. When just one overruns
 * (exactly the case OPvstrcmp produces for a default-length `strcmp` on two
 * strings of different length) len is left unclamped and the shorter buffer is
 * read past its end. Separate malloc()s make ASan flag the exact byte.
 * ------------------------------------------------------------------------- */
static int scen_syscmp(void)
{
    uint8_t *dst = malloc(4);   /* "AAxx" */
    uint8_t *src = malloc(2);   /* "AA"   */
    memcpy(dst, "AAxx", 4);
    memcpy(src, "AA", 2);

    /* i+len (0+4) does NOT exceed ldst(4); j+len (0+4) DOES exceed lsrc(2).
     * sysCmp's `(i+len>ldst)&&(j+len>lsrc)` guard is false -> no clamp ->
     * the loop reads src[2] (and src[3]) which are past the 2-byte buffer. */
    int r = sysCmp(dst, 0, 4, src, 0, 2, 4);

    printf("sysCmp returned %d (expected a clamped, in-bounds compare)\n", r);
    free(dst);
    free(src);
    return 0;
}

/* ---------------------------------------------------------------------------
 * Shared VM bring-up for the interpreter scenarios.
 *
 * Lays a tiny hand-assembled program at the front of _bytecode (which aliases
 * _vmem_heap), points the function table at it, boots the heap past it, and
 * enters interpGo() exactly the way the loader/boot path does.
 * ------------------------------------------------------------------------- */
static uint8_t tabfun_bytes[4];

static void vm_run(const uint8_t *code, int code_len)
{
    uint8_t *bc = (uint8_t *)_vmem_heap;
    const int code_off = 8;          /* leave room; keep code word-aligned */
    int i;

    for (i = 0; i < code_len; i++)
        bc[code_off + i] = code[i];

    /* function 0 starts at code_off; _bc_tabfun holds 4-byte LE entries */
    tabfun_bytes[0] = code_off & 0xff;
    tabfun_bytes[1] = (code_off >> 8) & 0xff;
    tabfun_bytes[2] = (code_off >> 16) & 0xff;
    tabfun_bytes[3] = (code_off >> 24) & 0xff;
    _bc_tabfun = tabfun_bytes;
    _bc_nbfun  = 1;

    vmemInit(64);                    /* heap starts well past our code bytes */
    _sys_start = _vmem_stack - 1;
    for (i = 0; i < SYS_NB; i++)
        VPUSH(NIL);

    VPUSH(INTTOVAL(0));              /* call function 0 */
    interpGo();
}

/* fun0: narg=0, nloc=0, then the opcode body */
#define FUN_HEADER 0x00, 0x00, 0x00

/* ---------------------------------------------------------------------------
 * Scenario 2: OPstore out-of-bounds write.
 *
 * `t.[i] <- v` compiles to OPstore, guarded by `(i>=0)||(i<VSIZE(p))` — a
 * tautology, so any index is accepted. We make a 2-slot table and store at a
 * huge index that lands past the end of the _vmem_heap array; ASan flags the
 * write. A correct `&&` guard would drop the store and run clean.
 * ------------------------------------------------------------------------- */
static int scen_store(void)
{
    /* index = VMEM_LENGTH (0x00032000) -> p+3+index is past the heap array */
    const uint8_t code[] = {
        FUN_HEADER,
        OPmktabb, 2,                          /* push table, size 2         */
        OPint, 0x00, 0x20, 0x03, 0x00,        /* push index = 204800        */
        OPintb, 42,                           /* push value = 42            */
        OPstore,                              /* t.[204800] <- 42  (OOB!)   */
        OPret
    };
    printf("driving OPstore with index 204800 into a size-2 table...\n");
    vm_run(code, sizeof(code));
    printf("OPstore returned without ASan firing (guard rejected the store?)\n");
    return 0;
}

/* ---------------------------------------------------------------------------
 * Scenario 3: OPsetstruct out-of-bounds write (same tautology guard).
 * `p.[i] <- v` with i from the stack.
 * ------------------------------------------------------------------------- */
static int scen_setstruct(void)
{
    /* OPsetstruct: i=VPULL(); v=VPULL(); p=VSTACKGET(0)
     * push order: table, value, index */
    const uint8_t code[] = {
        FUN_HEADER,
        OPmktabb, 2,                          /* push table, size 2         */
        OPintb, 42,                           /* push value = 42            */
        OPint, 0x00, 0x20, 0x03, 0x00,        /* push index = 204800        */
        OPsetstruct,                          /* p.[204800] <- 42  (OOB!)   */
        OPret
    };
    printf("driving OPsetstruct with index 204800 into a size-2 table...\n");
    vm_run(code, sizeof(code));
    printf("OPsetstruct returned without ASan firing (guard rejected?)\n");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <syscmp|store|setstruct>\n", argv[0]);
        return 2;
    }
    if (!strcmp(argv[1], "syscmp"))    return scen_syscmp();
    if (!strcmp(argv[1], "store"))     return scen_store();
    if (!strcmp(argv[1], "setstruct")) return scen_setstruct();
    fprintf(stderr, "unknown scenario: %s\n", argv[1]);
    return 2;
}
