/**
 * @file rfid.c
 * @brief STMicro CRX14/CR14 RFID coupler over I2C.
 *
 * Trimmed port of src/firmware/src/hal/rfid.c (Violet / RedoX GCC port) - see
 * hal/rfid.h for what was dropped (EEPROM read/write) and why.
 */
#include "ml674061.h"
#include "common.h"

#include "hal/i2c.h"
#include "hal/rfid.h"
#include "utils/delay.h"

static uint8_t i2c_ok;

/* Tracks whether the RF field is currently energized. A passive tag draws its
 * power from this field, so toggling it off/on drives a full tag power-cycle
 * (~ms of settle time) on every single scan - a tight nab.rfid() poll loop
 * would then power-cycle the tag every poll, making detection intermittent.
 * Once the field is on, later scans only need to deselect (completion_rfid)
 * before a fresh anti-collision round, not drop the field. */
static uint8_t field_on;

/* The CRX14 needs a short settle after each I2C stop before the next command
 * gets an answer (per the original driver's comments). V1 used DelayMs(1);
 * with the 1 ms tick live (sys/src/tick.c) we do the same - calibrated
 * against the real clock, replacing the uncalibrated ~200k-spin busy loop
 * (#180's open settle-tuning risk, resolved per #195). */
static void rfid_delay_1ms(void)
{
  DelayMs(1);
}

static uint16_t writecheck(uint8_t addr_i2c, uint8_t *data, uint8_t nb_byte)
{
  uint16_t nmax=1000;
  while( (nmax>0) && (!write_i2c(addr_i2c,data,nb_byte)))
  {
    nmax--;
    __no_operation();
  }
  if (!nmax)
    i2c_ok = 0;
  return nmax;
}

static uint16_t readcheck(uint8_t addr_i2c, uint8_t *data, uint8_t nb_byte)
{
  uint16_t nmax=1000;
  while( (nmax>0) && (!read_i2c(addr_i2c,data,nb_byte)))
  {
    nmax--;
    __no_operation();
  }
  if (!nmax)
    i2c_ok = 0;
  return nmax;
}

/**
 * @brief Turn ON electromagnetic field and initialize CRX14
 *
 * If the field is already on (a prior scan left it energized), this only
 * deselects any previously-selected tag so a fresh anti-collision round can
 * run - it does not drop and re-raise the field, which would power-cycle
 * whatever tag is currently sitting on the coupler.
 *
 * @retval 1 on success
 * @retval 0 on error
 */
uint8_t init_rfid(void)
{
  uint8_t dummy_tab[2];

  if (field_on)
    return completion_rfid();

  /* Turn OFF RF (also clears any stale select state) */
  close_rfid();
  /* Turn ON RF */
  dummy_tab[0]=CRX14_PARAMETER_REGISTER;
  dummy_tab[1]=0x10;
  if(!writecheck(CRX14_ADDR,dummy_tab,2) || !readcheck(CRX14_ADDR,dummy_tab,1))
    return 0;

  /* Check if module is ON */
  field_on = dummy_tab[0]==0x10;
  return field_on;
}

/**
 * @brief Turn OFF electromagnetic field
 *
 * @retval 1 on success
 * @retval 0 on error
 */
uint8_t close_rfid(void)
{
  uint8_t dummy_tab[2];
  /* Unselect tags */
  completion_rfid();
  /* Turn OFF RF */
  dummy_tab[0]=CRX14_PARAMETER_REGISTER;
  dummy_tab[1]=0x00;
  if(!writecheck(CRX14_ADDR,dummy_tab,2) || !readcheck(CRX14_ADDR,dummy_tab,1))
    return 0;
  field_on = 0;
  /* Check if module is OFF */
  return (dummy_tab[0]==0x00);
}

/**
 * @brief RFID command to initiate all TAGs present in the electromagnetic field
 *
 * @retval 1 on success
 * @retval 0 on error
 */
uint8_t initiate_rfid(void)
{
  uint8_t dummy_tab[4];
  dummy_tab[0]=CRX14_IO_FRAME_REGISTER;
  dummy_tab[1]=0x02;
  dummy_tab[2]=0x06;
  dummy_tab[3]=0x00;
  return writecheck(CRX14_ADDR,dummy_tab,4);
}

/**
 * @brief RFID command to check all TAGs present in the electromagnetic field
 *
 * @retval 1 on success
 * @retval 0 on error
 */
uint8_t slot_marker_rfid(void)
{
  uint8_t dummy_tab[4];
  dummy_tab[0]=CRX14_SLOT_MARKER_REGISTER;
  return writecheck(CRX14_ADDR,dummy_tab,1);
}

/**
 * @brief RFID command to select a TAG
 *
 * @param [in]  chip_id CHIP_ID of the TAG to select
 *
 * @retval 1 on success
 * @retval 0 on error
 */
uint8_t select_tag_rfid(uint8_t chip_id)
{
  uint8_t dummy_tab[4];
  dummy_tab[0]=CRX14_IO_FRAME_REGISTER;
  dummy_tab[1]=0x02;
  dummy_tab[2]=0x0E;
  dummy_tab[3]=chip_id;
  return writecheck(CRX14_ADDR,dummy_tab,4);
}

/**
 * @brief RFID command to read frame buffer of CRX14
 *
 * @param [in]  data      pointer to the data to return
 * @param [in]  nb_bytes  number of bytes to read
 *
 * @retval 1 on success
 * @retval 0 on error
 */
uint8_t read_frame_rfid(uint8_t *data, uint8_t nb_bytes)
{
  uint8_t dummy_tab[1];
  uint8_t dummy_cmpt;

  /* Blank answer tab */
  for(dummy_cmpt=0; dummy_cmpt<nb_bytes; dummy_cmpt++)
    *(data+dummy_cmpt)=0x00;
  /* Select frame register */
  dummy_tab[0]=CRX14_IO_FRAME_REGISTER;
  if(!writecheck(CRX14_ADDR,dummy_tab,1))
      return 0;
  /* Read answer tab */
  return readcheck(CRX14_ADDR,data,nb_bytes);
}

/**
 * @brief RFID command to deactivate TAGs
 *
 * @retval 1 on success
 * @retval 0 on error
 */
uint8_t completion_rfid(void)
{
  uint8_t dummy_tab[3];
  dummy_tab[0]=CRX14_IO_FRAME_REGISTER;
  dummy_tab[1]=0x01;
  dummy_tab[2]=0x0F;
  return writecheck(CRX14_ADDR,dummy_tab,3);
}

/**
 * @brief RFID command to get unique UID of selected TAG
 *
 * @retval 1 on success
 * @retval 0 on error
 */
uint8_t get_uid_rfid(void)
{
  uint8_t dummy_tab[3];
  dummy_tab[0]=CRX14_IO_FRAME_REGISTER;
  dummy_tab[1]=0x01;
  dummy_tab[2]=0x0B;
  return writecheck(CRX14_ADDR,dummy_tab,3);
}

/**
 * @brief Check if RFID tags are present
 *
 * @param [out] p_tags CHIP_ID + UID of each tag found (up to RFID_MAX_TAGS)
 * @return Number of tags detected
 */
uint8_t check_rfid_devices(struct rfid_tag *p_tags)
{
  uint8_t dummy_tab[20];
  uint16_t dummy_short;
  uint8_t dummy_cmpt;
  uint8_t cmpt_tags=0;

  /* Turn ON RFID */
  init_rfid();
  rfid_delay_1ms();
  if (!i2c_ok)
    return 0;

  /* Run Initiate Command to tags */
  initiate_rfid();
  rfid_delay_1ms();
  /* Read reception buffer */
  read_frame_rfid(dummy_tab, 2);
  if (!i2c_ok)
    return 0;

  /* return if no tag detected */
  if(dummy_tab[0]==0x00)
    return 0;

  /* Turn on detected sequence of tags */
  slot_marker_rfid();
  rfid_delay_1ms();
  /* Read reception buffer */
  read_frame_rfid(dummy_tab, 19);
  if (!i2c_ok)
    return 0;

  /* Check detected devices => CHIP_ID */
  dummy_short = (dummy_tab[2]<<8) + dummy_tab[1];
  for(dummy_cmpt=0; dummy_cmpt<RFID_MAX_TAGS; dummy_cmpt++)
  {
    if(dummy_short & 0x0001)
    {
        (p_tags+cmpt_tags)->chip_id=dummy_tab[dummy_cmpt+3];
        cmpt_tags++;
    }
    dummy_short>>=1;
  }

  /* Get UID of detected tags */
  for(dummy_cmpt=0; dummy_cmpt<cmpt_tags; dummy_cmpt++)
  {
    uint8_t dummy_cmpt_2;
    /* Select Tag */
    select_tag_rfid((p_tags+dummy_cmpt)->chip_id);
    rfid_delay_1ms();
    read_frame_rfid(dummy_tab, 2);
    if (!i2c_ok)
      return dummy_cmpt; /* tags before this one already have a valid UID */

    /* Get UID of selected tag */
    get_uid_rfid();
    rfid_delay_1ms();
    read_frame_rfid(dummy_tab, 9);
    if (!i2c_ok)
      return dummy_cmpt;

    /* Copy UID to struct */
    for(dummy_cmpt_2=0; dummy_cmpt_2<8; dummy_cmpt_2++)
      (p_tags+dummy_cmpt)->uid[dummy_cmpt_2]=dummy_tab[7-dummy_cmpt_2+1];
  }

  return cmpt_tags;
}

/**
 * @brief nab.rfid() entry point: scan and return the first tag's UID.
 *
 * Leaves the RF field on between calls (see init_rfid) so a tag sitting on
 * the coupler stays powered across repeated polls, instead of being reset
 * every call. Only forces the field back off on an I2C error, so the next
 * poll starts from a clean hard reset rather than retrying a wedged bus.
 */
int8_t rfid_read_uid(uint8_t uid_out[8])
{
  struct rfid_tag tags[RFID_MAX_TAGS];
  i2c_ok = 1;
  uint8_t n = check_rfid_devices(tags);
  if (!i2c_ok) {
    close_rfid();
    return -1;
  }
  if (n == 0)
    return 0;
  for (uint8_t i = 0; i < 8; i++)
    uid_out[i] = tags[0].uid[i];
  return 1;
}
