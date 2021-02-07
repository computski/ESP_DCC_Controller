// 
// 
// 

#include "DCClayer1.h"

/*DCClayer1 puts a DCC signal on the track.  It will continuously write the DCCbuffer to the track
The routine also sets a msTickFlag every 10mS which the main loop can use for general timing such as
keyboard scans.


2020-06-08 RailCom cutout.  RailCom is not supported at this time.
S9.3.2 para 2.1.  After each packet transmission power is disconnected from the track and the track X-Y is shorted
so that a current loop can be detected. Per 2.4, the cutout time shall be 450uS.
NOTE: this routine originally only controlled the dcc signal pin(s).  On the in-built LMD18200T hardware, I control PWM, DIR and
BRAKE is tied to ground.  Per the logic table, if PWM is low the two output drivers are both connected to Source and are
therefore 'shorted' to allow a current loop. On my hardware it is not possible to make the drivers open circuit (as brake
cannot go high). i.e. the hardware is RailCom compatible as we an support a current loop through the booster.

a DDC 1 is 116uS.  a preamble is a series of 1s.  Railcom cutout at 450us is approx 4 x 1-bit periods = 464uS
and during this time we need to set PWM=low.  Dir=don't care.  its not possible to read the output latch state on the ESP
easily, so we need to abstract main-loop power cut-off to the DCC packet object and have layer-one OR this in as required.

will this work for other hardware? L298, we drive the IN1 and IN2 with antiphase DCC signals, so to implement a RailCom cutout
we need to take both these logic levels to the same.   Ditto for the BT2 driver.  For both these two types of drivers, the Enable
logic will make the drivers go open-circuit.   We need conditional compilation to differentiate the drive types.


Note: using non PWM compat mode, the timebase is 200nS.
*/

#include <c_types.h>
#include <pwm.h>
#include <eagle_soc.h>
#include <ets_sys.h>
	//now adding gpio.h for function GPIO_PIN_ADDR
#include <gpio.h>

#ifndef SDK_PWM_PERIOD_COMPAT_MODE
#define SDK_PWM_PERIOD_COMPAT_MODE 0
#endif
#define PWM_USE_NMI 0

/* no user servicable parts beyond this point */

/*notes.
 * compat mode 0.  works with nmI = 0 or 1
 * nmi=1  does not work with webserver present
 * nmi=0  does work with webserver present
 * compat mode 1.  does not work with mni=1
*/

#if SDK_PWM_PERIOD_COMPAT_MODE
/*ticks are 1uS. This may not work because minimum reload value needs to be >100*/
#define ticksZERO 116  //116uS half cycles for DCC zero
#define ticksONE  58  //58uS half cycles for DCC one
#define ticksMS  172  //10mS interval
#define ticksRAILCOM 450 //450uS railcom cutout

#else
/*ticks are 200nS*/
#define ticksZERO 580  //116uS half cycles for DCC zero
#define ticksONE  281  //58uS half cycles for DCC one, was 290 tweaked to 281
#define ticksMS  172  //10mS interval.  will need adjusting from 172
#define ticksRAILCOM 2250 //450uS railcom cutout
#endif





// from SDK hw_timer.c
#define TIMER1_DIVIDE_BY_16             0x0004
#define TIMER1_DIVIDE_BY_256            0x0008
#define TIMER1_ENABLE_TIMER             0x0080


	struct gpio_regs {
		uint32_t out;         /* 0x60000300 */
		uint32_t out_w1ts;    /* 0x60000304 */
		uint32_t out_w1tc;    /* 0x60000308 */
		uint32_t enable;      /* 0x6000030C */
		uint32_t enable_w1ts; /* 0x60000310 */
		uint32_t enable_w1tc; /* 0x60000314 */
		uint32_t in;          /* 0x60000318 */
		uint32_t status;      /* 0x6000031C */
		uint32_t status_w1ts; /* 0x60000320 */
		uint32_t status_w1tc; /* 0x60000324 */
	};
	static struct gpio_regs* gpio = (struct gpio_regs*)(0x60000300);

	struct timer_regs {
		uint32_t frc1_load;   /* 0x60000600 */
		uint32_t frc1_count;  /* 0x60000604 */
		uint32_t frc1_ctrl;   /* 0x60000608 */
		uint32_t frc1_int;    /* 0x6000060C */
		uint8_t  pad[16];
		uint32_t frc2_load;   /* 0x60000620 */
		uint32_t frc2_count;  /* 0x60000624 */
		uint32_t frc2_ctrl;   /* 0x60000628 */
		uint32_t frc2_int;    /* 0x6000062C */
		uint32_t frc2_alarm;  /* 0x60000630 */
	};
	static struct timer_regs* timer = (struct timer_regs*)(0x60000600);

	// 3-tuples of MUX_REGISTER, MUX_VALUE and GPIO number
	typedef uint32_t(pin_info_type)[3];

	static uint16_t dcc_mask = 0;
	static uint16_t dcc_maskInverse = 0;

	static uint8_t  dccCount;

	enum DCCbit { DCC_ONE_H, DCC_ONE_L, DCC_ZERO_H, DCC_ZERO_L, TEST_H, TEST_L };
	static enum DCCbit DCCperiod = DCC_ONE_H;



	volatile DCCBUFFER DCCpacket;  //externally visible
	volatile DCCBUFFER _TXbuffer;   //internal to this module


	//DCC layer 1 
	volatile uint8_t  TXbyteCount;
	volatile uint8_t  TXbitCount;



	/*Interrupt handler
	  for a dcc_zero or dcc_one the reload periods are different.  We queue up the next-bit in the second half of the bit currently being transmitted
	  Some jitter is inevitable with maskable Ints, but it does not cause any problems with decooding in the locos at present.
	  The handler will work its way through the TXbuffer transmitting each byte.  when it reaches the end, it sets the byte pointer to zero
	  and sets the bitcounter to 22 indicating a preamble.  at this point it clears the DCCclearToSend flag to lock the DCCpacket buffer from writes for the next
	  12 preamble bits this is so that the main prog loop routines have long enough to finish writing to the DCCpacket  if they had just missed the flag clearing.
	  Once preamble is transmitted, the handler will copy the DCCpacket to the transmit buffer and set the DCCclearToSend flag indicating it is able to accept
	  a new packet.  If the DDCpacket is not modified by the main loop, this layer 1 handler will continuously transmit the same packet to line.  This is useful
	  as it allows an idle to be continuously transmitted when we are in Service Mode for example.
	*/


	static void ICACHE_RAM_ATTR pwm_intr_handler(void) {

		/*set the period based on the bit-type we queued up in the last int*/

		switch (DCCperiod) {
		case DCC_ZERO_H:
			WRITE_PERI_REG(&timer->frc1_load, ticksZERO);
			gpio->out_w1ts = dcc_mask;  //set bits to logic 1
			gpio->out_w1tc = dcc_maskInverse;  //set bits to logic 0
			dccCount++;
			break;
		case DCC_ZERO_L:
			WRITE_PERI_REG(&timer->frc1_load, ticksZERO);
			gpio->out_w1ts = dcc_maskInverse;  //set bits to logic 1
			gpio->out_w1tc = dcc_mask;  //set bits to logic 0
			dccCount++;
			break;
		case DCC_ONE_H:
			WRITE_PERI_REG(&timer->frc1_load, ticksONE);
			gpio->out_w1ts = dcc_mask;  //set bits to logic 1
			gpio->out_w1tc = dcc_maskInverse;  //set bits to logic 0
			break;
		case DCC_ONE_L:
			WRITE_PERI_REG(&timer->frc1_load, ticksONE);
			gpio->out_w1ts = dcc_maskInverse;  //set bits to logic 1
			gpio->out_w1tc = dcc_mask;  //set bits to logic 0
			break;
			/*the delay gets executed and the block below sets next-state and queues up next databit*/
		}


		timer->frc1_int &= ~FRC1_INT_CLR_MASK;
		//this memory barrier compiler instruction is left in from the code I leveraged
		asm volatile ("" : : : "memory");

		switch (DCCperiod) {
		case DCC_ZERO_H:
			DCCperiod = DCC_ZERO_L;   //queue up second part of the zero bit
			break;
		case DCC_ONE_H:
			DCCperiod = DCC_ONE_L;   //queue up second part of the one bit
			break;

		default:
			/*if executing the low part of a DCC zero or DCC one, then advance bit sequence and queue up next bit */
			DCCperiod = DCC_ONE_H;  //default
			if (TXbitCount == 21) { DCCpacket.clearToSend = false; }

			if (TXbitCount == 9) {
				/*copy DCCpacket to TXbuffer. memcpy woould be slower than direct assignment
				immediately after data is copied, set DCCclearToSend which flags to DCCcore module that a new
				DCCpacket may be written 
				2019-12-05 increased to 6 packet buffer with copy-over*/
				
				_TXbuffer.data[0] = DCCpacket.data[0];
				_TXbuffer.data[1] = DCCpacket.data[1];
				_TXbuffer.data[2] = DCCpacket.data[2];
				_TXbuffer.data[3] = DCCpacket.data[3];
				_TXbuffer.data[4] = DCCpacket.data[4];
				_TXbuffer.data[5] = DCCpacket.data[5];
				_TXbuffer.packetLen = DCCpacket.packetLen;
				_TXbuffer.longPreamble = DCCpacket.longPreamble;
				
				TXbyteCount = 0;
				DCCpacket.clearToSend = true;
			}
			if (TXbitCount <= 8) {
				if (TXbitCount == 8)
				{
					//8 is a start bit, or a preamble
					if (TXbyteCount == _TXbuffer.packetLen)
					//2020-06-08 end of a packet, it is now, and prior 
					//to preamble for next packet that we assert a RailCom cutout
						
					{
						if (DCCpacket.longPreamble)
						{
							TXbitCount = 32;
						}  //long peamble 24 bits
						else
						{
							TXbitCount = 22;
						}  //Preamble 14 bits

						TXbyteCount = 0;
					}
					else
					{
						DCCperiod = DCC_ZERO_H; //queue up a zero separator
					}
				}
				else
				{
					/*must be 7-0, queue up databit*/
					if ((_TXbuffer.data[TXbyteCount] & (1 << TXbitCount)) == 0)
					{//queue a zero
						DCCperiod = DCC_ZERO_H; //queue up a zero
					}

					/*special case 0, assert bit but set bit count as 9 as it immediatley decrements to 8 on exit*/
					if (TXbitCount == 0) { TXbyteCount++; TXbitCount = 9; }
				}
			}
			TXbitCount--;
		}
		/*ten millisecond flag.  DCC zeros have twice the period length hence the count is doubled for these*/
		dccCount++;
		if (dccCount >= ticksMS) {
			dccCount = 0;
			DCCpacket.msTickFlag = true;
		}
	}


	/*Initialisation. call repeatedly to activate additional DCC outputs*/
	void ICACHE_FLASH_ATTR dcc_init(uint32_t pin_info[3], uint8_t invert)
	{
		//load with an IDLE packet
		DCCpacket.data[0] = 0xFF;
		DCCpacket.data[1] = 0;
		DCCpacket.data[2] = 0xFF;
		DCCpacket.packetLen = 3;


		// PIN info: MUX-Register, Mux-Setting, PIN-Nr
		PIN_FUNC_SELECT(pin_info[0], pin_info[1]);
		//PIN_PULLUP_EN(pin_info[0]); 
		if (invert == 0) {
			dcc_mask |= (1 << pin_info[2]);
		}
		else {
			dcc_maskInverse |= (1 << pin_info[2]);
		}
		/*clear target bit with OUT_W1TC.  enable as an output with ENABLE_WITS*/
		GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS, (1 << pin_info[2]));  //clear target bit
		GPIO_REG_WRITE(GPIO_ENABLE_W1TS_ADDRESS, (1 << pin_info[2]));  //set pin as an output
		/*select totem-pole output, default is open drain which is selected with |4*/
		GPIO_REG_WRITE(GPIO_PIN_ADDR(pin_info[2]), GPIO_REG_READ(GPIO_PIN_ADDR(pin_info[2])) & ~4);


#if PWM_USE_NMI
		ETS_FRC_TIMER1_NMI_INTR_ATTACH(pwm_intr_handler);
#else
		ETS_FRC_TIMER1_INTR_ATTACH(pwm_intr_handler, NULL);
#endif

		TM1_EDGE_INT_ENABLE();
		ETS_FRC1_INTR_ENABLE();
		RTC_REG_WRITE(FRC1_LOAD_ADDRESS, 0);  //i think this starts timer

		timer->frc1_ctrl = TIMER1_DIVIDE_BY_16 | TIMER1_ENABLE_TIMER;

	}

