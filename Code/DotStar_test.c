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
#define PIXEL_FRAME(i,r,g,b)(0xe0000000 | (((0x1f & (i)))<<24) | ((0xff & (b)<<16)) | ((0xff & (g))<<8) | (0xff & (r)))
#define FULL_ON 0x1f
#define HALF_ON 0x0f
#define QUAR_ON 0x07

// number of pixels
#define PixelNum 150

typedef struct pixel pixel;
struct pixel{
    char red;
    char green;
    char blue;
    char intensity;
};

pixel pixel_array[PixelNum];

void write_pixels(void){ 

    // start frame
    WriteSPI2(START_FRAME);
    // wait for end of transaction
    while (SPI2STATbits.SPIBUSY); 
    
    int i;
    //payload
    for (i=0; i<PixelNum; i++){
        WriteSPI2(PIXEL_FRAME(pixel_array[i].intensity, pixel_array[i].red, pixel_array[i].green, pixel_array[i].blue));
        // wait for end of transaction
        while (SPI2STATbits.SPIBUSY); 
    }
    //stop frame
    WriteSPI2(STOP_FRAME);
    // wait for end of transaction
    while (SPI2STATbits.SPIBUSY); 
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
      static int i ;
      static int s[256], c[256], m[256] ;
      static unsigned char dds_inc_r=5, dds_inc_g=3, dds_inc_b=1, dds_inc_m=2;
      static unsigned char dds_acc_r, dds_acc_g, dds_acc_b, dds_acc_m;
      
      // DDS tables
      for(i=0; i<256; i++){
        s[i] = (int)(120.*sin((float)i*6.28/256.)+ 120);
        c[i] = (int)(120.*cos((float)i*6.28/256.)+ 120); 
        m[i] = (int)(7.*sin((float)i*6.28/256.) + 8); // half max intensity
      }
      
      while(1) {
        // yield time 
        PT_YIELD_TIME_msec(30) ;
        // DDS phase incrementers
        dds_acc_r += dds_inc_r ; 
        dds_acc_g += dds_inc_g ;
        dds_acc_b += dds_inc_b ;
        dds_acc_m += dds_inc_m ;
        
        for(i=0; i<PixelNum; i++){
            pixel_array[i].intensity = HALF_ON ; 
            pixel_array[i].red = 0;//s[(dds_acc_r + i) & 0xff ] ;
            pixel_array[i].green = 0;//c[(dds_acc_g + i) & 0xff ] ;
            pixel_array[i].blue = s[(dds_acc_b + i/2) & 0xff ] ;
        }
        
        write_pixels();
       
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

    // divide Fpb by 2, configure the I/O ports. Not using SS in this example
    // 16 bit transfer CKP=1 CKE=1
    // possibles SPI_OPEN_CKP_HIGH;   SPI_OPEN_SMP_END;  SPI_OPEN_CKE_REV
    // For any given peripherial, you will need to match these
    SpiChnOpen(2, SPI_OPEN_ON | SPI_OPEN_MODE32 | SPI_OPEN_MSTEN | SPICON_CKP, 4);
    // SCK2 is pin 26 
    // SDO2 (MOSI) is in PPS output group 2, could be connected to RB5 which is pin 14
    PPSOutput(2, RPB5, SDO2);
        
  // init the threads
  PT_INIT(&pt_timer);
  
  // round-robin scheduler for threads
  while (1){
      PT_SCHEDULE(protothread_timer(&pt_timer));
  }
} // main

// === end  ======================================================

