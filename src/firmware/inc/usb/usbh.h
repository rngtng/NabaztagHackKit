/**
 * @file usbh.h
 * @author Oki Electric Industry Co.,Ltd. - 2003 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief USB Stack - Host core
 */
#ifndef _USBH_H_
#define _USBH_H_

#include "list.h"

#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

/* USB endpoint type */
#define USB_CTRL        0x00
#define USB_ISO          0x01
#define USB_BULK        0x02
#define USB_INT          0x03

/* USB standard requests */
#define USB_GET_STATUS      0x00
#define USB_CLEAR_FEATURE    0x01
#define USB_SET_FEATURE      0x03
#define USB_SET_ADDRESS      0x05
#define USB_GET_DESCRIPTOR    0x06
#define USB_SET_DESCRIPTOR    0x07
#define USB_GET_CONFIGURATION    0x08
#define USB_SET_CONFIGURATION    0x09
#define USB_GET_INTERFACE    0x0A
#define USB_SET_INTERFACE    0x0B
#define USB_SYNCH_FRAME      0x0C

/* USB class requests */
#define USB_GET_REPORT      0x01
#define USB_GET_IDLE      0x02
#define USB_GET_PROTOCOL    0x03
#define USB_SET_REPORT      0x09
#define USB_SET_IDLE      0x0A
#define USB_SET_PROTOCOL    0x0B

/* USB types */
#define USB_TYPE_STANDARD    (0x00 << 5)
#define USB_TYPE_CLASS      (0x01 << 5)
#define USB_TYPE_VENDOR      (0x02 << 5)
#define USB_TYPE_RESERVED    (0x03 << 5)

/* USB recipients */
#define USB_RECIP_MASK      0x1f
#define USB_RECIP_DEVICE    0x00
#define USB_RECIP_INTERFACE    0x01
#define USB_RECIP_ENDPOINT    0x02
#define USB_RECIP_OTHER      0x03

/* USB directions */
#define USB_DIR_OUT      0x00
#define USB_DIR_IN      0x80

/* Descriptor types */
#define USB_DT_DEVICE      0x01
#define USB_DT_CONFIGURATION    0x02
#define USB_DT_STRING      0x03
#define USB_DT_INTERFACE    0x04
#define USB_DT_ENDPOINT      0x05
#define USB_DT_OTG      0x09

#define USB_DT_HID      (USB_TYPE_CLASS | 0x01)
#define USB_DT_REPORT      (USB_TYPE_CLASS | 0x02)
#define USB_DT_PHYSICAL      (USB_TYPE_CLASS | 0x03)
#define USB_DT_HUB      (USB_TYPE_CLASS | 0x09)

/* OTG Feature Selectors */
#define OTG_B_HNP_ENABLE    3
#define OTG_A_HNP_SUPPORT    4
#define OTG_A_ALT_HNP_SUPORT  5

/* Endpoint descriptor */
struct usb_endpoint_descriptor {
  uint8_t  bLength;
  uint8_t  bDescriptorType;
  uint8_t  bEndpointAddress;
  uint8_t  bmAttributes;
  uint16_t wMaxPacketSize;
  uint8_t  bInterval;
  uint8_t  bRefresh;
  uint8_t  bSynchAddress;
  uint8_t  reserved0;
  uint8_t  reserved1;
  uint8_t  reserved2;

  struct usb_endpoint_descriptor *next;
};

/* Interface descriptor */
struct usb_interface_descriptor {
  uint8_t  bLength;
  uint8_t  bDescriptorType;
  uint8_t  bInterfaceNumber;
  uint8_t  bAlternateSetting;
  uint8_t  bNumEndpoints;
  uint8_t  bInterfaceClass;
  uint8_t  bInterfaceSubClass;
  uint8_t  bInterfaceProtocol;
  uint8_t  iInterface;
  uint8_t  reserved0;
  uint8_t  reserved1;
  uint8_t  reserved2;

  void   *expansion;
  struct usb_interface_descriptor *next;
  struct usb_endpoint_descriptor *endpoint;
};

/* Configuration descriptor */
struct usb_configuration_descriptor {
  uint8_t  bLength;
  uint8_t  bDescriptorType;
  uint16_t wTotalLength;
  uint8_t  bNumInterfaces;
  uint8_t  bConfigurationValue;
  uint8_t  iConfiguration;
  uint8_t  bmAttributes;
  uint8_t  bMaxPower;
  uint8_t  reserved0;
  uint8_t  reserved1;
  uint8_t  reserved2;

  struct usb_configuration_descriptor  *next;
  struct usb_interface_descriptor  *interface;
};

/* OTG descriptor */
struct usb_otg_descriptor{
  uint8_t  bLength;
  uint8_t  bDescriptorType;
  uint8_t  bmAttributes;
  uint8_t  reserved0;
};

#define HNP_SUPPORT  0x02
#define SRP_SUPPORT  0x01

/* Device descriptor */
struct usb_device_descriptor {
  uint8_t  bLength;
  uint8_t  bDescriptorType;
  uint16_t bcdUSB;
  uint8_t  bDeviceClass;
  uint8_t  bDeviceSubClass;
  uint8_t  bDeviceProtocol;
  uint8_t  bMaxPacketSize0;
  uint16_t idVendor;
  uint16_t idProduct;
  uint16_t bcdDevice;
  uint8_t  iManufacturer;
  uint8_t  iProduct;
  uint8_t  iSerialNumber;
  uint8_t  bNumConfigurations;
  uint8_t  reserved0;
  uint8_t  reserved1;

  struct usb_configuration_descriptor  *configuration;
  struct usb_otg_descriptor *otg;
};


/****************************************************************************/

struct usbh_device_info;

struct usbh_driver {
  const char *name;
  void *(*connect)(struct usbh_device_info *);
  void (*disconnect)(struct usbh_device_info *);
  LIST_ENTRY driver_list;
};


#define MAX_PIPE  32

/* Device infomation */
typedef struct usbh_device_info{
  uint8_t    dev_speed;
  uint8_t    dev_addr;
  uint8_t       dummy[2];
  struct usb_device_descriptor *descriptor;
  struct usbh_driver *driver;
  void    *driver_data;
  void    *pipe[MAX_PIPE];
} DEVINFO, *PDEVINFO;

/* USB Setup Data */
struct usb_setup{
  uint8_t   bmRequestType;
  uint8_t   bRequest;
  uint16_t  wValue;
  uint16_t  wIndex;
  uint16_t  wLength;
};

/* USBD Request */
struct _URB;
typedef void (*callback)(struct _URB *);
typedef struct _URB {
  void    *buffer;

  uint32_t    length;
  uint32_t    result;

  uint16_t    timeout;
  uint8_t       dma_enable;
  uint8_t       dummy;

  void    *ed;  /* PHCD_ED */
  struct usbh_device_info *dev;
  struct usb_setup *setup;
  callback  callback;

  int8_t status;
  LIST_ENTRY  td_list;
} URB, *PURB;

#define URB_PENDING    (-1)
#define URB_CANCEL    (-2)
#define URB_XFERERR    (-3)
#define URB_ABORT    (-4)
#define URB_NOMEM    (-5)
#define URB_ADDTDERR    (-6)
#define URB_SYSERR    (-7)
#define URB_TIMEOUT    (-8)

void usbh_driver_install(struct usbh_driver *);
void usbh_driver_uninstall(struct usbh_driver *);

PDEVINFO usbh_connect(uint8_t);
int8_t usbh_disconnect(PDEVINFO *);

void *usbh_create_pipe(PDEVINFO, uint8_t, uint8_t, uint16_t, uint8_t);
void usbh_delete_pipe(PDEVINFO, uint8_t);

int8_t usbh_control_transfer(PDEVINFO, uint8_t, uint8_t, uint8_t, uint16_t, uint16_t, uint16_t, void *);
int8_t usbh_bulk_transfer_async(PDEVINFO, uint8_t, void *, uint16_t);
int8_t usbh_transfer_request(PURB urb);
void usbh_transfer_cancel(PURB urb);
int8_t usbh_set_configuration(PDEVINFO, uint16_t);
int8_t usbhost_init(void);
void usbhost_exit(void);
void usbhost_events(void);

#endif /* _USBH_H_ */
