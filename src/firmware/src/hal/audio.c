/**
 * @file audio.c
 * @author Violet - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief Manage the audio chip, record and replay
 */
#include "ml674061.h"
#include "common.h"

#include "utils/delay.h"

#include "hal/audio.h"
#include "hal/spi.h"

#include "usb/hcd.h"

#include "vm/vlog.h"

#if (PCB_RELEASE == LLC2_3) || (PCB_RELEASE == LLC2_4c)

#ifdef SINUS_TEST                    //SDI tests allowed
#define VS1003_MODE_VALUE_L (SM_TESTS)
#else
#define VS1003_MODE_VALUE_L (0x00)
#endif
#define VS1003_MODE_VALUE_H (SM_SDISHARE | SM_SDINEW) //VS1002 native SPI mode active

#elif PCB_RELEASE == LLC2_2

#ifdef SINUS_TEST                    //SDI tests allowed
#define VS1003_MODE_VALUE_L (SM_TESTS | SM_DIFF)
#else
#define VS1003_MODE_VALUE_L (SM_DIFF)
#endif
#define VS1003_MODE_VALUE_H (SM_SDINEW) //VS1002 native SPI mode active

#endif

/**
 * @brief Analog to digital conversion of channel #2
 * @return 8 Most significant bits of the ADC result
 */
uint8_t get_adc_value(void)
{
  //Start ADC for channel #2
  set_hbit(ADCON1, ADCON1_STS | ADCON1_CH2);
  //Wait for the ADC to be done
  while(get_hvalue(ADCON1) & ADCON1_STS);
  //Return the ADC in a 8bits result
  return (uint8_t)(get_hvalue(ADR2)>>2);
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
  uint16_t received_short;
  while( !(INT_AUDIO_READ & INT_AUDIO_BIT) ) CLR_WDT;
  CS_AUDIO_SCI_CLEAR;
  WriteSPI(VS1003_READ);
  WriteSPI(reg);
  //Clear reception buffer
  while( get_wvalue(SPSR0) & SPSR0_RFD ) get_value(SPDRR0);
  received_short = ReadSPI()<<8;
  received_short += ReadSPI();
  CS_AUDIO_SCI_SET;
  return received_short;
}

/**
 * @brief Write a register of the VLSI, using SPI
 *
 * @param [in] reg VLSI register address
 * @param [in] val Value
 */
void vlsi_write_sci(uint8_t reg,uint16_t val)
{
  while( !(INT_AUDIO_READ & INT_AUDIO_BIT) ) CLR_WDT;
  CS_AUDIO_SCI_CLEAR;
  WriteSPI(VS1003_WRITE);
  WriteSPI(reg);
  WriteSPI(val>>8);
  WriteSPI(val);
  CS_AUDIO_SCI_SET;
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
  CS_AUDIO_SDI_CLEAR;
  while((i<len)&&(INT_AUDIO_READ & INT_AUDIO_BIT)) WriteSPI(data[i++]);
  CS_AUDIO_SDI_SET;
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
  return ((INT_AUDIO_READ & INT_AUDIO_BIT) == INT_AUDIO_BIT);
}

/**
 * @brief Turn ON or OFF the amplifier
 *
 * @param [in] on new status
 */
void vlsi_ampli(uint8_t on)
{
  if (on)
  {
    TURN_ON_AUDIO_AMPLIFIER;
  }
  else
  {
    TURN_OFF_AUDIO_AMPLIFIER;
  }
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
  //Set SPI0 to low speed before initialising PLL of VS1003 => 2.11MHz max
  //SPI0 baudrate = APB_CLK/16 => 2MHz @ 32MHz
  clr_wbit(SPCR0,SPCR0_SPE);
  put_wvalue(SPBRR0,0x00000008);
  set_wbit(SPCR0,SPCR0_SPE);

  //Set VS1003
  RST_AUDIO_CLEAR;
  DelayMs(1);
  RST_AUDIO_SET;
  DelayMs(1);

  //Update CLKI of VS 1003 from 12.688MHz to 12.688MHZ x 4 = 49.152MHz
  //So SCI write and SDI can be up to 49.152MHz/4 = 12.288MHZ
  //So SCI read can be up to 49.152MHz/6 = 8.192MHZ
  //vlsi_write_sci(VS1003_CLOCKF,0xc430);
  vlsi_write_sci(VS1003_CLOCKF,0xc000);

  //Set SPI0 to low speed before initialising PLL of VS1003 => 8.458MHz max
  //SPI0 baudrate = APB_CLK/4 => 8MHz @ 32MHz
  clr_wbit(SPCR0,SPCR0_SPE);
  put_wvalue(SPBRR0,0x00000002);
  set_wbit(SPCR0,SPCR0_SPE);

  //Config MODE
  vlsi_write_sci(VS1003_MODE,
                 (VS1003_MODE_VALUE_H<<8)|VS1003_MODE_VALUE_L | SM_RESET);

  //Config VOLUME
  vlsi_write_sci(VS1003_VOLUME,0xffff);

  patchwma();
}

/**
 * @brief Set configuration for adpcm encoding
 *
 * @param [in] sampling_frequency Sampling freq in Hz on 16bits, up to 48kHz
 * @param [in] gain               Gain control
 */
void init_adpcm_encode(uint16_t sampling_frequency, uint16_t gain)
{
  uint16_t config_frequency;

  //Sampling frequency up to 48kHz
  //XTALI id 12.288MHz x 4 = 49.152MHz and /256 => 192000
  config_frequency=(uint16_t)(192000/sampling_frequency);

  //Config Sampling frequency
  vlsi_write_sci(VS1003_AICTRL0,config_frequency);

  //Config Automatic Gain Control
  vlsi_write_sci(VS1003_AICTRL1,gain);

  //Config MODE
  vlsi_write_sci(VS1003_MODE,
                 ((VS1003_MODE_VALUE_H | SM_ADPCM_HP | SM_ADPCM)<<8)|
                  VS1003_MODE_VALUE_L | SM_RESET);
}

/**
 * @brief  Exit adpcm mode
 */
void stop_adpcm_encode(void)
{
  //Config MODE
  vlsi_write_sci(VS1003_MODE,
                 ((VS1003_MODE_VALUE_H)<<8)|VS1003_MODE_VALUE_L | SM_RESET);
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
  // Playing something, exit
  if (play_state)
    return;
  consolestr("rec_start"EOL);
  clear_vlsi_fifo();
  vlsi_ampli(0);  // Turn off amplifier
  init_adpcm_encode(sampling_frequency,gain);
  rec_state=1;  // Update state
}

/**
 * @brief Stop an ongoing recording
 */
void rec_stop()
{
  // Not recording, exit
  if (!rec_state)
    return;
  consolestr("rec_stop"EOL);
  stop_adpcm_encode();
  vlsi_ampli(0);  // Turn off amplifier
  clear_vlsi_fifo();
  rec_state=0;  // Update state
}

/**
 * @brief Check if ther's an ongoing recording
 */
void rec_check()
{
  uint16_t received_short;
  // Not recording, exit
  if (!rec_state)
    return;
  //each audio block is 256bytes long, but the buffer fill information is
  //in 16bits word information

  received_short=vlsi_read_sci(VS1003_HDAT1)&0xFF80;
  if (received_short)
  {
    uint16_t val;
    char* dst=audioRecFeed_begin(received_short<<1);
    if (dst)
      while(received_short--)
      {
        val=vlsi_read_sci(VS1003_HDAT0);
        *(dst++)=val>>8;
        *(dst++)=val;
      }
    audioRecFeed_end();
  }
}

/**
 * @brief Start playing FIXME
 *
 * @param [in]  trytofeed FIXME
 */
void play_start(int32_t trytofeed)
{
  // Recording something, exit
  if (rec_state)
    return;
  consolestr("play_start"EOL);
  clear_vlsi_fifo();
  patchwma();

  if (trytofeed<=0)
    trytofeed=1024;
  valtrytofeed=trytofeed;
  vlsi_ampli(1); // Turn ampli ON
  play_state=1;
}

/**
 * @brief Stop an ongoing replay
 */
void play_stop()
{
  // Not playing, exit
  if (!play_state)
    return;
  consolestr("play_stop"EOL);
  vlsi_ampli(0);  // Turn ampli OFF
  clear_vlsi_fifo();
  play_state=0;
}

/**
 * @brief Check if there's an ongoing replay
 *
 * @param [in]  nocb  FIXME
 */
void play_check(int32_t nocb)
{
  int32_t empty=0;
  int32_t nb=0;

  /* Not playing
   * or buffer busy
   * => Exit
   */
  if (!play_state || !vlsi_fifo_ready())
    return;

  if (!nocb)
    nocb=1-audioPlayTryFeed(valtrytofeed);

  disable_ohci_irq();

  // au moins 32 octets dispos
  while(vlsi_fifo_ready() && !empty)
  {
    CLR_WDT;
    int32_t val=audioPlayFetchByte();
    if ((val<0)&&(nb+nb>=valtrytofeed)&&(!nocb))
    {
      nocb=1-audioPlayTryFeed(valtrytofeed);
      audioPlayTryFeed(valtrytofeed);
      val=audioPlayFetchByte();
    }
    if (val>=0)
    {
      nb++;
      CS_AUDIO_SDI_CLEAR;
      WriteSPI(val);
      CS_AUDIO_SDI_SET;
    }
    else
    {
      empty=1;
      if (play_state==2)
        play_state=0;
    }
  }
  enable_ohci_irq();
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
  //Config MODE
  vlsi_write_sci(VS1003_MODE,
                 (VS1003_MODE_VALUE_H<<8)|VS1003_MODE_VALUE_L | SM_RESET);
}

/**
 * @brief Check the audio file specs which is played
 *
 * @return HDAT1 and HDAT0
 */
uint32_t check_audio_file(void)
{
  return ((uint32_t)vlsi_read_sci(VS1003_HDAT1)<<16)|
          vlsi_read_sci(VS1003_HDAT0);
}

/**
 * @brief Check the number of seconds of data decoded
 *
 * @return  Number of seconds decoded
 */
/****************************************************************************/
uint16_t check_decode_time(void)
{
  return vlsi_read_sci(VS1003_DECODE_TIME);
}
