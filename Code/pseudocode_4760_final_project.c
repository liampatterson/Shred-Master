#include song.h
#include "math.h"

////////////////////////////////////
// DAC ISR
// A-channel, 1x, active
#define DAC_config_chan_A 0b0011000000000000
// B-channel, 1x, active
#define DAC_config_chan_B 0b1011000000000000

// DDS sine table
#define sine_table_size 256
int sin_table[sine_table_size];

// Frequency to Output
//static float Fout = 400.0;
// Sampling frequency
#define Fs 50000.0
#define two32 4294967296.0 // 2^32

int R = 0, current_note = 0, Playtime = 0;


// search note to play in lookup table
void __ISR(_TIMER_2_VECTOR, ipl2) Timer2Handler(void)
{
    // 74 cycles to get to this point from timer event
    mT2ClearIntFlag();
    
    // check if we need to ramp up
    if ( ramp_up  ) {
        // if so, increment R from 0
        R += 1;
    }
    
    // check if we need to ramp down
    if ( ramp_down  ) {
        // if so, decrement R to 0
        R -= 1;
    }
    
    // continue incrementing through sine tables
    phase_accum_main1 += phase_incr_main1 ;
    phase_accum_main2 += phase_incr_main2 ;
    // set DAC_data to both sine tables combined, ensuring values to DAC 0-4096
    DAC_data = (((sin_table[phase_accum_main1>>24] + sin_table[phase_accum_main2>>24])*R)>>8);
    
    // === Channel A =============
    // CS low to start transaction
     mPORTBClearBits(BIT_4); // start transaction
    // test for ready
    //while (TxBufFullSPI2());
    // write to spi2
    WriteSPI2( DAC_config_chan_A | (DAC_data+2048 ));
    while (SPI2STATbits.SPIBUSY); // wait for end of transaction
     // CS high
    mPORTBSetBits(BIT_4); // end transaction
} // end IRS TIMER2


// look up table for frequency
void song() {
	note_to_play = song[current_note][0];
	Fs = lookuptable[note_to_play];
	Playtime = song[current_note][1];
	
	phase_incr_main1 = (int)(freq_table[song[current_note][3]]*(float)two32/Fs);

	current_note += 1;
}


// MAIN

// steps
// lookup table for dds for frequency to step through sine table at


  	// set up DAC on big board
  	/// timer interrupt //////////////////////////
    // Set up timer2 on,  interrupts, internal clock, prescalar 1, toggle rate
    // 400 is 100 ksamples/sec at 30 MHz clock
    // 200 is 200 ksamples/sec
    
    OpenTimer2(T2_ON | T2_SOURCE_INT | T2_PS_1_1, 800);
    // set up the timer interrupt with a priority of 2
    ConfigIntTimer2(T2_INT_ON | T2_INT_PRIOR_2);
    mT2ClearIntFlag(); // and clear the interrupt flag

    // SCK2 is pin 26 
    // SDO2 (MOSI) is in PPS output group 2, could be connected to RB5 which is pin 14//
    PPSOutput(2, RPB5, SDO2);

    // control CS for DAC
    mPORTBSetPinsDigitalOut(BIT_4);
    mPORTBSetBits(BIT_4);
    
    // Set up switch input for test mode
    mPORTBSetPinsDigitalIn( BIT_13 );
    EnablePullUpB( BIT_13 );

    // divide Fpb by 2, configure the I/O ports. Not using SS in this example
    // 16 bit transfer CKP=1 CKE=1
    // possibles SPI_OPEN_CKP_HIGH;   SPI_OPEN_SMP_END;  SPI_OPEN_CKE_REV
    // For any given peripherial, you will need to match these
    // clk divider set to 2 for 20 MHz
    SpiChnOpen(SPI_CHANNEL2, SPI_OPEN_ON | SPI_OPEN_MODE16 | SPI_OPEN_MSTEN | SPI_OPEN_CKE_REV , 2);
  // end DAC setup


// generate the sine table
int ii;
for (ii = 0; ii < sine_table_size; ii++){
    sin_table[ii] = (int)(1023*sin((float)ii*6.283/(float)sine_table_size));
}
for (ii = 0; ii < 128, ii++ ) {
	freq_table[ii] = pow(double(2), (ii-49)/12) * doduble(440);
}