/**
 * @file rfid.h
 * @author Violet - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief RFID low level access
 */
#ifndef _RFID_H_
#define _RFID_H_

/********************/
/* SPECIFIC DEFINES */
/********************/
#define CRX14_PARAMETER_REGISTER      0x00
#define CRX14_IO_FRAME_REGISTER       0x01
#define CRX14_AUTHENTICATE_REGISTER   0x02
#define CRX14_SLOT_MARKER_REGISTER    0x03

#define SR176_CHIP_ID      0x00
#define SRI4K_CHIP_ID      0x00
#define SRIX4K_CHIP_ID     0x00
#define SRIX512_CHIP_ID    0x00

struct _tag_rfid
{
  uint8_t  CHIP_ID;
  uint8_t  UID[8];
};

/*************/
/* Functions */
/*************/
uint8_t  init_rfid(void);
uint8_t  close_rfid(void);
uint8_t  initiate_rfid(void);
uint8_t  slot_marker_rfid(void);
uint8_t  select_tag_rfid(uint8_t  chip_id);
uint8_t  read_frame_rfid(uint8_t  *data, uint8_t  nb_bytes);
uint8_t  completion_rfid(void);
uint8_t  get_uid_rfid(void);
uint8_t  check_rfid_devices(struct _tag_rfid *p_tag_rfid);
void write_eeprom_rfid(uint8_t  chip_id, uint8_t  num_block,
                       uint8_t  *data, uint8_t  num_bytes);
void read_eeprom_rfid(uint8_t  chip_id, uint8_t  num_block,
                      uint8_t  *data, uint8_t  num_bytes);

uint8_t* get_rfid_first_device();

#endif
