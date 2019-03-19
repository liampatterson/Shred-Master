/**
 * This is a very small example that shows how to use
 * === OUTPUT COMPARE and INPUT CAPTURE ===
 * The system uses hardware to generate precisely timed
 * pulses, then uses input capture to compare the capture period
 * to the generation period for accuracy
 *
 * There is a capture time print-summary thread
 * There is a one second timer tick thread
 * 
 * -- Pin RB5 and RB9 are output compare outputs
 * -- Pin RB13 is input capture input -- connect this to one of the output compares
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
#include "pt_cornell_1_2_1.h"

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

//The measured rise time
unsigned int capture1;
//Indicates whether IC1 has received has sent an interrupt
int flag = 0;
// For averaging, store the previous two measurements to average with the
// most recent measurement.
float C1 = 0;
float C2 = 0;
float C3 = 0;


// == Capture 1 ISR ====================================================
void __ISR(_INPUT_CAPTURE_1_VECTOR, ipl3) C1Handler(void)
{
    // read the capture register 
    capture1 = mIC1ReadCapture();
    //capture1 = 5000000;
    // clear the interrupt flag
    mIC1ClearIntFlag();
    flag = 1;
}

// === Period print Thread ======================================================
// prints the captured period of the generated wave
static PT_THREAD (protothread_print(struct pt *pt))
{
    PT_BEGIN(pt);
      // string buffer
      //static char buffer[128];
      while(1) {
            // Wait half a second
            PT_YIELD_TIME_msec(500);
            // Set the cursor to 0, 80
            tft_setCursor(0, 80);
            // Draw the circle
            tft_fillCircle(15,80, 10, ILI9340_WHITE);// x,y,w,h,radius,color
            // Display circle for 200msec
            PT_YIELD_TIME_msec(200);
            // Erase the circle
            tft_fillCircle(15,80, 10, ILI9340_BLACK);
            // Wait 300 msec to complete 1 sec
            PT_YIELD_TIME_msec(300);
      } // END WHILE(1)
  PT_END(pt);
} // thread 4


// Measure thread
static PT_THREAD (protothread_time(struct pt *pt))
{
    PT_BEGIN(pt);
      while(1) {
        // Set RB3 as a digital output
        mPORTBSetPinsDigitalOut(BIT_3);
        // Clear the bit at RB3
        mPORTBClearBits(BIT_3);
        
        // Wait long enough to discharge the capacitor through 140 ohms
        PT_YIELD_TIME_msec(1);
        
        // Set RB3 as a digital input
        mPORTBSetPinsDigitalIn(BIT_3);
        // Start timer 2 by zeroing the timer
        WriteTimer2(0x00000000);
        
        // Wait for IC1 to detect change
        PT_YIELD_UNTIL(pt, flag);
        flag = 0;
        
        //erase the screen
        tft_setCursor(0, 40);
        tft_fillRoundRect(0,40, 240, 20, 1, ILI9340_BLACK);// x,y,w,h,radius,color
        tft_setCursor(0, 20);
        tft_fillRoundRect(0,20, 240, 20, 1, ILI9340_BLACK);
        tft_setCursor(0, 100);
        tft_fillRoundRect(0, 100, 240, 20, 1, ILI9340_BLACK);
        
        // Create the char buffer for capture1 to be stored in
        static char buffer[128];
        
        // Set the cursor to 0, 40
        tft_setCursor(0, 40);
        // Set text color to white and fontsize to 2
        tft_setTextColor(ILI9340_WHITE);  tft_setTextSize(2);
        // Turn the text into a char
        sprintf(buffer, "capture1 = %lu", capture1);
        // Display capture1 and its value
        tft_writeString(buffer);
                
        // Store the third measurement for averaging
        C3 = C2;
        // Store the second measurement for averaging
        C2 = C1;
        // Read current capacitor value
        C1 = 0.00214967*capture1 + 0.0091747672;
  
        // Average the three capacitor
        float C = ( C1 + C2 + C3 ) / 3;
        
        
        // Set the cursor to 0, 20
        tft_setCursor(0, 20);
        // Set text color to white and text size to 2
        tft_setTextColor(ILI9340_WHITE);  tft_setTextSize(2);
        
        // If capacitance is greater than 0.8 nF, display value of cap
        if ( C > 0.8 ) {
            static char buffer1[128];
            sprintf(buffer1, "C = %.1f nF", C);
            tft_writeString(buffer1);
        }
        // If capacitance is less than 0.8 nF, assume no cap present
        else {
            static char buffer2[128];
            tft_writeString("No Capacitor Present");
        }
          
        // print and measure every 200 mSec
        PT_YIELD_TIME_msec(200) ;
        
      } // END WHILE(1)
    PT_END(pt);
}


/*
// === Main  ======================================================
int main(void) {
//set up compare 1
CMP1Open(CMP_ENABLE | CMP_OUTPUT_ENABLE | CMP1_NEG_INPUT_IVREF);
PPSOutput(4, RPB9, C1OUT); //pin18
mPORTBSetPinsDigitalIn(BIT_3); //Set port as input (pin 7 is RB3)

// set up timer 2
//Prescaler set to 4!!
OpenTimer2( T2_ON | T2_PS_1_4 | T2_SOURCE_INT, 0xffffffff );



// === set up input capture ================================
OpenCapture1(  IC_EVERY_RISE_EDGE | IC_INT_1CAPTURE | IC_TIMER2_SRC | IC_ON | IC_CAP_16BIT );
// turn on the interrupt so that every capture can be recorded
ConfigIntCapture1(IC_INT_ON | IC_INT_PRIOR_3 | IC_INT_SUB_PRIOR_3 );
INTClearFlag(INT_IC1);

// connect PIN 24 to IC1 capture unit (connect to RB13)
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

}
*/