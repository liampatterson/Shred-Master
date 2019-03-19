/*
 * File:        Neopixel
 * Author:      Bruce Land
 * For use with Sean Carroll's Big Board
 */

////////////////////////////////////
// clock AND protoThreads configure!
// You MUST check this file!
#include "config.h"
// threading library
#include "pt_cornell_1_2_1.h"
#include <stdlib.h>

// data sheet gives zero-bit as high for 350 nS, low for 800 nS +/- 150 nS
// data sheet gives one-bit as high for 700 nS, low for 600 nS +/- 150 nS
// data sheet gives reset hold line low for 50 microSec
// at 40 MHz 350/25=14 cycles 800/25=32 cycles
// at 40 MHz 700/25=28 cycles 600/25=24 cycles
// BUT the durations had to be hand-tuned!
#define NOP asm("nop");
// wait 5 is 125 nS
#define wait125  NOP;NOP;NOP;NOP;NOP;
// wait10 is 250 nS
#define wait250  NOP;NOP;NOP;NOP;NOP; NOP;NOP;NOP;NOP;NOP;
// zero bit on time
#define wait0on wait250; NOP;//NOP; //NOP;NOP;
// one bit on time
#define wait1on wait250; wait250; wait125 //NOP;//NOP;NOP;
// one bit off time
#define wait1off wait250; wait125;  //NOP; //NOP;NOP;NOP;
// zero bit off time
#define wait0off wait250; wait125; NOP;NOP;NOP;NOP;NOP;

// bit_test macro
// returns NON-ZERO if TRUE
#define bit_test(v,bit_num) ((v)&(1<<(bit_num)))

// === select pixel colors ===================================
// number of pixels
#define NeoNum 60
#define pulse_size 17
unsigned char NeoGreen [NeoNum];
unsigned char NeoBlue [NeoNum];
unsigned char NeoRed [NeoNum];
unsigned char NeoGreenTemplate [NeoNum];
unsigned char NeoBlueTemplate [NeoNum];
unsigned char NeoRedTemplate [NeoNum];
// define an array of three-channel color values 0-255
char init_array[]={ 1, 2, 4, 8, 16, 32, 64, 128, 255, 128, 64, 32, 16, 8, 4,2, 1};
//  char init_array[]={ 255,255,255,255,255,255,255,255,255,255,255,255,255 };
void NeoInit (void) {
   unsigned char NeoPixel;
   for (NeoPixel = 0; NeoPixel < NeoNum; NeoPixel++)   
   {
      if (NeoPixel < 17)
         { NeoGreenTemplate[NeoPixel] =init_array[NeoPixel]; NeoBlueTemplate[NeoPixel] = 0; NeoRedTemplate[NeoPixel] = 0; }
      else 
         { NeoGreenTemplate[NeoPixel] = 8; NeoBlueTemplate[NeoPixel] = 0; NeoRedTemplate[NeoPixel] = 8; }
   }
}

// === output one bit ======================================
void NeoBit (char Bit){
   if (Bit == 0)
   { mPORTASetBits(BIT_0);wait0on;mPORTAClearBits(BIT_0);wait0off }  //wait32;// Bit '0'   
   else
   { mPORTASetBits(BIT_0);wait1on;mPORTAClearBits(BIT_0);wait1off; }  //wait24;// Bit '1'   
}

// === draw pixel colors ===================================
void NeoDraw (void){
   unsigned char NeoPixel;
   signed char BitCount;
   for (NeoPixel = 0; NeoPixel < NeoNum; NeoPixel++)
   {    
      for (BitCount = 7; BitCount >= 0; BitCount--)      
         NeoBit(bit_test(NeoGreen[NeoPixel], BitCount));      
      for (BitCount = 7; BitCount >= 0; BitCount--)           
         NeoBit(bit_test(NeoRed[NeoPixel], BitCount));            
      for (BitCount = 7; BitCount >= 0; BitCount--)      
         NeoBit(bit_test(NeoBlue[NeoPixel], BitCount));      
   }
   mPORTAClearBits(BIT_0);
}

// === move ==================================================

void NeoMove (int position){
    unsigned char NeoPixel;   
    for (NeoPixel = 0; NeoPixel < NeoNum-1; NeoPixel++)   
    {           
       NeoGreen[(NeoPixel+position)%NeoNum] = NeoGreenTemplate[NeoPixel];
       NeoBlue[(NeoPixel+position)%NeoNum] = NeoBlueTemplate[NeoPixel];
       NeoRed[(NeoPixel+position)%NeoNum] = NeoRedTemplate[NeoPixel];
    }     
}

// === thread structures ============================================
// thread control structs
// note that UART input and output are threads
static struct pt pt_timer ;


// === Timer Thread =================================================
// update a 1 second tick counter
int position=0, dir=1;

static PT_THREAD (protothread_timer(struct pt *pt)){
    PT_BEGIN(pt);
    static  unsigned char NeoPixel, color_change, color_seq ;
      //Set port as output
      mPORTASetPinsDigitalOut(BIT_0);  
      
      // Set up color Template
      NeoInit();  
      
      while(1) {
        // yield time 
        PT_YIELD_TIME_msec(30) ;
        
        
        // mutate color template
        //color_change++;
        //if((color_change & 0x3f) == 0){
        //if((color_change & 0x0f) == 0){
          //  color_seq++ ;
        for (NeoPixel = 0; NeoPixel < NeoNum; NeoPixel++)   
        {
            NeoGreenTemplate[NeoPixel] =  (char)(128. * ((float)NeoPixel)/NeoNum) ;
            NeoBlueTemplate[NeoPixel]  =  (char)(128. * (1-((float)NeoPixel)/NeoNum)) ;
            NeoRedTemplate[NeoPixel]   =  0;               
        }
        
        NeoMove(0);
        NeoDraw();
      } // END WHILE(1)
  PT_END(pt);
} // timer thread

// === Main  ======================================================
void main(void) {
  
  // === config threads ==========
  // turns OFF UART support and debugger pin, unless defines are set
  PT_setup();

  // === setup system wide interrupts  ========
  INTEnableSystemMultiVectoredInt();

  // init the threads
  PT_INIT(&pt_timer);
  
  // round-robin scheduler for threads
  while (1){
      PT_SCHEDULE(protothread_timer(&pt_timer));
  }
} // main

// === end  ======================================================

