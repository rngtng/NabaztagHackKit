/**
 * @file rt2501usb_buffer.h
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief RT2501 USB buffer management
 */
#ifndef _RT2501_USB_BUFFER_H_
#define _RT2501_USB_BUFFER_H_

void rt2501buffer_init(void);
void rt2501buffer_free(void);
uint8_t rt2501buffer_new(const uint8_t *data, uint32_t length,
                     const uint8_t *source_mac,
                     const uint8_t *dest_mac);

#endif
