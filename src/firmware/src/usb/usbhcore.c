/**
 * @file usbcore.c
 * @author Oki Electric Industry Co.,Ltd. - 2003 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief USB Stack - Core
 */
#include <string.h>
#include <stdio.h>

#include "ml674061.h"
#include "common.h"

#include "utils/debug.h"
#include "utils/delay.h"
#include "utils/mem.h"

#include "usb/usbh.h"
#include "usb/hcd.h"
#include "usb/hcdmem.h"

DEFINE_LIST_ENTRY(usb_driver_list);

struct _usb_devmap {
	uint32_t map[128 / 32];
} usb_devmap;

uint8_t usbhost_init_status = 0;

uint8_t find_zero_bit(void *map, uint8_t max, uint8_t start)
{
	uint8_t *ptr = (uint8_t *)map;
	while(start<max){
		if(((*ptr >> start) & 1) == 0)
      break;
		if(++start%(32) == 0)
      ptr += 1;
	}
	return start;
}

void set_usb_bit(uint8_t nr, void *addr)
{
	uint8_t bit = nr % (32);
	uint8_t * ptr = (uint8_t *)addr + (nr / (32));
	uint8_t data = *ptr | ((uint8_t)1 << bit);
	*ptr = data;
}

void clear_bit(uint8_t nr, volatile void *addr)
{
	uint8_t bit = nr % (32);
	uint8_t * ptr = (uint8_t *)addr + (uint8_t)(nr / (32));
	*ptr &= ~((uint8_t)1 << bit);
}

int8_t usbhost_init(void)
{
	int8_t ret;
	if(usbhost_init_status == 1)
    return 0;
	ret = hcd_init();
	if(ret<0)
    return ret;

	usbhost_init_status = 1;

	return 0;
}

void usbhost_events(void)
{
//	usb_hub_events();
	hcd_rh_events();
}

void *usbh_create_pipe(PDEVINFO dev, uint8_t type, uint8_t ep_num, ushort maxpacket,
                       uint8_t interval)
{
	return hcd_create_ed((uint8_t)dev->dev_speed, dev->dev_addr, type,
                          ep_num, maxpacket, interval);
}

int8_t usbh_update_pipe0(PDEVINFO dev, ushort maxpacket)
{
	return hcd_update_ed((PHCD_ED)dev->pipe[0], dev->dev_addr, maxpacket);
}

void usbh_delete_pipe(PDEVINFO dev, uint8_t ep_num)
{
	hcd_delete_ed((PHCD_ED)dev->pipe[ep_num]);
	dev->pipe[ep_num] = NULL;
}

int8_t usbh_control_transfer(PDEVINFO dev, uint8_t pipe, uint8_t bRequest,
	uint8_t bmRequestType, uint16_t wValue, uint16_t wIndex, uint16_t wLength,
	void *data)
{
	URB urb;
	struct usb_setup setup;

	urb.buffer	= (uint8_t *)data;
	urb.length	= wLength;
	urb.timeout	= 1000;
	urb.setup		= &setup;
	urb.dev			= dev;
	urb.ed			= dev->pipe[pipe];

	setup.bmRequestType 	= bmRequestType;
	setup.bRequest   	= bRequest;
	setup.wValue		= wValue;
	setup.wIndex		= wIndex;
	setup.wLength		= wLength;

	urb.callback = NULL;

	return hcd_transfer_request(&urb);
}

/* Called with OHCI IRQ masked */
static void usbh_free_urb_callback(PURB urb)
{
	hcd_free(urb->buffer);
	hcd_free(urb);
}

int8_t usbh_bulk_transfer_async(PDEVINFO dev, uint8_t pipe, void *buf, uint16_t size)
{
	URB *urb;

	disable_ohci_irq();
	urb = hcd_malloc(sizeof(URB), EXTRAM,20);
	enable_ohci_irq();
	if(urb == NULL)
    return URB_NOMEM;

	urb->buffer		  = (uint8_t *)buf;
	urb->length		  = size;
	urb->timeout	  = 0;
	urb->setup		  = NULL;
	urb->dev		    = dev;
	urb->ed			    = dev->pipe[pipe];
	urb->dma_enable	= 0;
	urb->callback		= usbh_free_urb_callback;

	return hcd_transfer_request(urb);

}

int8_t usbh_transfer_request(PURB urb)
{
	int8_t ret = hcd_transfer_request(urb);
	if(ret == URB_PENDING)
    ret = 0;
	return ret;
}

void usbh_transfer_cancel(PURB urb)
{
	hcd_transfer_cancel(urb);
}

int8_t usbh_set_address(PDEVINFO dev)
{
	return usbh_control_transfer(dev, 0, USB_SET_ADDRESS, USB_DIR_OUT,
		(uint8_t)dev->dev_addr, 0, 0, NULL);
}

int8_t usbh_get_descriptor(PDEVINFO dev, uint8_t type, uint8_t index, uint16_t size, void *buf)
{
	return usbh_control_transfer(dev, 0, USB_GET_DESCRIPTOR, USB_DIR_IN,
		((type<<8)|index), 0, size, buf);
}

int8_t usbh_set_configuration(PDEVINFO dev, uint16_t index)
{
	return usbh_control_transfer(dev, 0, USB_SET_CONFIGURATION, USB_DIR_OUT,
		index, 0, 0, NULL);
}

int8_t usbh_set_feature_dev(PDEVINFO dev, uint16_t feature)
{
	return usbh_control_transfer(dev, 0, USB_SET_FEATURE, USB_DIR_OUT,
		feature, 0, 0, NULL);
}

int8_t usbh_get_descriptor_device(PDEVINFO dev, uint8_t **pbuf)
{
	int8_t ret;

	*pbuf= (uint8_t *)hcd_malloc(sizeof(struct usb_device_descriptor), EXTRAM,21);
	if(!*pbuf)
  {
    DBG("get_desc00err "EOL);
    return -1;
  }

	ret = usbh_get_descriptor(dev, USB_DT_DEVICE, 0, 18, *pbuf);
	if(ret<18)
  {
    DBG("get_desc01err "EOL);
    return -1;
  }
	return ret;
}

int8_t usbh_get_descriptor_configuration(PDEVINFO dev, uint8_t **pbuf)
{
	int8_t ret;
	uint16_t size;
	uint32_t temp[2];	/* 8Byte buffer */

	ret = usbh_get_descriptor(dev, USB_DT_CONFIGURATION, 0, 8, temp);
	if(ret<0)
  {
    DBG("get_descConf03err "EOL);
    return ret;
  }

	size = ((struct usb_configuration_descriptor *)&temp)->wTotalLength;
	*pbuf= (uint8_t *)hcd_malloc(size, EXTRAM,22);
	if(!*pbuf)
  {
    DBG("get_descConf01err "EOL);
    return -1;
  }

	ret = usbh_get_descriptor(dev, USB_DT_CONFIGURATION, 0, size, *pbuf);
	if(ret<size)
  {
    DBG("get_descConf02err "EOL);
    return -1;
  }
	return ret;
}

void error(int32_t code, int32_t status)
{
#ifdef DEBUG_USB
	char *err;

	switch(code){
		case 00: err = "out of memory";				break;
		case 10: err = "create pipe0";				break;
		case 11: err = "GET_DESCRIPTOR(pre)";			break;
		case 12: err = "No new device address";			break;
		case 13: err = "SET_ADDRESS";				break;
		case 14: err = "update pipe0(address)";			break;
		case 20: err = "GET_DESCRIPTOR";			break;
		case 21: err = "update pipe0(maxpacket)";		break;
		case 22: err = "GET_DESCRIPTOR(device)";		break;
		case 30: err = "SET_CONFIGRATION";			break;
		case 31: err = "GET_CONFIGRATION";			break;
		case 32: err = "Not configuration value=1";		break;
		case 40: err = "GET_DESCRIPTOR(all)";			break;
		case 41: err = "GET_DESCRIPTOR(device)";		break;
		case 42: err = "GET_DESCRIPTOR(configration)";		break;
		case 50: err = "No class driver.";			break;
		default: err = "Unknown error code.";			break;
	}

        sprintf((char*)dbg_buffer,EOL" USBH: Error!! %s (%d, %d)"EOL, err, code, status);
        DBG_USB(dbg_buffer);
#endif
}

void usbh_release_descriptor_all(PDEVINFO dev)
{
	struct usb_configuration_descriptor *configuration;
	struct usb_interface_descriptor *interface;
	struct usb_endpoint_descriptor *endpoint;
	void * temp;

	if(!dev) return;

	if(!dev->descriptor) return;

	configuration = dev->descriptor->configuration;

	while(configuration){

		interface = configuration->interface;
		while(interface){

			if(interface->expansion){
				hcd_free(interface->expansion);
			}

			endpoint = interface->endpoint;
			while(endpoint){
				temp = endpoint;
				endpoint = endpoint->next;
				hcd_free(temp);
			}

			temp = interface;
			interface = interface->next;
			hcd_free(temp);
		}

		temp = configuration;
		configuration = configuration->next;
		hcd_free(temp);
	}

	hcd_free(dev->descriptor);
	dev->descriptor = NULL;
}

int32_t usbh_get_descriptor_all(PDEVINFO dev)
{
	uint8_t bLength, bDescriptorType;
	struct usb_configuration_descriptor **pconfiguration;
	struct usb_interface_descriptor **pinterface;
	struct usb_endpoint_descriptor **pendpoint;
	struct usb_otg_descriptor *otg;
	uint8_t *expansion;
	uint8_t *ptr, *buf;
	int32_t ret;

	if(!dev->descriptor){
		ret = usbh_get_descriptor_device(dev, (uint8_t **)&dev->descriptor);
		if(ret<dev->descriptor->bLength){
			error(41, 0);
			return NG;
		}
		dev->descriptor->configuration = NULL;
		dev->descriptor->otg = NULL;
	}

	pconfiguration = &dev->descriptor->configuration;
	pinterface = NULL;
	pendpoint = NULL;

	ret = usbh_get_descriptor_configuration(dev, &buf);
	if(ret<0){
		error(42, 0);
		return NG;
	}

	ptr = (uint8_t *)buf;

	while(ptr < buf + ret){

		bLength = *ptr;

		if(!bLength) break;

		bDescriptorType = *(ptr+1);

		if(bDescriptorType == USB_DT_CONFIGURATION){

			if(!pconfiguration) break;

			if(*pconfiguration)	pconfiguration = &((*pconfiguration)->next);

			*pconfiguration = (struct usb_configuration_descriptor *)
				hcd_malloc(sizeof(struct usb_configuration_descriptor), EXTRAM,23);
			if(!*pconfiguration) break;

			(*pconfiguration)->next = NULL;
			pinterface = &((*pconfiguration)->interface);
			*pinterface = NULL;
			pendpoint = NULL;

			memcpy((ulong *)*pconfiguration, (uint8_t *)ptr, bLength);
		}

		else if(bDescriptorType == USB_DT_INTERFACE){

			if(!pinterface) break;

			if(*pinterface) pinterface = &((*pinterface)->next);

			*pinterface =(struct usb_interface_descriptor *)
				hcd_malloc(sizeof(struct usb_interface_descriptor), EXTRAM,24);
			if(!*pinterface) break;

			(*pinterface)->expansion = NULL;
			(*pinterface)->next = NULL;
			pendpoint = &((*pinterface)->endpoint);
			*pendpoint = NULL;

			memcpy((ulong *)*pinterface, (uint8_t *)ptr, *ptr);
		}

		else if(bDescriptorType == USB_DT_ENDPOINT){

			if(!pendpoint) break;

			if(*pendpoint) pendpoint = &((*pendpoint)->next);

			*pendpoint =(struct usb_endpoint_descriptor *)
				hcd_malloc(sizeof(struct usb_endpoint_descriptor), EXTRAM,25);
			if(!*pendpoint) break;

			(*pendpoint)->next = NULL;

			memcpy((ulong *)*pendpoint, (uint8_t *)ptr, *ptr);
		}

		else if(bDescriptorType == USB_DT_OTG){

			if(dev->descriptor->otg) break;

			otg = (struct usb_otg_descriptor *)
				hcd_malloc(sizeof(struct usb_otg_descriptor), EXTRAM,26);
			if(!otg) break;

			dev->descriptor->otg = otg;

			memcpy((void *)otg, (uint8_t *)ptr, bLength);
		}

		else{
			if(!pinterface) break;

			if(!*pinterface) break;

			if((*pinterface)->expansion) break;

			expansion = (uint8_t *)hcd_malloc(bLength, EXTRAM,27);
			if(!expansion) break;

			(*pinterface)->expansion = expansion;

			memcpy((void *)expansion, (uint8_t *)ptr, *ptr);
		}

		ptr += bLength;
	}

	if(ptr != buf + ret){
		usbh_release_descriptor_all(dev);
		error(40, 0);
		return NG;
	}

	hcd_free(buf);
	return OK;
}

void usbh_driver_install(struct usbh_driver *driver)
{
#ifdef DEBUG_USB
        sprintf((char*)dbg_buffer, "USBH: install driver (%s)"EOL, driver->name);
        DBG_USB(dbg_buffer);
#endif
	list_add(&driver->driver_list, &usb_driver_list);
}

static int32_t usbh_find_driver(PDEVINFO dev)
{
	LIST_ENTRY *next = usb_driver_list.Flink;
	struct usbh_driver *driver;
	void *data;

	DBG_USB("Trying to find driver !"EOL);
	if(list_empty(&usb_driver_list)) {
		DBG_USB("Empty driver list"EOL);
		return 0;
	}

	while(next != &usb_driver_list) {
		driver = list_struct_entry(next, struct usbh_driver, driver_list);

		data = driver->connect(dev);
		if(data){
                        sprintf((char*)dbg_buffer, "USBH: found driver(%s)"EOL,
				driver->name);
                        DBG_USB(dbg_buffer);
			dev->driver = driver;
			dev->driver_data = data;
			return 0;
		}

		next = next->Flink;
	}

	return -1;
}

static int32_t usbh_enumeration(PDEVINFO dev)
{
	int32_t ret, err;
	uint8_t temp[8];

  DBG_USB("usbh_create_pipe"EOL);
	dev->pipe[0] = usbh_create_pipe(dev, USB_CTRL, 0, 8, 0);
	if(!dev->pipe[0]){
		error(10, 0);
		return -1;
	}

  DBG_USB("usbh_get_descriptor");
	for(err = 0; err < 3; err++){
		ret = usbh_get_descriptor(dev, USB_DT_DEVICE, 0, 8, temp);
    DBG_USB(".");
		if(ret==8)
      break;
    __no_operation();
		DelayMs(20);
    __no_operation();
	}
  DBG_USB(EOL);
	if(ret!=8){
		error(11, ret);
		return -1;
	}

  DBG_USB("find_zero_bit"EOL);
	dev->dev_addr = find_zero_bit(&usb_devmap, 128, 1);
	if (dev->dev_addr < 128) {
		set_usb_bit(dev->dev_addr , &usb_devmap);
	}else{
		error(12, 0);
		return -1;
	}

  DBG_USB("usbh_set_address"EOL);
	ret = usbh_set_address(dev);
	if(ret<0){
		error(13, ret);
		return -1;
	}

	DelayMs(10);

  DBG_USB("usbh_update_pipe0"EOL);
	ret = usbh_update_pipe0(dev, 8);
	if(ret<0){
		error(14, ret);
		return -1;
	}

  DBG_USB("usbh_get_descriptor"EOL);
	ret = usbh_get_descriptor(dev, USB_DT_DEVICE, 0, 8, temp);
	if(ret<8){
		error(20, ret);
		return -1;
	}

  DBG_USB("usbh_update_pipe0"EOL);
	ret = usbh_update_pipe0(dev, temp[7]);
	if(ret<0){
		error(21, ret);
		return -1;
	}

  DBG_USB("usbh_get_descriptor_device"EOL);
	ret = usbh_get_descriptor_device(dev, (uint8_t **)&dev->descriptor);
	if(ret<dev->descriptor->bLength){
		dev->descriptor = NULL;
		error(22, ret);
		return -1;
	}
  __no_operation();
  __no_operation();

  dev->descriptor->configuration = NULL;
  dev->descriptor->otg = NULL;

  DBG_USB("usbh_get_descriptor_all"EOL);
	ret = usbh_get_descriptor_all(dev);
	if(ret<0){
		return -1;
	}

	ret = usbh_find_driver(dev);
  __no_operation();

	if(ret<0){
		error(50, ret);
		return -1;
	}
	return 0;
}

PDEVINFO usbh_connect(uint8_t speed)
{
	PDEVINFO dev;

	DBG_USB(" USBH: connect a new device"EOL);

	dev = (PDEVINFO)hcd_malloc(sizeof(DEVINFO), EXTRAM,28);
	if(!dev){
		error(00, 0);
		return NULL;
	}

	memset(dev, 0, sizeof(DEVINFO));
	dev->dev_speed = speed;

	if(usbh_enumeration(dev)<0){
		DBG_USB(" USBH: Error!! failed enumeration"EOL);
		usbh_release_descriptor_all(dev);
		if(dev->pipe[0]){
			usbh_delete_pipe(dev, 0);
		}
	}

	return dev;
}

int8_t usbh_disconnect(PDEVINFO *pdev)
{
	uint8_t i;
	PDEVINFO dev;

	DBG_USB(" USBH: disconnect a device"EOL);

	if(!pdev)
    return -1;
	dev = *pdev;

	if(!dev)
    return -1;

	if(dev->driver)
		dev->driver->disconnect(dev);

	usbh_release_descriptor_all(dev);

	clear_bit(dev->dev_addr , &usb_devmap);

	for(i=0; i<MAX_PIPE; i++){
		if(dev->pipe[i]){
			usbh_delete_pipe(dev, i);
		}
	}

	hcd_free(dev);
	return 0;
}
