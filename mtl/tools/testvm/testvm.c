#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/un.h>
#include <sys/socket.h>

#include "vm/vmem.h"
#include "vm/vinterp.h"
#include "vm/vloader.h"
#include "vm/vlog.h"
#include "hal/uart.h"

extern const uint8_t dumpbc[];

uint32_t counter_timer, counter_timer_s;

int32_t _vmem_heap[VMEM_LENGTH];

#define BUTTON_SOCK_PATH "/sock/button.sock"
static int btn_sock = -1;

static void btn_sock_init(void)
{
    struct sockaddr_un addr;
    btn_sock = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (btn_sock < 0) return;
    unlink(BUTTON_SOCK_PATH);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, BUTTON_SOCK_PATH, sizeof(addr.sun_path) - 1);
    if (bind(btn_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(btn_sock); btn_sock = -1; return;
    }
    fcntl(btn_sock, F_SETFL, fcntl(btn_sock, F_GETFL, 0) | O_NONBLOCK);
}

uint8_t push_button_value(void)
{
    char buf[1];
    if (btn_sock >= 0 && recv(btn_sock, buf, 1, 0) == 1)
        return 1;
    return 0;
}

int main(void)
{
    vmemInit(0);
    loaderInit((uint8_t *)dumpbc);

    putst_uart((uint8_t *)"dumpShort" EOL);
    vmemDumpShort();
    vmemDump();
    uint32_t i;
    for (i = 0; i < _bc_nbfun; i++)
        printf("fun %d at %d" EOL, i, loaderFunstart(i));

    btn_sock_init();

    VPUSH(INTTOVAL(0));
    interpGo();
    (void)VPULL();

    while (1)
    {
        VPUSH(VCALLSTACKGET(_sys_start, SYS_CBLOOP));
        if (VSTACKGET(0) != NIL)
            interpGo();
        (void)VPULL();
    }

    return EXIT_SUCCESS;
}

void dump(uint8_t *src, int32_t len)
{
    int32_t i, j;
    uint8_t buffer[64];
    putst_uart((uint8_t *)EOL);
    for (i = 0; i < len; i += 16)
    {
        sprintf((char*)buffer, "%04x ", i);
        putst_uart(buffer);
        for (j = 0; j < 16; j++) if (i + j < len)
        {
            sprintf((char*)buffer, "%02x ", src[i + j]);
            putst_uart(buffer);
        }
        else putst_uart((uint8_t*)"   ");
        for (j = 0; j < 16; j++) if (i + j < len)
            putch_uart(((src[i + j] >= 32) && (src[i + j] < 128)) ? src[i + j] : '.');
        putst_uart((uint8_t *)EOL);
    }
}

void reset_uc(void)
{
}
