/*
 * File:        TFT_test_BRL4_framed_spi_DMA.c
 *  DO not post -- solution for lab 3 !!!
 * Author:      Bruce Land
 * For use with Sean Carroll's Big Board
 * Adapted from:
 *              main.c by
 * Author:      Syed Tahmid Mahbub
 * Target PIC:  PIC32MX250F128B
 */

////////////////////////////////////
// clock AND protoThreads configure!
// You MUST check this file!
#include "config.h"
// threading library
#include "pt_cornell_1_2_1.h"

////////////////////////////////////
// graphics libraries
// SPI channel 1 connections to TFT
#include "tft_master.h"
#include "tft_gfx.h"
// need for rand function
#include <stdlib.h>
////////////////////////////////////
// math for sine
#include <math.h>
// === DAC control vars ===========================================
volatile SpiChannel spiChn = SPI_CHANNEL2 ;	// the SPI channel to use
volatile int spiClkDiv = 2 ; // 20 MHz max speed for this DAC
#define dmaChn 0
// table size and timer rate determine frequency
// output freq = (timer rate)/(table size)
// output valuea + control info
unsigned short DAC_data1[2] ;

/* Demo code for interfacing TFT (ILI9340 controller) to PIC32
 * The library has been modified from a similar Adafruit library
 */
// Adafruit data:
/***************************************************
  This is an example sketch for the Adafruit 2.2" SPI display.
  This library works with the Adafruit 2.2" TFT Breakout w/SD card
  ----> http://www.adafruit.com/products/1480

  Check out the links above for our tutorials and wiring diagrams
  These displays use SPI to communicate, 4 or 5 pins are required to
  interface (RST is optional)
  Adafruit invests time and resources providing this open source code,
  please support Adafruit and open-source hardware by purchasing
  products from Adafruit!

  Written by Limor Fried/Ladyada for Adafruit Industries.
  MIT license, all text above must be included in any redistribution
 ****************************************************/

// string buffer
char buffer[60];

// DAC ISR
// A-channel, 1x, active
#define DAC_config_chan_A 0b0011000000000000
// B-channel, 1x, active
#define DAC_config_chan_B 0b1011000000000000
//== Timer 2 interrupt handler ===========================================

// === thread structures ============================================
// thread control structs
// note that UART input and output are threads
static struct pt pt_timer ;

// === Timer Thread =================================================
// update a 1 second tick counter
static PT_THREAD (protothread_timer(struct pt *pt))
{
    PT_BEGIN(pt);
     tft_setCursor(0, 0);
     tft_setTextColor(ILI9340_WHITE);  tft_setTextSize(1);
     tft_writeString("   \n");
     // set up LED to blink
     mPORTASetBits(BIT_0 );	//Clear bits to ensure light is off.
     mPORTASetPinsDigitalOut(BIT_0 );    //Set port as output
     
      while(1) {
        // modulate CRC channel noise
        PT_YIELD_TIME_msec(2) ;
        DmaChnDisable(0);
        PT_YIELD_TIME_msec(60) ;
        DmaChnEnable(0);
        
        // toggle the LED on the big board
        mPORTAToggleBits(BIT_0);
        // draw sys_time
        tft_fillRoundRect(0,10, 100, 14, 1, ILI9340_BLACK);// x,y,w,h,radius,color
        tft_setCursor(0, 10);
        tft_setTextColor(ILI9340_YELLOW); tft_setTextSize(2);
        //sprintf(buffer,"%d", sys_time_seconds);
        sprintf(buffer,"%x", DAC_data1[0]);
        tft_writeString(buffer);
        // NEVER exit while
      } // END WHILE(1)
  PT_END(pt);
} // timer thread


// === Main  ======================================================
void main(void) {
 //SYSTEMConfigPerformance(PBCLK);
  
  ANSELA = 0; ANSELB = 0; 

  // === timer2 =================================
    // Set up timer2 on,  interrupts, internal clock, prescalar 1, toggle rate
    // at 30 MHz PB clock 60 counts is two microsec
    // 100 is 400 ksamples/sec  
    // 400 is 100 ksamples/sec
    // 1000 is 40 Ksamples/sec
    // 2000 is 20 ksamp/sec
    OpenTimer2(T2_ON | T2_SOURCE_INT | T2_PS_1_1, 1000);

    // set up the timer interrupt with a priority of 2
    //ConfigIntTimer2(T2_INT_ON | T2_INT_PRIOR_2);
    //mT2ClearIntFlag(); // and clear the interrupt flag
 
    // === SPI channel for DAC =================
    // divide Fpb by 2, configure the I/O ports. Not using SS in this example
    // 16 bit transfer CKP=1 CKE=1
    // possibles SPI_OPEN_CKP_HIGH;   SPI_OPEN_SMP_END;  SPI_OPEN_CKE_REV
    // For any given peripherial, you will need to match these
    // clk divider set to 2 for 20 MHz
    // FRAMED SPI mode, so SS2 is produced by the SPI hardware
    SpiChnOpen(SPI_CHANNEL2, 
            SPI_OPEN_ON | SPI_OPEN_MODE16 | SPI_OPEN_MSTEN | SPI_OPEN_CKE_REV | SPICON_FRMEN | SPICON_FRMPOL,
            2);
    // SS1 to RPB4 for FRAMED SPI
    PPSOutput(4, RPB10, SS2);
    // SDO2 (MOSI) is in PPS output group 2, could be connected to RB5 which is pin 14
    PPSOutput(2, RPB5, SDO2);
    // SCK2 is pin 26 
  // end DAC setup
    
  // === DMA setup for DAC =====================
  // transfer 2 bytes from voltage table to DAC SPI controlled by timer event
  // and repeating indefinitely
    // Open the desired DMA channel.
    // priority zero
	// Enable the AUTO option, to keep repeating the same transfer over and over.
	DmaChnOpen(dmaChn, 0, DMA_OPEN_AUTO);

	// set the transfer parameters: source & destination address, source & destination size, number of bytes per event
    // Setting the last parameter to 2 makes the DMA output two bytes/timer event
    // sine_table_size mult by 2 because the table is SHORT ints
    //DmaChnSetTxfer(dmaChn, DAC_data1, (void*)&SPI2BUF, sine_table_size*2, 2, 2);
    // just one input VALUE to see what CRC does
    DmaChnSetTxfer(dmaChn, DAC_data1, (void*)&SPI2BUF, 2, 2, 2);
    
	// set the transfer event control: what event is to start the DMA transfer
        // In this case, timer2
	DmaChnSetEventControl(dmaChn, DMA_EV_START_IRQ(_TIMER_2_IRQ));

    // set up the CRC
    //void mCrcConfigure(int polynomial, int pLen, int seed);
     //Arguments: polynomial; The generator polynomial used for the CRC calculation.
     //      pLen; the length of the CRC generator polynomial.
     //      seed; the initial seed of the CRC generator.
    //       max lengtth 16,15,13,4,1 0xa010    --- 0x1120 sounds good
    DmaCrcConfigure(0xa010, 16, 0xffff); 
     // send the data thru
    #define appendMode 1
     CrcAttachChannel(dmaChn, appendMode);
     DmaCrcEnable(1);
//     DCRCDATA=0xffff;                 // seed the CRC generator
//    DCRCXOR=0x1021;                  // Use the standard CCITT CRC 16 polynomial: X^16+X^12+X^5+1
//    DCRCCON=0x0f80;                  // CRC enabled, polynomial length 16, background mode
// CRC attached to the DMA channel 0.
     
	// once we configured the DMA channel we can enable it
	// now it's ready and waiting for an event to occur...
	DmaChnEnable(dmaChn);
    
  // === config threads ==========
  // turns OFF UART support and debugger pin, unless defines are set
  PT_setup();

  // === setup system wide interrupts  ========
  INTEnableSystemMultiVectoredInt();

  // init the threads
  PT_INIT(&pt_timer);

  // init the display
  // NOTE that this init assumes SPI channel 1 connections
  tft_init_hw();
  tft_begin();
  tft_fillScreen(ILI9340_BLACK);
  //240x320 vertical display
  tft_setRotation(0); // Use tft_setRotation(1) for 320x240

  // seed random color
  srand(1);
  
  // init a trasnfer variable for CRC input
  DAC_data1[0] = 0;
  // build the sine lookup table
   // scaled to produce values between 0 and 4095 (12 bit)
//    int i, s ;
//    for (i = 0; i < sine_table_size; i++){
//        raw_sin[i] = (int)(2047 * sin((float)i*6.283/(float)sine_table_size) + 2047); //12 bit
//        DAC_data1[i] = DAC_config_chan_A | (raw_sin[i] & 0x0fff) ;
//    }
    
  // round-robin scheduler for threads
  while (1){
      PT_SCHEDULE(protothread_timer(&pt_timer));
      }
  } // main

// === end  ======================================================

