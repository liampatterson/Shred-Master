/*
 * File:        DotStar test
 * Author:      Bruce Land
 * For use with Sean Carroll's Little Board
 */

////////////////////////////////////
// clock AND protoThreads configure!
// You MUST check this file!
#include "config.h"
// threading library
#include "pt_cornell_1_2_1.h"
#include <stdlib.h>
#include <math.h>

// APA102 datasheet:
// 32 bits of zeros is a start frame
// 32 bits of ones is a stop frame
// LED frame:
// 111_5bit_global_intensity_8bitBlue_8bitGreen_8bitRed
// so 0xff_00_ff_00 is full intnsity green
#define START_FRAME 0x00000000
#define STOP_FRAME  0xffffffff
#define PIXEL_FRAME(i,r,g,b)(0xe0000000 | (((0x1f & (i)))<<24) | ((0xff & (b))<<16) | ((0xff & (g))<<8) | (0xff & (r)))
//#define PIXEL_FRAME(i,r,g,b)(0xe0000000 | ((i)<<24) | ((b)<<16) | ((g)<<8) | (r))
#define FULL_ON 0x1e
#define HALF_ON 0x0f
#define QUAR_ON 0x07

// number of pixels
#define PixelNum 144
#define StringNum 2

typedef struct pixel pixel;
struct pixel{
    char red;
    char green;
    char blue;
    char intensity;
};
// and the whole string
pixel pixel_array[PixelNum][StringNum];

// === display the LEDs ===========================================
// copies the contents of pixel_array to SPI
void write_pixels(int string_num){ 

    
    
    int i;
    if (string_num==1){
        // start frame
        WriteSPI1(START_FRAME);
        // wait for end of transaction
        while (SPI1STATbits.SPIBUSY); 
        //payload
        for (i=0; i<PixelNum; i++){
            WriteSPI1(PIXEL_FRAME(pixel_array[i][string_num].intensity, 
                                  pixel_array[i][string_num].red, 
                                  pixel_array[i][string_num].green, 
                                  pixel_array[i][string_num].blue));
            // wait for end of transaction
            while (SPI1STATbits.SPIBUSY); 
        }
        //stop frame
        WriteSPI1(STOP_FRAME);
        // wait for end of transaction
        while (SPI1STATbits.SPIBUSY); 
    }
    
    if (string_num==2){
        // start frame
        WriteSPI2(START_FRAME);
        // wait for end of transaction
        while (SPI2STATbits.SPIBUSY); 
        //payload
        for (i=0; i<PixelNum; i++){
            WriteSPI2(PIXEL_FRAME(pixel_array[i][string_num].intensity, 
                                  pixel_array[i][string_num].red, 
                                  pixel_array[i][string_num].green, 
                                  pixel_array[i][string_num].blue));
            // wait for end of transaction
            while (SPI2STATbits.SPIBUSY); 
        }
        //stop frame
        WriteSPI2(STOP_FRAME);
        // wait for end of transaction
        while (SPI2STATbits.SPIBUSY); 
    }
}

// === write a RGBI value to the pixel array =======================
void set_pixel_rgb(int string_num, int i, char r, char g, char b, char intensity){
    if (i<0 || i>=PixelNum) return ;
    pixel_array[i][string_num].intensity = intensity  ;  //enforce max 
    pixel_array[i][string_num].red = r   ;
    pixel_array[i][string_num].green = g  ;
    pixel_array[i][string_num].blue = b ;
}

// === read a RGBI value from the pixel array =======================
void get_pixel_rgb(int string_num, int i, char* r, char* g, char* b, char* intensity){
    if (i<0 || i>=PixelNum) return ;
    *intensity = pixel_array[i][string_num].intensity    ;  
    *r = pixel_array[i][string_num].red  ;
    *g = pixel_array[i][string_num].green ;
    *b = pixel_array[i][string_num].blue  ;
}

// === write a HSVI value to the pixel array =======================
void set_pixel_hsv(int string_num, int i, float h, float s, float v, char intensity){
    float C, X, m, rp, gp, bp ;
    unsigned char r, g, b ;
    // index range check
    if (i<0 || i>=PixelNum) return ;
    // hsv to rgb conversion from
    // http://www.rapidtables.com/convert/color/hsv-to-rgb.htm
    C = v * s;
    //X = C * (1 - abs((int)(h/60)%2 - 1));
    // (h/60) mod 2  = (h/60 - (int)(h/60))
    X = C * (1.0 - fabsf(fmodf(h/60.0, 2.0) - 1.));
    m = v - C;
    if (h<10)                    { rp = 0; gp = 0; bp = 0;}
    else if ((10<=h) && (h<60))  { rp = C; gp = X; bp = 0;}
    else if ((60<=h) && (h<120)) { rp = X; gp = C; bp = 0;}
    else if ((120<=h) && (h<180)){ rp = 0; gp = C; bp = X;}
    else if ((180<=h) && (h<240)){ rp = 0; gp = X; bp = C;}
    else if ((240<=h) && (h<300)){ rp = X; gp = 0; bp = C;}
    else if ((300<=h) && (h<360)){ rp = C; gp = 0; bp = X;}
    else                         { rp = 1; gp = 0; bp = 0;}
    
    r = (unsigned char)((rp+m)*255) ;
    g = (unsigned char)((gp+m)*255) ;
    b = (unsigned char)((bp+m)*255) ;
            
    pixel_array[i][string_num].intensity = intensity  ;  //enforce max 
    pixel_array[i][string_num].red = r   ;
    pixel_array[i][string_num].green = g  ;
    pixel_array[i][string_num].blue = b  ;
}
// === ADC read in ISR ==========================================
volatile int ADC_value;
volatile float ADC_avg1, ADC_scaled1, ADC_net1;
volatile float ADC_avg2, ADC_scaled2, ADC_net2;
volatile int sample_toggle=1;
// scale factor for nonlinear filter threshold
#define beat_override 1.0
// Timer 2 interrupt handler ///////
// ipl2 means "interrupt priority level 2"
// ASM output is 47 instructions for the ISR
void __ISR(_TIMER_2_VECTOR, ipl2) Timer2Handler(void)
{
    // clear the interrupt flag
    mT2ClearIntFlag();
    // read the ADC AN4 (RB2))
    if (sample_toggle == 1){
        // read the first buffer position
        ADC_value = ReadADC10(0) ;   
        // compute the DC value to subtract off ising 1000 points
        ADC_avg1 = (float)ADC_value * 0.001 + 0.999 * ADC_avg1;
        // The next lines are a nonlinear low pass filter
        // The absolute value of the amplitude is low passed --
        // BUT any new value higher than the running low pass value
        // is immediately output
        // 100 point low pass corresponds to about 80/sec
        ADC_net1 = fabsf((float)ADC_value - ADC_avg1) ;
        ADC_scaled1 = ADC_net1 * 0.02 + 0.98 * ADC_scaled1;
        if (ADC_net1 > beat_override * ADC_scaled1) ADC_scaled1 = ADC_net1;
        SetChanADC10( ADC_CH0_NEG_SAMPLEA_NVREF | ADC_CH0_POS_SAMPLEA_AN5 );
        AcquireADC10();
        sample_toggle = 2;
    }
    else {
        // read AN5 (RB3))
        ADC_value = ReadADC10(0) ;   
        // compute the DC value to subtract off ising 1000 points
        ADC_avg2 = (float)ADC_value * 0.001 + 0.999 * ADC_avg2;
        // The next lines are a nonlinear low pass filter
        // The absolute value of the amplitude is low passed --
        // BUT any new value higher than the running low pass value
        // is immediately output
        // 100 point low pass corresponds to about 80/sec
        ADC_net2 = fabsf((float)ADC_value - ADC_avg2) ;
        ADC_scaled2 = ADC_net2 * 0.02 + 0.98 * ADC_scaled2;
        if (ADC_net2 > beat_override * ADC_scaled2) ADC_scaled2 = ADC_net2;
        SetChanADC10( ADC_CH0_NEG_SAMPLEA_NVREF | ADC_CH0_POS_SAMPLEA_AN4 );
        AcquireADC10();
        sample_toggle = 1;
    }
}
// === thread structures ============================================
// thread control structs
// note that UART input and output are threads
static struct pt pt_timer ;


// === Timer Thread =================================================
// update a 1 second tick counter
int position=0, dir=1;
// 40/second -- 20 mS may be better
#define sample_time 25

static PT_THREAD (protothread_timer(struct pt *pt)){
    PT_BEGIN(pt);
      static int i, string_num ;
      static float h, s, v;
      static char r, g ,b, intensity;
      static char r0, g0, b0, inten0 ;
      static float ADC_final1, ADC_final2;
     
      while(1) {
        // yield time 
        PT_YIELD_TIME_msec(sample_time) ;
        // DDS phase incrementers
        
        // Play with the ADC output
        ADC_final1 = ADC_scaled1 * 4;
        if(ADC_final1>359) ADC_final1 = 10; //out of range value renders as "black"
        if(ADC_final1<0) ADC_final1 = 0;
        // and set pixel zero to the ADC
        set_pixel_hsv(1, 0, ADC_final1, 1.0, 1.0, HALF_ON);
        
        // move the pattern up the array from 0 
        for(i=PixelNum; i>=1; i--){
            get_pixel_rgb(1, i-1, &r0, &g0, &b0, &inten0);
            set_pixel_rgb(1, i, r0, g0, b0, inten0);        
        }
        get_pixel_rgb(1, PixelNum-1, &r0, &g0, &b0, &inten0);
        if ((r0>20) && (g0<80)) {
            set_pixel_hsv(1, PixelNum-1, 0.0, 0.0, 1.0, FULL_ON);
            set_pixel_hsv(1, PixelNum-2, 0.0, 0.0, 1.0, FULL_ON);
            set_pixel_hsv(1, PixelNum-3, 0.0, 0.0, 1.0, FULL_ON);
            set_pixel_hsv(1, PixelNum-4, 0.0, 0.0, 1.0, HALF_ON);
        }
        write_pixels(2);
        
        // Play with the ADC output
        ADC_final2 = ADC_scaled2 * 4;
        if(ADC_final2>359) ADC_final2 = 10; //out of range value renders as "red"
        if(ADC_final2<0) ADC_final2 = 0;
        // and set pixel zero to the ADC
        set_pixel_hsv(2, 0, ADC_final2, 1.0, 1.0, HALF_ON);
        
        // move the pattern up the array from 0 
        for(i=PixelNum; i>=1; i--){
            get_pixel_rgb(2, i-1, &r0, &g0, &b0, &inten0);
            set_pixel_rgb(2, i, r0, g0, b0, inten0);        
        }
        get_pixel_rgb(2, PixelNum-1, &r0, &g0, &b0, &inten0);
        if ((r0>20) && (g0<80)) {
            set_pixel_hsv(2, PixelNum-1, 0.0, 0.0, 1.0, FULL_ON);
            set_pixel_hsv(2, PixelNum-2, 0.0, 0.0, 1.0, FULL_ON);
            set_pixel_hsv(2, PixelNum-3, 0.0, 0.0, 1.0, FULL_ON);
            set_pixel_hsv(2, PixelNum-4, 0.0, 0.0, 1.0, HALF_ON);
        }
        write_pixels(1);
       
      } // END WHILE(1)
  PT_END(pt);
} // timer thread

// === Main  ======================================================
void main(void) {
  
  // === config threads ==========
  // turns OFF UART support and debugger pin, unless defines are set
  PT_setup();

  // === timer interrupt //////////////////////////
        // Set up timer2 on,  interrupts, internal clock, prescalar 1, toggle rate
        // 8 kHz i2 5000 cycles
        // run at 2500 and alternate samples on left/right channels
        OpenTimer2(T2_ON | T2_SOURCE_INT | T2_PS_1_1, 2500);

        // set up the timer interrupt with a priority of 2
         ConfigIntTimer2(T2_INT_ON | T2_INT_PRIOR_2);
        mT2ClearIntFlag(); // and clear the interrupt flag
        
  // === setup system wide interrupts  ========
  INTEnableSystemMultiVectoredInt();

    // divide Fpb by 2, configure the I/O ports. Not using SS in this example
    // 16 bit transfer CKP=1 CKE=1
    // possibles SPI_OPEN_CKP_HIGH;   SPI_OPEN_SMP_END;  SPI_OPEN_CKE_REV
    // For any given peripherial, you will need to match these
    SpiChnOpen(2, SPI_OPEN_ON | SPI_OPEN_MODE32 | SPI_OPEN_MSTEN | SPICON_CKP, 4);
    // SCK2 is pin 26 
    // SDO2 (MOSI) is in PPS output group 2, could be connected to RB5 which is pin 14
    PPSOutput(2, RPB5, SDO2);
    // Channel 1
     SpiChnOpen(1, SPI_OPEN_ON | SPI_OPEN_MODE32 | SPI_OPEN_MSTEN | SPICON_CKP, 4);
    // SCK2 is pin 26 
    // SDO2 (MOSI) is in PPS output group 2, could be connected to RB5 which is pin 14
    PPSOutput(2, RPB1, SDO1);
  
    // define setup parameters for OpenADC10
	// Turn module on | ouput in integer | trigger mode auto | enable autosample
    // ADC_CLK_AUTO -- Internal counter ends sampling and starts conversion (Auto convert)
    // ADC_AUTO_SAMPLING_ON -- Sampling begins immediately after last conversion completes; SAMP bit is automatically set
    // ADC_AUTO_SAMPLING_OFF -- Sampling begins with AcquireADC10();
    #define PARAM1  ADC_FORMAT_INTG16 | ADC_CLK_AUTO | ADC_AUTO_SAMPLING_OFF 

    // define setup parameters for OpenADC10
	// ADC ref external  | disable offset test | disable scan mode | do 1 sample | use single buf | alternate mode off
	#define PARAM2  ADC_VREF_AVDD_AVSS | ADC_OFFSET_CAL_DISABLE | ADC_SCAN_OFF | ADC_SAMPLES_PER_INT_1 | ADC_ALT_BUF_OFF | ADC_ALT_INPUT_OFF
        //
	// Define setup parameters for OpenADC10
    // use peripherial bus clock | set sample time | set ADC clock divider
    // ADC_CONV_CLK_Tcy2 means divide CLK_PB by 2 (max speed)
    // ADC_SAMPLE_TIME_5 seems to work with a source resistance < 1kohm
    #define PARAM3 ADC_CONV_CLK_PB | ADC_SAMPLE_TIME_5 | ADC_CONV_CLK_Tcy2 //ADC_SAMPLE_TIME_15| ADC_CONV_CLK_Tcy2

	// define setup parameters for OpenADC10
	// set AN4 and  as analog inputs
	#define PARAM4	ENABLE_AN4_ANA | ENABLE_AN5_ANA

	// define setup parameters for OpenADC10
	// do not assign channels to scan
	#define PARAM5	SKIP_SCAN_ALL

	// use ground as neg ref for A | use AN4 for input A     
	// configure to sample AN4 
	SetChanADC10( ADC_CH0_NEG_SAMPLEA_NVREF | ADC_CH0_POS_SAMPLEA_AN4 ); // configure to sample AN4 
	OpenADC10( PARAM1, PARAM2, PARAM3, PARAM4, PARAM5 ); // configure ADC using the parameters defined above

	EnableADC10(); // Enable the ADC
    
   
  // init the threads
  PT_INIT(&pt_timer);
  
  // round-robin scheduler for threads
  while (1){
      PT_SCHEDULE(protothread_timer(&pt_timer));
  }
} // main

// === end  ======================================================

