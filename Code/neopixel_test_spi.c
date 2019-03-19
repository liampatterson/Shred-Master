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
#define NOP asm("nop");
// wait 5 is 125 nS
#define wait125  NOP;NOP;NOP;NOP;NOP;
// wait10 is 250 nS
#define wait250  NOP;NOP;NOP;NOP;NOP; NOP;NOP;NOP;NOP;NOP;
// zero bit on time
#define wait0on wait250; NOP;NOP; //NOP;NOP;
// one bit on time
#define wait1on wait250; wait250; wait125 //NOP;NOP;NOP;
// one bit off time
#define wait1off wait250; wait125;  //NOP; //NOP;NOP;NOP;
// zero bit off time
#define wait0off wait250; wait250; //wait125; 

// number of pixels
#define NeoNum 75
unsigned char NeoGreen [NeoNum];
unsigned char NeoBlue [NeoNum];
unsigned char NeoRed [NeoNum];

// bit_test macro
#define bit_test(v,bit_num) ((v)&(1<<(bit_num)))

// === select pixel colors ===================================
// define an array of three-channel color values 0-255
char init_array[]={ 1, 4, 16, 32, 64, 128, 255, 128, 64, 32, 16, 4, 1};
//  char init_array[]={ 255,255,255,255,255,255,255,255,255,255,255,255,255 };
void NeoInit (void) {
   unsigned char NeoPixel;
   for (NeoPixel = 0; NeoPixel < NeoNum; NeoPixel++)   
   {
      if (NeoPixel < NeoNum/3)
         { NeoGreen[NeoPixel] =32; NeoBlue[NeoPixel] = 32; NeoRed[NeoPixel] = 32; }
      else if (NeoPixel < 2*NeoNum/3)
         { NeoGreen[NeoPixel] = 32; NeoBlue[NeoPixel] = 64; NeoRed[NeoPixel] = 32; }
      else  
         { NeoGreen[NeoPixel] = 32; NeoBlue[NeoPixel] = 16; NeoRed[NeoPixel] = 32; }     
   }
}

// === output one bit
void NeoBit (Bit){
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

// === animate ===============================================
int mutate_count, count_max=10, select_color;
void NeoRotate (void){
   unsigned char NeoPixel;   
   for (NeoPixel = 0; NeoPixel < NeoNum - 1; NeoPixel++)   
   {           
      NeoGreen[NeoPixel] = NeoGreen[NeoPixel + 1];
      NeoBlue[NeoPixel] = NeoBlue[NeoPixel + 1];
      NeoRed[NeoPixel] = NeoRed[NeoPixel + 1];
   }
   if (mutate_count > count_max){
       NeoGreen[NeoNum - 1]= NeoGreen[0] + (char)((rand() & 0x0f)) ; //NeoGreen[0];
       NeoBlue[NeoNum - 1] = NeoBlue[0]  + (char)((rand() & 0x0f)) ;//NeoBlue[0];
       NeoRed[NeoNum - 1] =  NeoRed[0]   + (char)((rand() & 0x0f)) ;// NeoRed[0]; 
       if((rand() & 0xff) > 245) {
           mutate_count = 0;
       }
   }
   else {
       mutate_count++;
       // threshold for new burst
       select_color = rand()&3 ;
       if(select_color==0)NeoGreen[NeoNum - 1]=0;
       if(select_color==1)NeoBlue[NeoNum - 1]=0;
       if(select_color==2)NeoRed[NeoNum - 1]=0;
       if(select_color==3) {NeoGreen[NeoNum - 1]=0;NeoBlue[NeoNum - 1]=0;NeoRed[NeoNum - 1]=0;}
   }
}

// === thread structures ============================================
// thread control structs
// note that UART input and output are threads
static struct pt pt_timer ;

// === Timer Thread =================================================
// update a 1 second tick counter
static PT_THREAD (protothread_timer(struct pt *pt)){
    PT_BEGIN(pt);
      mPORTASetPinsDigitalOut(BIT_0);    //Set port as output
      NeoInit ();  
      
      while(1) {
        // yield time 
        PT_YIELD_TIME_msec(60) ;
        NeoDraw ();
        NeoRotate ();
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

