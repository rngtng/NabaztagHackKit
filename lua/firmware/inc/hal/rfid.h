/**
 * @file rfid.h
 * @brief STMicro CRX14/CR14 RFID coupler over I2C.
 *
 * Trimmed port of src/firmware/src/hal/rfid.c's API (Violet / RedoX GCC port):
 * keeps CRX14 bring-up, the anti-collision initiate/slot-marker/select dance,
 * and UID read. Drops the SRIX EEPROM read/write path (write_eeprom_rfid,
 * read_eeprom_rfid, and the UID->CHIP_ID re-select helpers built on them) -
 * UID read only.
 *
 * Coupler: STMicro CR14 on I2C, confirmed present in the teardown
 * (docs/hardware-dissection.md). Tags are ST SRIX-family (ISO 14443 type
 * B-ish anti-collision).
 */
#ifndef _RFID_H_
#define _RFID_H_

#define CRX14_ADDR 0xA0

#define CRX14_PARAMETER_REGISTER    0x00
#define CRX14_IO_FRAME_REGISTER     0x01
#define CRX14_AUTHENTICATE_REGISTER 0x02
#define CRX14_SLOT_MARKER_REGISTER  0x03

#define RFID_MAX_TAGS 16

struct rfid_tag
{
  uint8_t chip_id;   /* transient anti-collision slot ID, not the tag's identity */
  uint8_t uid[8];
};

/* Turn on the RF field and confirm the CRX14 answers (parameter read-back).
 * @retval 1 on success, 0 if the coupler did not respond. */
uint8_t init_rfid(void);

/* Turn off the RF field (also deselects any selected tag). */
uint8_t close_rfid(void);

/* Anti-collision protocol steps (CRX14 IO-frame commands), exposed for the
 * EEPROM read/write follow-up built on the same dance. */
uint8_t initiate_rfid(void);
uint8_t slot_marker_rfid(void);
uint8_t select_tag_rfid(uint8_t chip_id);
uint8_t read_frame_rfid(uint8_t *data, uint8_t nb_bytes);
uint8_t completion_rfid(void);
uint8_t get_uid_rfid(void);

/* Run the full anti-collision scan (init -> initiate -> slot_marker -> per-tag
 * select+get_uid), filling p_tags (up to RFID_MAX_TAGS). Leaves the field on
 * (caller closes). @return number of tags found, or 0 on I2C error/no tag. */
uint8_t check_rfid_devices(struct rfid_tag *p_tags);

/* nab.rfid() entry point: scan and copy the first tag's 8-byte UID.
 * @retval 1  a tag was found, uid_out is filled
 * @retval 0  no tag present
 * @retval -1 I2C error (coupler not responding) */
int8_t rfid_read_uid(uint8_t uid_out[8]);

#endif
