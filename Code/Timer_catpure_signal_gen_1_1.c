/**
 * This is a very small example that shows how to use
 * === OUTPUT COMPARE and INPUT CAPTURE ===
 * The system uses hardware to generate precisely timed
 * pulses, then uses input capture to compare the capture period
 * to the gereration period for acccuracy
 *
 * There is a capture time print-summary thread
 * There is a one second timer tick thread
 * 
 * -- Pin 14 and 18 are output compare outputs
 * -- Pin 24 is input capture input -- connect this to one of the output compares
 *
 * -- TFT LCD connections explained elsewhere
 * Modified by Bruce Land 
 * Jan 2015
 */

////////////////////////////////////
// clock AND protoThreads configure!
// You MUST check this file!
#include "config.h"
// threading library
#include "pt_cornell_1_1.h"

////////////////////////////////////
// graphics libraries
#include "tft_master.h"
#include "tft_gfx.h"
// need for rand function
#include <stdlib.h>
////////////////////////////////////

// === thread structures ============================================
// thread control structs

// note that UART input and output are threads
static struct pt pt_print, pt_time ; //, pt_input, pt_output, pt_DMA_output ;

// system 1 second interval tick
int sys_time_seconds ;

//The measured period of the wave
short capture1, last_capture1=0, capture_period=99 ;
//The actual period of the wave
int generate_period=10000 ;

// == Capture 1 ISR ====================================================
// check every cpature for consistency
void __ISR(_INPUT_CAPTURE_1_VECTOR, ipl3) C1Handler(void)
{
    capture1 = mIC1ReadCapture();
    capture_period = capture1 - last_capture1 ;
    last_capture1 = capture1 ;
    // clear the timer interrupt flag
    mIC1ClearIntFlag();
}

// === Period print Thread ======================================================
// prints the captured period of the generated wave
static PT_THREAD (protothread_print(struct pt *pt))
{
    PT_BEGIN(pt);
      // string buffer
      char buffer[128];
      tft_setCursor(0, 0);
      tft_setTextColor(ILI9340_WHITE);  tft_setTextSize(1);
      tft_writeString("Connect pin18 OR pin14 to pin24\n");
      while(1) {
            // print every 200 mSec
            PT_YIELD_TIME_msec(200) ;
            // erase
            tft_fillRoundRect(0,50, 200, 20, 1, ILI9340_BLACK);// x,y,w,h,radius,color
            // print the periods
            tft_setCursor(0, 50);
             sprintf(buffer,"gen=%d cap=%d time=%d ",
                generate_period, capture_period, sys_time_seconds);
            tft_writeString(buffer);
      } // END WHILE(1)
  PT_END(pt);
} // thread 4

// === One second Thread ======================================================
// update a 1 second tick counter
static PT_THREAD (protothread_time(struct pt *pt))
{
    PT_BEGIN(pt);

      while(1) {
            // yield time 1 second
            PT_YIELD_TIME_msec(1000) ;
            sys_time_seconds++ ;
            // NEVER exit while
      } // END WHILE(1)

  PT_END(pt);
} // 

// === Main  ======================================================

int main(void)
{
  
  // === Config timer and output compares to make pulses ========
  // set up timer2 to generate the wave period
  OpenTimer2(T2_ON | T2_SOURCE_INT | T2_PS_1_1, generate_period);
  //ConfigIntTimer2(T2_INT_ON | T2_INT_PRIOR_2);
  //mT2ClearIntFlag(); // and clear the interrupt flag

  // set up compare3 for double compare mode
  // first number is the time to clear, second is the time to set the pin
  // in this case, the end of the timer period and 50% of the timer period
  OpenOC3(OC_ON | OC_TIMER2_SRC | OC_CONTINUE_PULSE , generate_period, generate_period>>1); //
  // OC3 is PPS group 4, map to RPB9 (pin 18)
  PPSOutput(4, RPB9, OC3);
 // mPORTASetPinsDigitalOut(BIT_3);    //Set port as output -- not needed

  // set pulse to go high at 1/4 of the timer period and drop again at 1/2 the timer period
  OpenOC2(OC_ON | OC_TIMER2_SRC | OC_CONTINUE_PULSE, generate_period>>1, generate_period>>2);
  // OC2 is PPS group 2, map to RPB5 (pin 14)
  PPSOutput(2, RPB5, OC2);
 // mPORTBSetPinsDigitalOut(BIT_5); //Set port as output -- not needed

  // === Config timer3 free running ==========================
  // set up timer3 as a souce for input capture
  // and let it overflow for contunuous readings
  OpenTimer3(T3_ON | T3_SOURCE_INT | T3_PS_1_1, 0xffff);

  // === set up input capture ================================
  OpenCapture1(  IC_EVERY_RISE_EDGE | IC_INT_1CAPTURE | IC_TIMER3_SRC | IC_ON );
  // turn on the interrupt so that every capture can be recorded
  ConfigIntCapture1(IC_INT_ON | IC_INT_PRIOR_3 | IC_INT_SUB_PRIOR_3 );
  INTClearFlag(INT_IC1);
  // connect PIN 24 to IC1 capture unit
  PPSInput(3, IC1, RPB13);

  // init the display
  tft_init_hw();
  tft_begin();
  tft_fillScreen(ILI9340_BLACK);
  //240x320 vertical display
  tft_setRotation(0); // Use tft_setRotation(1) for 320x240
  tft_setCursor(0, 0);
  
  // === config the uart, DMA, vref, timer5 ISR ===========
  PT_setup();

  // === setup system wide interrupts  ====================
  INTEnableSystemMultiVectoredInt();
  
  // === now the threads ===================================
  // init the threads
  PT_INIT(&pt_print);
  PT_INIT(&pt_time);

  // schedule the threads
  while(1) {
    PT_SCHEDULE(protothread_print(&pt_print));
    PT_SCHEDULE(protothread_time(&pt_time));
  }
} // main
