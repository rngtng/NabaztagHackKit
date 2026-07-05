#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "hal/audio.h"
#include "hal/uart.h"

#include "vm/vlog.h"

/**
 * @brief Analog to digital conversion of channel #2
 * @return 8 Most significant bits of the ADC result
 */
uint8_t get_adc_value(void)
{

  return 0;
}

/**
 * @brief Read a register from the VLSI chip, using SPI
 *
 * @param [in] reg  VLSI register address
 *
 * @return Value of the register
 */
uint16_t vlsi_read_sci(uint8_t reg)
{
  return 0;
}

/**
 * @brief Write a register of the VLSI, using SPI
 *
 * @param [in] reg VLSI register address
 * @param [in] val Value
 */
void vlsi_write_sci(uint8_t reg,uint16_t val)
{

}

/**
 * @brief Write a buffer/feed to the VLSI
 *
 * @param [in] data Pointer to the buffer
 * @param [in] len  Length
 *
 * @return Number of written bytes
 */
uint32_t vlsi_feed_sdi(uint8_t* data,uint32_t len)
{
  uint32_t i=0;

  return i;
}

/**
 * @brief Get the status of the VLSI chip
 *
 * @retval true when ready
 * @retval false when not ready
 */
uint8_t vlsi_fifo_ready()
{
  return 1;
}

/**
 * @brief Turn ON or OFF the amplifier
 *
 * @param [in] on new status
 */
void vlsi_ampli(uint8_t on)
{

}

#define patchwma_len 10
const uint32_t patchwma_data[patchwma_len]=
{
0x0207800e,0x02062801,0x02063f80,0x02060006,0x020653d7,
0x020784fe,0x02062000,0x02060000,0x02063f05,0x0206c024
};

void patchwma()
{
  uint8_t i;
  for(i=0;i<patchwma_len;i++)
  {
    uint32_t k=patchwma_data[i];
    vlsi_write_sci((k>>16)&0xFF,k);
  }
}
/**
 * @brief Init the VLSI peripheral
 */
void init_vlsi(void)
{

}

/**
 * @brief Set configuration for adpcm encoding
 *
 * @param [in] sampling_frequency Sampling freq in Hz on 16bits, up to 48kHz
 * @param [in] gain               Gain control
 */
void init_adpcm_encode(uint16_t sampling_frequency, uint16_t gain)
{

}

/**
 * @brief  Exit adpcm mode
 */
void stop_adpcm_encode(void)
{

}



/**
 * @brief Set output volume of the VLSI
 *
 * @param [in] volume Volume on 8bits; the maximum is 0x00
 */
void set_vlsi_volume(uint8_t volume)
{
  //Config VOLUME
  vlsi_write_sci(VS1003_VOLUME,(volume<<8)|volume);
  vlsi_ampli(volume<0x7f);
}


int32_t audioPlayTryFeed(int32_t ask);
int32_t audioPlayFetchByte();
int32_t audioPlayFetch(char* dst,int32_t ask);
char* audioRecFeed_begin(int32_t size);
void audioRecFeed_end();

uint8_t play_state=0,
        rec_state=0;
int32_t valtrytofeed=2048;

/**
 * @brief Start a recording
 *
 * @param [in] sampling_frequency Sampling freq in Hz on 16bits, up to 48kHz
 * @param [in] gain               Gain control
 */
void rec_start(uint16_t sampling_frequency,uint16_t gain)
{

}

/**
 * @brief Stop an ongoing recording
 */
void rec_stop()
{

}

/**
 * @brief Check if ther's an ongoing recording
 */
void rec_check()
{

}

/**
 * @brief Start playing FIXME
 *
 * @param [in]  trytofeed FIXME
 */
void play_start(int32_t trytofeed)
{

}

/**
 * @brief Stop an ongoing replay
 */
void play_stop()
{

}

/**
 * @brief Check if there's an ongoing replay
 *
 * @param [in]  nocb  FIXME
 */
void play_check(int32_t nocb)
{

}

/**
 * @brief FIXME
 */
void play_eof()
{
  if (play_state==1)
    play_state=2;
}

/**
 * @brief  Clear the VLSI input FIFO to be ready for a new file to be played
 */
void clear_vlsi_fifo(void)
{
  consolestr("clear_vlsi_fifo"EOL);
  sw_reset_vlsi();
}


/**
 * @brief Software reset of VS1003
 */
void sw_reset_vlsi(void)
{

}

/**
 * @brief Check the audio file specs which is played
 *
 * @return HDAT1 and HDAT0
 */
uint32_t check_audio_file(void)
{
  return 0;
}

/**
 * @brief Check the number of seconds of data decoded
 *
 * @return  Number of seconds decoded
 */
/****************************************************************************/
uint16_t check_decode_time(void)
{
  return 0;
}
