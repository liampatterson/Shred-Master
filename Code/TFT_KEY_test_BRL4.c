/*
 * File:        TFT, keypad, DAC, LED test
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
#include <math.h>
////////////////////////////////////

////////////////////////////////////
// pullup/down macros for keypad
// PORT B
#define EnablePullDownB(bits) CNPUBCLR=bits; CNPDBSET=bits;
#define DisablePullDownB(bits) CNPDBCLR=bits;
#define EnablePullUpB(bits) CNPDBCLR=bits; CNPUBSET=bits;
#define DisablePullUpB(bits) CNPUBCLR=bits;
//PORT A
#define EnablePullDownA(bits) CNPUACLR=bits; CNPDASET=bits;
#define DisablePullDownA(bits) CNPDACLR=bits;
#define EnablePullUpA(bits) CNPDACLR=bits; CNPUASET=bits;
#define DisablePullUpA(bits) CNPUACLR=bits;
////////////////////////////////////

////////////////////////////////////
// some precise, fixed, short delays
// to use for extending pulse durations on the keypad
// if behavior is erratic
#define NOP asm("nop");
// 1/2 microsec
#define wait20 NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;
// one microsec
#define wait40 wait20;wait20;
////////////////////////////////////

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
volatile char buffer[60];
volatile char buffer2[60];
volatile char buffer3[60];
volatile char buffer4[60];
volatile char buffer5[60];

// Type for state machine
typedef enum {
        NoPush,
        MaybePush,
        Pushed,
        MaybeNoPush
    } State;
    
int stored_nums[13];
int pushnum = -1;
int keyvalue = 0;
int press;

int num_to_play;


int ramp_up = 0;
int ramp_down = 0;
int R = 0;
int i;


////////////////////////////////////
// DAC ISR
// A-channel, 1x, active
#define DAC_config_chan_A 0b0011000000000000
// B-channel, 1x, active
#define DAC_config_chan_B 0b1011000000000000

// DAC ISR
// A-channel, 1x, active
//#define DAC_config_chan_A 0b0011000000000000

// DDS sine table
#define sine_table_size 256
int sin_table[sine_table_size];

// Frequency to Output
//static float Fout = 400.0;
#define Fs 50000.0
#define two32 4294967296.0 // 2^32 

//== Timer 2 interrupt handler ===========================================
// actual scaled DAC 
volatile unsigned int DAC_data;
// the DDS units:
volatile unsigned int phase_accum_main, phase_incr_main=400.0*two32/Fs ;//
volatile unsigned int phase_accum_main2, phase_incr_main2=400.0*two32/Fs;
volatile unsigned int phase_accum_main1, phase_incr_main1=400.0*two32/Fs;
// profiling of ISR
volatile int isr_time;

int freqs_to_play[24] = {1336, 941, 1209, 697, 1336, 697, 1477, 697, 1209, 770,
        1336, 770, 1477, 770, 1209, 852, 1336, 852, 1477, 852, 1209, 941, 1477,
        941};

int test_mode_freqs[7] = {697, 770, 852, 941, 1209, 1336, 1477};


//=============================
void __ISR(_TIMER_2_VECTOR, ipl2) Timer2Handler(void)
{
        // 74 cycles to get to this point from timer event
    mT2ClearIntFlag();
    
    // the led
    //mPORTAToggleBits(BIT_0);
    /*
    // main DDS phase
    phase_accum_main += phase_incr_main  ;
    //sine_index = phase_accum_main>>24 ;
    DAC_data = sin_table[phase_accum_main>>24]  ;
    // now the 90 degree data
    */
    if ( ramp_up  ) {
        R += 1;
    }
    
    if ( ramp_down  ) {
        R -= 1;
    }
    
    
    phase_accum_main1 += phase_incr_main1 ;
    phase_accum_main2 += phase_incr_main2 ;
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
    isr_time = ReadTimer2() ; // - isr_time;
} // end ISR TIMER2


/*
 // === set parameters ======================================================
// update a 1 second tick counter
static float Fout = 1336.0;
static PT_THREAD (protothread_param(struct pt *pt))
{
    PT_BEGIN(pt);

      while(1) {
            // 
            PT_YIELD_TIME_msec(100) ;

            // step the frequency between 400 and 4000 Hz
            //Fout = Fout * 1.05;
            //if (Fout > 4000) Fout = 400; // Hz
            phase_incr_main = (int)(Fout*(float)two32/Fs);
            tft_fillRoundRect(0,200, 100, 14, 1, ILI9340_BLACK);// x,y,w,h,radius,color
            tft_setCursor(0, 200);
            sprintf(buffer4, "%d", phase_incr_main);
            tft_writeString(buffer4);
            // NEVER exit while
      } // END WHILE(1)
  PT_END(pt);
} // thread 4*/

/*
void Playback( int numbers_to_play, int length, int which_to_play ){
    //playback all numbers passed in
    if ( which_to_play == -1 ){
        int i = 0;
        while ( i < length - 1 ) {
            num_to_play = numbers_to_play[i];
            ramp_up = 1;
        }
    }

}
*/

// === thread structures ============================================
// thread control structs
// note that UART input and output are threads
static struct pt pt_timer, pt_color, pt_anim, pt_key; ;

// system 1 second interval tick
int sys_time_seconds ;

// === Timer Thread =================================================
// update a 1 second tick counter
static PT_THREAD (protothread_timer(struct pt *pt))
{
    PT_BEGIN(pt);
     tft_setCursor(0, 0);
     tft_setTextColor(ILI9340_WHITE);  tft_setTextSize(1);
     tft_writeString("Time in seconds since boot\n");
     // set up LED to blink
     mPORTASetBits(BIT_0 );	//Clear bits to ensure light is off.
     mPORTASetPinsDigitalOut(BIT_0 );    //Set port as output
      while(1) {
        // yield time 1 second
        PT_YIELD_TIME_msec(1000) ;
        sys_time_seconds++ ;
        // toggle the LED on the big board
        //mPORTAToggleBits(BIT_0);
        // draw sys_time
        tft_fillRoundRect(0,10, 100, 14, 1, ILI9340_BLACK);// x,y,w,h,radius,color
        tft_setCursor(0, 10);
        tft_setTextColor(ILI9340_YELLOW); tft_setTextSize(2);
        sprintf(buffer,"%d", sys_time_seconds);
        tft_writeString(buffer);
        // NEVER exit while
      } // END WHILE(1)
  PT_END(pt);
} // timer thread

//static float Fout = 400.0;
/*
// === set parameters ======================================================
// update a 1 second tick counter

static PT_THREAD (protothread_param(struct pt *pt))
{
    PT_BEGIN(pt);

      while(1) {
            // 
            PT_YIELD_TIME_msec(100) ;

            // step the frequency between 400 and 4000 Hz
            //Fout = Fout * 1.05;
            //if (Fout > 4000) Fout = 400; // Hz
            phase_incr_main = (int)(Fout*(float)two32/Fs);
            // NEVER exit while
      } // END WHILE(1)
  PT_END(pt);
} // thread 4*/

// === Main  ======================================================
// set up UART, timer2, threads
// then schedule them as fast as possible



//Keypad Thread
static PT_THREAD (protothread_key_state(struct pt *pt))
{
    PT_BEGIN(pt);
    static int keypad, i, pattern;
    // order is 0 thru 9 then * ==10 and # ==11
    // no press = -1
    // table is decoded to natural digit order (except for * and #)
    // 0x80 for col 1 ; 0x100 for col 2 ; 0x200 for col 3
    // 0x01 for row 1 ; 0x02 for row 2; etc
    // 0x88 is star, 0x208 is pound
    static int keytable[12]={0x108, 0x81, 0x101, 0x201, 0x82, 0x102, 0x202, 0x84, 0x104, 0x204, 0x88, 0x208};
    // init the keypad pins A0-A3 and B7-B9
    // PortA ports as digital outputs
    mPORTASetPinsDigitalOut(BIT_0 | BIT_1 | BIT_2 | BIT_3);    //Set port as output
    // PortB as inputs
    mPORTBSetPinsDigitalIn(BIT_7 | BIT_8 | BIT_9);    //Set port as input
    // and turn on pull-down on inputs
    EnablePullDownB( BIT_7 | BIT_8 | BIT_9);
    


    static State PushState = NoPush;
    
    
    while(1){
        // yield time
        PT_YIELD_TIME_msec(10);
        //tft_fillRoundRect(30,200, 100, 14, 1, ILI9340_BLACK);// x,y,w,h,radius,color
        //tft_setTextColor(ILI9340_WHITE);  tft_setTextSize(2);
        //tft_setCursor(30, 200);
        //tft_writeString("In thread");
        
        static int test_mode;
        
        test_mode = mPORTBReadBits( BIT_13 ) >> 13;
        tft_fillRoundRect(30,160, 100, 14, 1, ILI9340_BLACK);// x,y,w,h,radius,color
        tft_setTextColor(ILI9340_WHITE);  tft_setTextSize(2);
        tft_setCursor(30, 160);
        sprintf(buffer3, "%d", test_mode);
        tft_writeString(buffer3);
        
        
        // read each row sequentially
        mPORTAClearBits(BIT_0 | BIT_1 | BIT_2 | BIT_3);
        pattern = 1; mPORTASetBits(pattern);
   
        for (i=0; i<4; i++) {
            wait40 ;
            keypad  = mPORTBReadBits(BIT_7 | BIT_8 | BIT_9);
            if(keypad!=0) {keypad |= pattern ; break;}
            mPORTAClearBits(pattern);
            pattern <<= 1;
            mPORTASetBits(pattern);
        }
        
        tft_fillRoundRect(30,260, 100, 14, 1, ILI9340_BLACK);// x,y,w,h,radius,color
        tft_setCursor(30, 260);
        sprintf(buffer4, "%x", keypad);
        tft_writeString(buffer4);

        // search for keycode
        if (keypad > 0){ // then button is pushed
            for (i=0; i<12; i++){
                if (keytable[i]==keypad) {
                    press = 1;
                    keyvalue = i;
                    break;
                }
            }
            // if invalid, two button push, set to -1
            if (i==12) {
                press = 0;
            }
        }
        else press = 0; // no button pushed
        
        

        
        tft_fillRoundRect(30,220, 100, 14, 1, ILI9340_BLACK);// x,y,w,h,radius,color
        tft_setCursor(30, 220);
        sprintf(buffer5, "%d", keyvalue);
        tft_writeString(buffer5);
        
        tft_fillRoundRect(30,240, 200, 14, 1, ILI9340_BLACK);// x,y,w,h,radius,color
        
        static float Fout = 0;
        
        switch (PushState) {

           case NoPush:
              tft_setCursor(30, 240);
              tft_writeString("NoPush");
              if (press==1) {
                  PushState=MaybePush;
              }
              else {
                  phase_incr_main1 = 0;
                  phase_incr_main2 = 0;
                  R = 0;
              }
              break;

           case MaybePush:
              tft_setCursor(30, 240);
              tft_writeString("MaybePush");
              if (press) {
                 PushState=Pushed;
                 if ( test_mode ){
                     if ( keyvalue > 7 || keyvalue == 0) Fout = 0;
                     else Fout = test_mode_freqs[keyvalue - 1];
                     phase_incr_main1 = (int)(Fout*(float)two32/Fs);
                     phase_incr_main2 = (int)(Fout*(float)two32/Fs);
                     R = 256;
                 }
                 // Star
                 else{
                    if ( keyvalue == 10 ){
                        phase_incr_main1 = (int)(1209.0*(float)two32/Fs);
                        phase_incr_main2 = (int)(941.0*(float)two32/Fs);
                        ramp_up = 1;
                        R = 0;
                        PT_YIELD_TIME_msec(5);
                        ramp_up = 0;
                        PT_YIELD_TIME_msec(65);
                        ramp_down = 1;
                        PT_YIELD_TIME_msec(5);
                        ramp_down = 0;
                        phase_incr_main1 = 0;
                        phase_incr_main2 = 0;
                        pushnum = -1;
                    }
                    //Pound
                    else if ( keyvalue == 11 ){
                        phase_incr_main1 = (int)(1477.0*(float)two32/Fs);
                        phase_incr_main2 = (int)(941.0*(float)two32/Fs);
                        ramp_up = 1;
                        R = 0;
                        PT_YIELD_TIME_msec(5);
                        ramp_up = 0;
                        PT_YIELD_TIME_msec(65);
                        ramp_down = 1;
                        PT_YIELD_TIME_msec(5);
                        ramp_down = 0;
                        phase_incr_main1 = 0;
                        phase_incr_main2 = 0;
                        PT_YIELD_TIME_msec(500);
                        static int x;
                        static int loopey;
                        if ( pushnum == 12 ) loopey = 11;
                        else loopey = pushnum;
                        for( x = 0; x <= loopey; x++ ) {
                            float freq1 = freqs_to_play[stored_nums[x] * 2];
                            float freq2 = freqs_to_play[stored_nums[x] * 2 + 1];
                            phase_incr_main1 = (int)(freq1*(float)two32/Fs);
                            phase_incr_main2 = (int)(freq2*(float)two32/Fs);
                            ramp_up = 1;
                            R = 0;
                            PT_YIELD_TIME_msec(5);
                            ramp_up = 0;
                            PT_YIELD_TIME_msec(65);
                            ramp_down = 1;
                            PT_YIELD_TIME_msec(5);
                            ramp_down = 0;
                            phase_incr_main1 = 0;
                            phase_incr_main2 = 0;
                            PT_YIELD_TIME_msec(100);
                        }
                        // Playback all numbers
                        }
                    else if ( pushnum < 12 ) {
                        pushnum += 1;
                        stored_nums[pushnum] = keyvalue;
                        float freq1 = freqs_to_play[keyvalue * 2];
                        float freq2 = freqs_to_play[keyvalue * 2 + 1];
                        phase_incr_main1 = (int)(freq1*(float)two32/Fs);
                        phase_incr_main2 = (int)(freq2*(float)two32/Fs);
                        ramp_up = 1;
                        R = 0;
                        PT_YIELD_TIME_msec(5);
                        ramp_up = 0;
                        PT_YIELD_TIME_msec(65);
                        ramp_down = 1;
                        PT_YIELD_TIME_msec(5);
                       ramp_down = 0;
                        phase_incr_main1 = 0;
                        phase_incr_main2 = 0;

                    }
                    // ADD TEST MODE
                    // ADD TEST MODE
                    // ADD TEST MODE
                    else if ( pushnum >= 12 ) {
                        float freq1 = freqs_to_play[keyvalue * 2];
                        float freq2 = freqs_to_play[keyvalue * 2 + 1];
                        phase_incr_main1 = (int)(freq1*(float)two32/Fs);
                        phase_incr_main2 = (int)(freq2*(float)two32/Fs);
                        ramp_up = 1;
                        R = 0;
                        PT_YIELD_TIME_msec(5);
                        ramp_up = 0;
                        PT_YIELD_TIME_msec(65);
                        ramp_down = 1;
                        PT_YIELD_TIME_msec(5);
                        ramp_down = 0;
                        phase_incr_main1 = 0;
                        phase_incr_main2 = 0;
                    }
              }
              }
              else PushState = NoPush;
              break;

           case Pushed:
              tft_setCursor(30, 240);
              tft_writeString("Pushed");
              if (press) {
                PushState=Pushed;
                if ( test_mode ) {
                     if ( keyvalue > 7 || keyvalue == 0) Fout = 0;
                     else Fout = test_mode_freqs[keyvalue - 1];
                     phase_incr_main1 = (int)(Fout*(float)two32/Fs);
                     phase_incr_main2 = (int)(Fout*(float)two32/Fs);
                }
              }
              else PushState=MaybeNoPush;    
              break;

           case MaybeNoPush:
              if (press) PushState=Pushed; 
              else PushState=NoPush;    
              break;

        } // end case
        
        tft_fillRoundRect(30,280, 100, 14, 1, ILI9340_BLACK);// x,y,w,h,radius,color
        tft_setCursor(30, 280);
        sprintf(buffer3, "%d", pushnum);
        tft_writeString(buffer3);
        
        // draw key number
        if ( pushnum <= 11) {
            tft_fillRoundRect(0,180, 320, 40, 1, ILI9340_BLACK);// x,y,w,h,radius,color
            tft_setTextColor(ILI9340_YELLOW); tft_setTextSize(2);
            static int loopv;
            for( loopv = 0; loopv <= pushnum; loopv++ ) {
                tft_setCursor(20 + loopv*16, 200);
                sprintf(buffer2, "%d", stored_nums[loopv]);
                tft_writeString(buffer2); 
            }
        }
    }    
    PT_END(pt);
    } //end thread
 
    

/*
// === Keypad Thread =============================================
// connections:
// A0 -- row 1 -- thru 300 ohm resistor -- avoid short when two buttons pushed
// A1 -- row 2 -- thru 300 ohm resistor
// A2 -- row 3 -- thru 300 ohm resistor
// A3 -- row 4 -- thru 300 ohm resistor
// B7 -- col 1 -- internal pulldown resistor -- avoid open circuit input when no button pushed
// B8 -- col 2 -- internal pulldown resistor
// B9 -- col 3 -- internal pulldown resistor

static PT_THREAD (protothread_key(struct pt *pt))
{
    PT_BEGIN(pt);
    static int keypad, i, pattern;
    // order is 0 thru 9 then * ==10 and # ==11
    // no press = -1
    // table is decoded to natural digit order (except for * and #)
    // 0x80 for col 1 ; 0x100 for col 2 ; 0x200 for col 3
    // 0x01 for row 1 ; 0x02 for row 2; etc
    static int keytable[12]={0x108, 0x81, 0x101, 0x201, 0x82, 0x102, 0x202, 0x84, 0x104, 0x204, 0x88, 0x208};
    // init the keypad pins A0-A3 and B7-B9
    // PortA ports as digital outputs
    mPORTASetPinsDigitalOut(BIT_0 | BIT_1 | BIT_2 | BIT_3);    //Set port as output
    // PortB as inputs
    mPORTBSetPinsDigitalIn(BIT_7 | BIT_8 | BIT_9);    //Set port as input
    // and turn on pull-down on inputs
    EnablePullDownB( BIT_7 | BIT_8 | BIT_9);

      while(1) {

        // yield time
        PT_YIELD_TIME_msec(30);
        
        // read each row sequentially
        mPORTAClearBits(BIT_0 | BIT_1 | BIT_2 | BIT_3);
        pattern = 1; mPORTASetBits(pattern);
   
        for (i=0; i<4; i++) {
            wait40 ;
            keypad  = mPORTBReadBits(BIT_7 | BIT_8 | BIT_9);
            if(keypad!=0) {keypad |= pattern ; break;}
            mPORTAClearBits(pattern);
            pattern <<= 1;
            mPORTASetBits(pattern);
        }
        
        // search for keycode
        if (keypad > 0){ // then button is pushed
            for (i=0; i<12; i++){
                if (keytable[i]==keypad) break;
            }
            // if invalid, two button push, set to -1
            if (i==12) i=-1;
        }
        else i = -1; // no button pushed
        
        PT_YIELD()

        // draw key number
        tft_fillRoundRect(30,200, 100, 14, 1, ILI9340_BLACK);// x,y,w,h,radius,color
        tft_setCursor(30, 200);
        tft_setTextColor(ILI9340_YELLOW); tft_setTextSize(2);
        sprintf(buffer,"%d", i);
        if (i==10)sprintf(buffer,"*");
        if (i==11)sprintf(buffer,"#");
        tft_writeString(buffer);

        // NEVER exit while
      } // END WHILE(1)
  PT_END(pt);
} // keypad thread
 */

// === Main  ======================================================
void main(void) {
 //SYSTEMConfigPerformance(PBCLK);
  
  ANSELA = 0; ANSELB = 0; 

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
    // SDO2 (MOSI) is in PPS output group 2, could be connected to RB5 which is pin 14
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
    
  // === config threads ==========
  // turns OFF UART support and debugger pin, unless defines are set
  PT_setup();

  // === setup system wide interrupts  ========
  INTEnableSystemMultiVectoredInt();

  // init the threads
  PT_INIT(&pt_timer);
  PT_INIT(&pt_color);
  PT_INIT(&pt_anim);
  PT_INIT(&pt_key);

  // init the display
  // NOTE that this init assumes SPI channel 1 connections
  tft_init_hw();
  tft_begin();
  tft_fillScreen(ILI9340_BLACK);
  //240x320 vertical display
  tft_setRotation(0); // Use tft_setRotation(1) for 320x240

  // seed random color
  srand(1);
  
  // === build the sine lookup table =======
  // scaled to produce values between 0 and 4096
  int ii;
    for (ii = 0; ii < sine_table_size; ii++){
        sin_table[ii] = (int)(1023*sin((float)ii*6.283/(float)sine_table_size));
    }
  
  // round-robin scheduler for threads
  while (1){
      PT_SCHEDULE(protothread_timer(&pt_timer));
      //PT_SCHEDULE(protothread_color(&pt_color));
      //PT_SCHEDULE(protothread_anim(&pt_anim));
      PT_SCHEDULE(protothread_key_state(&pt_key));
      }
  } // main

// === end  ======================================================

