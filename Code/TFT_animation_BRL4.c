/*
 * File:        Guitar Hero MMMMDCCLX
 * Author:      Brian Dempsey, Katarina Martucci, Liam Patterson
 * Target PIC:  PIC32MX250F128B
 * NOTE: Based on code written by Bruce Land.
 */

////////////////////////////////////
// clock AND protoThreads configure!
// You MUST check this file!
#include "config.h"
// threading library
#include "pt_cornell_1_2.h"
////////////////////////////////////
// graphics libraries
#include "tft_master.h"
#include "tft_gfx.h"
#include "math.h"
#include <stdlib.h>
#include "sounds.h"

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
#define NINETYDEG 420
#define THIRTYDEG 500
#define NEGTHIRTYDEG 360

// DAC ISR
// A-channel, 1x, active
#define DAC_config_chan_A 0b0011000000000000


// === thread structures ============================================
// thread control structs

//print lock
static struct pt_sem print_sem ;

// note that UART input and output are threads
static struct pt pt_notehit, pt_print, pt_button1, pt_button2, 
        pt_button3, pt_button4, pt_button5, pt_draw, pt_time, pt_menu,
        pt_spawnandsound, pt_rockerbuttondown, pt_rockerbuttonup;

// system 1 second interval tick
int sys_time_seconds ;
// variables to store whether buttons pressed
int greenPushed = 0;
int redPushed = 0;
int yellowPushed = 0;
int bluePushed = 0;
int orangePushed = 0;

// global variable telling how many points
int points = 0;

// variables to tell whether the menu is on
int menuOn = 1;
int done_with_menu = 0;

// note struct
typedef struct {
  //position
  int x;
  int y;
  
  // whether to display or not
  int to_display;
  
  //color of note
  unsigned short color;
} note;

// maximum number of notes per row so that we don't crash CPU
static const int num_per_row = 20;
// notes array, 5 notes for each color
note notes_array[5][20];

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

int i;

//== Timer 2 interrupt handler ===========================================
// actual scaled DAC
volatile unsigned int DAC_data;
// the DDS units:
volatile unsigned int phase_accum_main1, phase_incr_main1=0;
// variables to help us keep track of which notes are on
int note_counter = 0, disp_note_counter = 0;
// difficulty setting
int difficulty = 4;
// enum to help us go through the menu state machine
typedef enum {
    main_menu,
    songs,
    difficulty_menu
} menu_state;
// initialize menu_status
menu_state menu_status = main_menu;

// == Timer 2 ISR =====================================================
// just toggles a pin for timing strobe
void __ISR(_TIMER_2_VECTOR, ipl2) Timer2Handler(void)
{
     phase_accum_main1 += phase_incr_main1 ;
    // set DAC_to the sin_table value
    DAC_data = (((sin_table[phase_accum_main1>>24])));
    
    // === Channel A =============
    // CS low to start transaction
     mPORTBClearBits(BIT_4); // start transaction
    // test for ready
    //while (TxBufFullSPI2());
    // write to spi2
    WriteSPI2( DAC_config_chan_A | ( DAC_data + 2048 ));
    while (SPI2STATbits.SPIBUSY); // wait for end of transaction
     // CS high
    mPORTBSetBits(BIT_4); // end transaction
    mT2ClearIntFlag();
}

int start_music = 0;

// where the song is contained
int start_index = 0, end_index = 0;


// time thread
static PT_THREAD ( protothread_time( struct pt *pt ) )
{
//    static int spawn_loop;
    
    PT_BEGIN(pt);
    // wait while the menu is on
    while( menuOn ) {
        PT_YIELD_TIME_msec(10);
    }
    // once menu is off, wait 2 seconds for the notes to reach the bottom of the screen
    PT_YIELD_TIME_msec(2000);
    // now start playing the music
    start_music = 1;
    // start time of the thread
    static int start_time_time = 0;
    // while the menu is not on, increment the time by 200 milliseconds
    while(!menuOn) {
        // start time of the thread so we can increment the time by 200 ms
        start_time_time = PT_GET_TIME();
        if ( !menuOn ) {
            // increment
            sys_time_seconds++ ;
        }
        // yield for exactly 200 ms
        PT_YIELD_TIME_msec( 200 - ( PT_GET_TIME() - start_time_time ) );
    }
    PT_END(pt);
}

// numbers of notes in each row to make sure we have less than 20 in each row
int num_green = 0, num_red = 0, num_yellow = 0, num_blue = 0, num_orange = 0;

 //spawn thread to get notes on screen
int freq_to_play = 0;
static PT_THREAD (protothread_spawnandsound(struct pt *pt))
{
    PT_BEGIN(pt);
    // loop variables
    static int spawn_loop;
    static int spawn_loop_i = 0;
    static int spawn_loop_break;
    static int start_time_spawn = 0;
    while(1) {
        start_time_spawn = PT_GET_TIME();
        // only go into this if the menu is off
        if ( !menuOn ) {
            spawn_loop_break = 0;
            // loop through the notes array
            for ( spawn_loop_i = 0; spawn_loop_i < num_per_row && spawn_loop_break == 0; spawn_loop_i++ ) {
                // check if we should be spawning a note
                if ( notes_array[SONGS[disp_note_counter][0] - 1][spawn_loop_i].to_display == 0 && ( disp_note_counter % difficulty == 0 ) ) {
                    // put the note in the corresponding color and row
                    switch( notes_array[SONGS[disp_note_counter][0] - 1][spawn_loop_i].color ) {
                        // green
                        case ILI9340_GREEN :
                            // check if the note is not currently displayed and the number of notes in this row is less than 20
                            if ( notes_array[SONGS[disp_note_counter][0] - 1][spawn_loop_i].to_display == 0 && num_green < 20 ) {
                                // set the to display value to 1
                                notes_array[SONGS[disp_note_counter][0] - 1][spawn_loop_i].to_display = 1;
                                // set the x value to 240
                                notes_array[SONGS[disp_note_counter][0] - 1][spawn_loop_i].x = 240;
                                // break out of the loop
                                spawn_loop_i = num_per_row;
                                spawn_loop_break = 1;
                                // add 1 to the number of green in the row
                                num_green += 1;
                            }
                            break;
                        // red
                        case ILI9340_RED :
                            // check if the note is not currently displayed and the number of notes in this row is less than 20
                            if ( notes_array[SONGS[disp_note_counter][0] - 1][spawn_loop_i].to_display == 0 && num_red < 20 ) {
                                // set the to display value to 1
                                notes_array[SONGS[disp_note_counter][0] - 1][spawn_loop_i].to_display = 1;
                                // set the x value to 240
                                notes_array[SONGS[disp_note_counter][0] - 1][spawn_loop_i].x = 240;
                                // break out of the loop
                                spawn_loop_i = num_per_row;
                                // add 1 to the number of red in the row
                                num_red += 1;
                            }
                            break;
                        // yellow
                        case ILI9340_YELLOW :
                            // check if the note is not currently displayed and the number of notes in this row is less than 20
                            if ( notes_array[SONGS[disp_note_counter][0] - 1][spawn_loop_i].to_display == 0 && num_yellow < 20 ) {
                                // set the to display value to 1
                                notes_array[SONGS[disp_note_counter][0] - 1][spawn_loop_i].to_display = 1;
                                // set the x value to 240
                                notes_array[SONGS[disp_note_counter][0] - 1][spawn_loop_i].x = 240;
                                // break out of the loop
                                spawn_loop_i = num_per_row;
                                // add 1 to the number of yellow in the row
                                num_yellow += 1;
                            }
                            break;
                        // blue
                        case ILI9340_BLUE :
                            // check if the note is not currently displayed and the number of notes in this row is less than 20
                            if ( notes_array[SONGS[disp_note_counter][0] - 1][spawn_loop_i].to_display == 0 && num_blue < 20 ) {
                                // set the to display value to 1
                                notes_array[SONGS[disp_note_counter][0] - 1][spawn_loop_i].to_display = 1;
                                // set the x value to 240
                                notes_array[SONGS[disp_note_counter][0] - 1][spawn_loop_i].x = 240;
                                // break out of the loop
                                spawn_loop_i = num_per_row;
                                // add 1 to the number of blue in the row
                                num_blue += 1;
                            }
                            break;
                        // orange
                        default:
                            // check if the note is not currently displayed and the number of notes in this row is less than 20
                            if ( notes_array[SONGS[disp_note_counter][0] - 1][spawn_loop_i].to_display == 0 && num_orange < 20 ) {
                                // set the to display value to 1
                                notes_array[SONGS[disp_note_counter][0] - 1][spawn_loop_i].to_display = 1;
                                // set the x value to 240
                                notes_array[SONGS[disp_note_counter][0] - 1][spawn_loop_i].x = 240;
                                // break out of the loop
                                spawn_loop_i = num_per_row;
                                // add 1 to the number of orange in the row
                                num_orange += 1;
                            }
                            break;
                    }
                }
            }
            // if we should be starting the music, we give the ISR the correct value to play
            // out of the DAC
            if ( start_music ) {
                    freq_to_play = (int)freqs[SONGS[note_counter][1] - 15];
                    phase_incr_main1 = (int)(freq_to_play*(float)two32/Fs);
                    note_counter += 1;
                }
                // if we are at the end of the song, go here
                if ( note_counter == end_index - 10 ) {
                        // go back to the menu and reset all the values
                        menuOn = 1;
                        done_with_menu = 0;
                        menu_status = main_menu;
                        disp_note_counter = 0;
                        note_counter = 0;
                        freq_to_play = 0; phase_incr_main1 = 0; start_music = 0;
                        static int inner_spawn_loop = 0;
                        static int spawn_loop = 0;
                        for(spawn_loop = 0; spawn_loop < 5; spawn_loop++ ){
                            for ( inner_spawn_loop = 0; inner_spawn_loop < num_per_row; inner_spawn_loop++ ) {
                                notes_array[spawn_loop][inner_spawn_loop].to_display = 0;
                                notes_array[spawn_loop][inner_spawn_loop].x = 240;
                            }
                        }
                        num_red = 0; num_green = 0; num_blue = 0; num_yellow = 0; num_orange = 0;
                        tft_fillRoundRect(0,0,400,400,1,ILI9340_BLACK);
                }
                else {
                    // increment the displayed note counter
                    disp_note_counter += 1;
                }
        }
        // yield for exactly 200 msec
        PT_YIELD_TIME_msec(200 - ( PT_GET_TIME() - start_time_spawn ) );
    }
    PT_END(pt);
}

// thread time
int time_beginning = 0;
static PT_THREAD (protothread_draw(struct pt *pt))
{   
    PT_BEGIN(pt);
        // loop variables
        static int spawn_loop, inner_spawn_loop;
        while(1){
            // get the time at the beginning of the thread
            time_beginning = PT_GET_TIME();
            // only go into this thread if the menu is off
            if ( !menuOn ) {
                // loop through all the notes
                for(spawn_loop = 0; spawn_loop < 5; spawn_loop++ ){
                    for ( inner_spawn_loop = 0; inner_spawn_loop < num_per_row; inner_spawn_loop++ ) {
                        // if the notes are being displayed but their location is less than 10, they should be erased
                        if(notes_array[spawn_loop][inner_spawn_loop].to_display == 1 && notes_array[spawn_loop][inner_spawn_loop].x < 10 ) {
                            // set the to display value to 0
                            notes_array[spawn_loop][inner_spawn_loop].to_display = 0;
                            // erase the note
                            tft_fillRoundRect(notes_array[spawn_loop][inner_spawn_loop].x, notes_array[spawn_loop][inner_spawn_loop].y, 20, 20, 7, ILI9340_BLACK);
                            // reduce the number of that color note
                            switch ( notes_array[spawn_loop][inner_spawn_loop].color ) {
                                // green
                                case ILI9340_GREEN :
                                    num_green = num_green - 1;
                                    break;
                                // red
                                case ILI9340_RED :
                                    num_red = num_red - 1;
                                    break;
                                // yellow
                                case ILI9340_YELLOW :
                                    num_yellow = num_yellow - 1;
                                    break;
                                // blue
                                case ILI9340_BLUE :
                                    num_blue = num_blue - 1;
                                    break;
                                // orange
                                default:
                                    num_orange = num_orange - 1;
                                    break;
                            }
                        }
                        // simply update the location of the note on the screen
                        if(notes_array[spawn_loop][inner_spawn_loop].to_display == 1){
                                tft_fillRoundRect(notes_array[spawn_loop][inner_spawn_loop].x, notes_array[spawn_loop][inner_spawn_loop].y, 20, 20, 7, ILI9340_BLACK);
                                notes_array[spawn_loop][inner_spawn_loop].x = notes_array[spawn_loop][inner_spawn_loop].x - 8; 
                                tft_fillRoundRect(notes_array[spawn_loop][inner_spawn_loop].x, notes_array[spawn_loop][inner_spawn_loop].y, 20, 20, 7, notes_array[spawn_loop][inner_spawn_loop].color); 
                                tft_fillRoundRect(notes_array[spawn_loop][inner_spawn_loop].x+7, notes_array[spawn_loop][inner_spawn_loop].y+7, 6, 6, 1, ILI9340_WHITE);
                        }
                    }
                }
            }
            // yield so we get as close to 15 FPS as possible
            PT_YIELD_TIME_msec(67 - (PT_GET_TIME() - time_beginning) );
        }
    PT_END(pt);
}

// the value of the button we read from the I/O
int button_val;

// Different states for our pushbutton debouncing state machine
typedef enum {
    NoPush,
    MaybePush,
    Pushed,
    MaybeNoPush
} State;

// Different states for user input modifications
typedef enum {
    Pterm,
    Iterm,
    Dterm,
    Angle
} button_push_state;

////////////// Button Debouncing Threads \\\\\\\\\\\\\\
// All the threads are the same, only the first will be commented fully


State buttonState1 = NoPush;
// button 1 debouncing thread
static PT_THREAD (protothread_button1(struct pt *pt))
{
    PT_BEGIN(pt);
      while(1) {
            // read the value of the I/O port
            button_val = mPORTAReadBits( BIT_3 ) >> 3;
            // debouncing switch case
            switch( buttonState1 ) {
                // if the button isn't pushed
                case NoPush:
                    // if the button is still pressed, go to MaybePush
                    if ( !button_val ) buttonState1 = MaybePush;
                    // otherwise stay at NoPush
                    else buttonState1 = NoPush;
                    break;
                // if the button might be pressed
                case MaybePush:
                    // check if the button is still pressed,
                    // go to Pushed and set greenPushed to 1
                    if ( !button_val ) {
                        buttonState1 = Pushed;
                        if ( greenPushed == 0 ) {
                            greenPushed = 1; 
                        }
                        else greenPushed = 0;
                    }
                    // otherwise go to NoPush
                    else buttonState1 = NoPush;
                    break;
                // if the button is pushed
                case Pushed:
                    // if the button is still pressed, stay here
                    if ( !button_val ) buttonState1 = Pushed;
                    // otherwise set greenPushed to 0 and go to MaybeNoPush state
                    else{ 
                        greenPushed = 0;
                        buttonState1 = MaybeNoPush;
                    }
                    break;
                // if the button might not be pressed anymore
                case MaybeNoPush:
                    // if the button is pressed, go back to pushed state
                    if ( !button_val ) buttonState1 = Pushed;
                    // otherwise go to NoPush
                    else buttonState1 = NoPush;
                    break;
            }
            
            PT_YIELD_TIME_msec(10);
            // NEVER exit while
      } // END WHILE(1)

  PT_END(pt);
} // thread 4


// SAME AS THE ABOVE THREAD, NOT COMMENTED
State buttonState2 = NoPush;
// button 2 debouncing thread
static PT_THREAD (protothread_button2(struct pt *pt))
{
    PT_BEGIN(pt);
      while(1) {
            button_val = mPORTAReadBits( BIT_4 ) >> 4;
            switch( buttonState2 ) {
                case NoPush:
                    if ( !button_val ) buttonState2 = MaybePush;
                    else buttonState2 = NoPush;
                    break;
                case MaybePush:
                    if ( !button_val ) {
                        buttonState2 = Pushed;
                        if ( redPushed == 0 ) {
                            redPushed = 1; 
                        }
                        else redPushed = 0;
                    }
                    else buttonState2 = NoPush;
                    break;
                case Pushed:
                    if ( !button_val ) buttonState2 = Pushed;
                    else{ 
                        redPushed = 0;
                        buttonState2 = MaybeNoPush;
                    }
                    break;
                case MaybeNoPush:
                    if ( !button_val ) buttonState2 = Pushed;
                    else buttonState2 = NoPush;
                    break;
            }
            
            PT_YIELD_TIME_msec(10);
            // NEVER exit while
      } // END WHILE(1)

  PT_END(pt);
} // thread 4


// SAME AS THE ABOVE THREAD, NOT COMMENTED
State buttonState3 = NoPush;
// button 3 debouncing thread
static PT_THREAD (protothread_button3(struct pt *pt))
{
    PT_BEGIN(pt);
      while(1) {
            button_val = mPORTAReadBits( BIT_2 ) >> 2;
            switch( buttonState3 ) {
                case NoPush:
                    if ( !button_val ) buttonState3 = MaybePush;
                    else buttonState3 = NoPush;
                    break;
                case MaybePush:
                    if ( !button_val ) {
                         buttonState3 = Pushed;
                        if ( yellowPushed == 0 ) {
                            yellowPushed = 1; 
                        }
                        else yellowPushed = 0;
                    }
                    else  buttonState3 = NoPush;
                    break;
                case Pushed:
                    if ( !button_val )  buttonState3 = Pushed;
                    else{ 
                        yellowPushed = 0;
                        buttonState3 = MaybeNoPush;
                    }
                    break;
                case MaybeNoPush:
                    if ( !button_val )  buttonState3 = Pushed;
                    else  buttonState3 = NoPush;
                    break;
            }
            
            PT_YIELD_TIME_msec(10);
            // NEVER exit while
      } // END WHILE(1)

  PT_END(pt);
} // thread 4


// SAME AS THE ABOVE THREAD, NOT COMMENTED

State buttonState4 = NoPush;
// button 4 debouncing thread
static PT_THREAD (protothread_button4(struct pt *pt))
{
    PT_BEGIN(pt);
      while(1) {
            button_val = mPORTAReadBits( BIT_1 ) >> 1;
            switch( buttonState4 ) {
                case NoPush:
                    if ( !button_val ) buttonState4 = MaybePush;
                    else buttonState4 = NoPush;
                    break;
                case MaybePush:
                    if ( !button_val ) {
                         buttonState4 = Pushed;
                        if ( bluePushed == 0 ) {
                            bluePushed = 1; 
                        }
                        else bluePushed = 0;
                    }
                    else  buttonState4 = NoPush;
                    break;
                case Pushed:
                    if ( !button_val )  buttonState4 = Pushed;
                    else{ 
                        bluePushed = 0;
                        buttonState4 = MaybeNoPush;
                    }
                    break;
                case MaybeNoPush:
                    if ( !button_val )  buttonState4 = Pushed;
                    else  buttonState4 = NoPush;
                    break;
            }
            
            PT_YIELD_TIME_msec(10);
            // NEVER exit while
      } // END WHILE(1)

  PT_END(pt);
} // thread 4


// SAME AS THE ABOVE THREAD, NOT COMMENTED

State buttonState5 = NoPush;
// button 5 debouncing thread
static PT_THREAD (protothread_button5(struct pt *pt))
{
    PT_BEGIN(pt);
      while(1) {
            button_val = mPORTBReadBits( BIT_7 ) >> 0;
            switch( buttonState5 ) {
                case NoPush:
                    if ( !button_val ) buttonState5 = MaybePush;
                    else buttonState5 = NoPush;
                    break;
                case MaybePush:
                    if ( !button_val ) {
                         buttonState5 = Pushed;
                        if ( orangePushed == 0 ) {
                            orangePushed = 1; 
                        }
                        else orangePushed = 0;
                    }
                    else  buttonState5 = NoPush;
                    break;
                case Pushed:
                    if ( !button_val )  buttonState5 = Pushed;
                    else{ 
                        orangePushed = 0;
                        buttonState5 = MaybeNoPush;
                    }
                    break;
                case MaybeNoPush:
                    if ( !button_val )  buttonState5 = Pushed;
                    else  buttonState5 = NoPush;
                    break;
            }
            
            PT_YIELD_TIME_msec(10);
            // NEVER exit while
      } // END WHILE(1)

  PT_END(pt);
} // thread 4

// values to tell us whether the strummer is activated
int StrummerUpOn = 0;
int Strummermenu = 0;

// same idea as the above debouncing threads but we only keep the strummer value
// high for only one cycle. Also we have a handshake value called strummermenu
// that prevents changing the value of the strummer while the menu is on. This 
// makes it much easier to use the menu, but after the game has started we turn
// this off so that you can't hold the strummer down and continue to get points
State buttonState6 = NoPush;
// strummer debouncing thread
static PT_THREAD (protothread_rockerbuttonup(struct pt *pt))
{
    PT_BEGIN(pt);
    static int rockerup_val = 0;
      while(1) {
          if ( !Strummermenu ) {
                // yield time 1 second
                rockerup_val = mPORTBReadBits( BIT_13 ) >> 13;
                switch( buttonState6 ) {
                    case NoPush:
                        if ( rockerup_val ) buttonState6 = MaybePush;
                        else buttonState6 = NoPush;
                        break;
                    case MaybePush:
                        if ( rockerup_val ) {
                             buttonState6 = Pushed;
                            if ( StrummerUpOn == 0 ) {
                                StrummerUpOn = 1; 
                                if ( menuOn ) Strummermenu = 1;
                            }
                            else StrummerUpOn = 0;
                        }
                        else  buttonState6 = NoPush;
                        break;
                    case Pushed:
                        StrummerUpOn = 0;
                        if ( rockerup_val )  buttonState6 = Pushed;
                        else{ 
                            StrummerUpOn = 0;
                            buttonState6 = MaybeNoPush;
                        }
                        break;
                    case MaybeNoPush:
                        if ( rockerup_val )  buttonState6 = Pushed;
                        else  buttonState6 = NoPush;
                        break;
                }
          }
                PT_YIELD_TIME_msec(10);
                // NEVER exit while
            
    } // END WHILE(1)

  PT_END(pt);
} // thread 6


// SAME AS THE THREAD ABOVE

int StrummerDownOn = 0;

State buttonState7 = NoPush;
// strummer debouncing thread
static PT_THREAD (protothread_rockerbuttondown(struct pt *pt))
{
    PT_BEGIN(pt);
    static int rockerdown_val = 0;
      while(1) {
          if ( !Strummermenu ) {
            // yield time 1 second
            rockerdown_val = mPORTBReadBits( BIT_8 );
            switch( buttonState7 ) {
                case NoPush:
                    if ( rockerdown_val ) buttonState7 = MaybePush;
                    else buttonState7 = NoPush;
                    break;
                case MaybePush:
                    if ( rockerdown_val ) {
                         buttonState7 = Pushed;
                        if ( StrummerDownOn == 0 ) {
                            StrummerDownOn = 1;
                            if ( menuOn ) Strummermenu = 1;
                        }
                        else StrummerDownOn = 0;
                    }
                    else  buttonState7 = NoPush;
                    break;
                case Pushed:
                    StrummerDownOn = 0;
                    if ( rockerdown_val )  buttonState7 = Pushed;
                    else{ 
                        StrummerDownOn = 0;
                        buttonState7 = MaybeNoPush;
                    }
                    break;
                case MaybeNoPush:
                    if ( rockerdown_val )  buttonState7 = Pushed;
                    else  buttonState7 = NoPush;
                    break;
            }
          } 
            PT_YIELD_TIME_msec(10);
            // NEVER exit while
        
      } // END WHILE(1)

  PT_END(pt);
} // thread 7

// string buffer to write 
char buffer1[60];

// check note hits
static PT_THREAD (protothread_notehit(struct pt *pt))
{
    // initialize variables for hitting notes
    static int green_hit = 0;
    static int red_hit = 0;
    static int yellow_hit = 0;
    static int blue_hit = 0;
    static int orange_hit = 0;
    
    PT_BEGIN(pt);
    while(1) {
        // only run this thread if the menu is off
        if ( !menuOn ) {
            // iterate through the notes in each row
            for ( i = 0; i < num_per_row; i++ ) {
              // check if the notes in each row are in the correct position for a point to be gained and the user
              // strums, give them a point and erase the note
              if( greenPushed && (notes_array[0][i].x<40 && notes_array[0][i].x>0) && ( StrummerUpOn || StrummerDownOn ) ){
                  points += 1;
                  notes_array[0][i].to_display = 0;
                  tft_fillRoundRect(notes_array[0][i].x, notes_array[0][i].y, 20, 20, 7, ILI9340_BLACK);
                  notes_array[0][i].x = 240;
                  num_green = num_green - 1;
              }
              else points += 0;
              if ( redPushed && (notes_array[1][i].x<40 && notes_array[1][i].x>0 ) && ( StrummerUpOn || StrummerDownOn ) ) {
                  points += 1;
                  notes_array[1][i].to_display = 0;
                  tft_fillRoundRect(notes_array[1][i].x, notes_array[1][i].y, 20, 20, 7, ILI9340_BLACK);
                  notes_array[1][i].x = 240;
                  num_red = num_red - 1;
              }
              else points += 0;
              if ( yellowPushed && (notes_array[2][i].x<40 && notes_array[2][i].x>0 ) && ( StrummerUpOn || StrummerDownOn ) ) {
                  points += 1;
                  notes_array[2][i].to_display = 0;
                  tft_fillRoundRect(notes_array[2][i].x, notes_array[2][i].y, 20, 20, 7, ILI9340_BLACK);
                  notes_array[2][i].x = 240;
                  num_yellow = num_yellow - 1;
              }
              else points += 0;
              if ( bluePushed && (notes_array[3][i].x<40 && notes_array[3][i].x>0 )  && ( StrummerUpOn || StrummerDownOn )) {
                  points += 1;
                  notes_array[3][i].to_display = 0;
                  tft_fillRoundRect(notes_array[3][i].x, notes_array[3][i].y, 20, 20, 7, ILI9340_BLACK);
                  notes_array[3][i].x = 240;
                  num_blue = num_blue - 1;
              }
              else points += 0;
              if ( orangePushed && (notes_array[4][i].x<40 && notes_array[4][i].x>0 ) && ( StrummerUpOn || StrummerDownOn ) ) {
                  points += 1;
                  notes_array[4][i].to_display = 0;
                  tft_fillRoundRect(notes_array[4][i].x, notes_array[4][i].y, 20, 20, 7, ILI9340_BLACK);
                  notes_array[4][i].x = 240;
                  num_orange = num_orange - 1;
              }
              else points += 0;
            }
            // print out the points
            tft_setCursor(80, 10); tft_setTextSize(2); tft_setTextColor(ILI9340_WHITE); tft_setRotation(2);
            sprintf(buffer1, "Points: %d", points);
            tft_writeString(buffer1);   
            tft_setRotation(1);
              // NEVER exit while
        }
        PT_YIELD_TIME_msec(10);
    } // END WHILE(1)

  PT_END(pt);
} 

// thread to print the frets at the bottom of the screen. If you are pressing
// the corresponding button on the guitar, the fret will light up
static PT_THREAD (protothread_print(struct pt *pt))
{
    PT_BEGIN(pt);
        while (1) {
            if (!menuOn) {
                tft_fillRoundRect(295,172, 15, 80, 1, ILI9340_BLACK);
                tft_fillRoundRect(200, 100, 10, 10, 1, ILI9340_BLACK);
                tft_fillRoundRect(18,15, 20, 20, 7, ILI9340_WHITE);
                if ( greenPushed == 1 )   tft_fillRoundRect(18,15,20, 20, 7, ILI9340_GREEN); // x,y,w,h,radius,color
                tft_fillRoundRect(18,63, 20, 20, 7, ILI9340_WHITE);
                if ( redPushed == 1 )   tft_fillRoundRect(18,63, 20, 20, 7, ILI9340_RED);
                tft_fillRoundRect(18,111, 20, 20, 7, ILI9340_WHITE);
                if ( yellowPushed == 1 )   tft_fillRoundRect(18,111, 20, 20, 7, ILI9340_YELLOW);
                tft_fillRoundRect(18,159, 20, 20, 7, ILI9340_WHITE);
                if ( bluePushed == 1 )   tft_fillRoundRect(18,159, 20, 20, 7, ILI9340_BLUE);
                tft_fillRoundRect(18,207, 20, 20, 7, ILI9340_WHITE);
                if ( orangePushed == 1 )    tft_fillRoundRect(18,207, 20, 20, 7, 0xFB00);
            }
            PT_YIELD_TIME_msec(67);
        }
  PT_END(pt);
} // print thread

// enum to go with the state machine for difficulty setting
typedef enum {
    easy,
    medium,
    hard
} difficulty_state;

// variable to help us move through the menu
int proceed = 0;

// enum to go with the state machine for songs to play
typedef enum {
    Sweetchild,
    Fireflies,
    CliffsofDover,
    Reptilia
} which_song;

// buffer to print the points
char buffer5123545[60];

// start menu
static PT_THREAD (protothread_menu(struct pt *pt))
{
    PT_BEGIN(pt);
    // initialize variables for difficulty and song
    static difficulty_state difficulty_setting = easy;
    static which_song song_to_play = Sweetchild;
        while(1) {
            // run the menu while we aren't done with the menu
            while(menuOn && !done_with_menu) {
               // if either of the buttons is still pressed and proceed is already
               // 0, don't continue
               if ( ( redPushed | greenPushed ) && ( proceed == 0 ) ) {
                   proceed = 0;
               }
               else {
                   // otherwise set proceed to 1 and move on
                   proceed = 1;
                   // depends on where we currently are in the menu
                   switch( menu_status ) {
                        // if in main_menu, display welcome back message and points
                        case main_menu:
                            tft_setCursor(30, 10); tft_setTextSize(2); tft_setTextColor(ILI9340_WHITE); tft_setRotation(2);
                            tft_writeString("Welcome back to:");
                            tft_setCursor(55, 30);
                            tft_writeString("Guitar Hero");
                            tft_setCursor(65, 50);
                            tft_writeString("MMMMDCCLX");
                            tft_setCursor(40,70);
                            tft_writeString("Previous Score:");
                            tft_setCursor(110,90);
                            sprintf(buffer5123545, "%d", points);
                            tft_writeString(buffer5123545);
                            // if the user pushes green, move onto songs
                            if ( greenPushed ) {
                                tft_fillRoundRect( 30,10, 200, 100, 1, ILI9340_BLACK );
                                menu_status = songs;
                                proceed = 0;
                            }
                            // otherwise stay at main_menu
                            else {
                                menu_status = main_menu;
                            }
                            break;
                        // if we are on the songs menu
                        case songs:
                            // display the currently selected song in red and set
                            // the global song variable depending on selected song.
                            // Move up or down according to current song
                            switch( song_to_play ) {
                                case Sweetchild:
                                    start_index = 0;
                                    end_index = 689;
                                    tft_setTextColor(ILI9340_RED);
                                    tft_setCursor(30, 100);
                                    tft_writeString("Sweet Child");
                                    tft_setTextColor(ILI9340_WHITE);
                                    tft_setCursor(30, 120);
                                    tft_writeString("Fireflies");
                                    tft_setCursor(30, 140);
                                    tft_writeString("Cliffs of Dover");
                                    tft_setCursor(30, 160);
                                    tft_writeString("Reptilia");
                                    if ( StrummerDownOn ) {
                                        song_to_play = Fireflies;
                                        Strummermenu = 0;
                                    }
                                    //up
                                    else if ( StrummerUpOn ) {
                                        song_to_play = Reptilia;
                                        Strummermenu = 0;
                                    }
                                    break;
                                case Fireflies:
                                    start_index = 1233;
                                    end_index = 1460;
                                    tft_setCursor(30, 100);
                                    tft_writeString("Sweet Child");
                                    tft_setCursor(30, 120);
                                    tft_setTextColor(ILI9340_RED);
                                    tft_writeString("Fireflies");
                                    tft_setTextColor(ILI9340_WHITE);
                                    tft_setCursor(30, 140);
                                    tft_writeString("Cliffs of Dover");
                                    tft_setCursor(30, 160);
                                    tft_writeString("Reptilia");
                                    if ( StrummerDownOn ) {
                                        song_to_play = CliffsofDover;
                                        Strummermenu = 0;
                                    }
                                    //up
                                    else if ( StrummerUpOn ) {
                                        song_to_play = Sweetchild;
                                        Strummermenu = 0;
                                    }
                                    break;
                                case CliffsofDover:
                                    start_index = 690;
                                    end_index = 1232;
                                    tft_setCursor(30, 100);
                                    tft_writeString("Sweet Child");
                                    tft_setCursor(30, 120);
                                    tft_writeString("Fireflies");
                                    tft_setCursor(30, 140);
                                    tft_setTextColor(ILI9340_RED);
                                    tft_writeString("Cliffs of Dover");
                                    tft_setTextColor(ILI9340_WHITE);
                                    tft_setCursor(30, 160);
                                    tft_writeString("Reptilia");
                                    if ( StrummerDownOn ) {
                                        song_to_play = Reptilia;
                                        Strummermenu = 0;
                                    }
                                    //up
                                    else if ( StrummerUpOn ) {
                                        song_to_play = Fireflies;
                                        Strummermenu = 0;
                                    }
                                    break;
                                case Reptilia:
                                    start_index = 1461;
                                    end_index = 2061;
                                    tft_setCursor(30, 100);
                                    tft_writeString("Sweet Child");
                                    tft_setCursor(30, 120);
                                    tft_writeString("Fireflies");
                                    tft_setCursor(30, 140);
                                    tft_writeString("Cliffs of Dover");
                                    tft_setCursor(30, 160);
                                    tft_setTextColor(ILI9340_RED);
                                    tft_writeString("Reptilia");
                                    tft_setTextColor(ILI9340_WHITE);
                                    if ( StrummerDownOn ) {
                                        song_to_play = Sweetchild;
                                        Strummermenu = 0;
                                    }
                                    //up
                                    else if ( StrummerUpOn ) {
                                        song_to_play = CliffsofDover;
                                        Strummermenu = 0;
                                    }
                                    break;
                                default:
                                    break;
                            }
                            if ( greenPushed ) {
                                tft_fillRoundRect( 30,100, 210, 80, 1, ILI9340_BLACK );
                                menu_status = difficulty_menu;
                                proceed = 0;
                                disp_note_counter = start_index;
                                note_counter = start_index;
                            }
                            else if ( redPushed ) {
                                tft_fillRoundRect( 30,100, 210, 80, 1, ILI9340_BLACK );
                                menu_status = main_menu;
                                proceed = 0;
                            }
                            break;
                        // if we are on the difficulty menu
                        case difficulty_menu:
                            // highlight the difficulty the user is hovering over
                            // and if they press green, set the global difficulty
                            // value. Move between the settings based on current
                            // position
                            switch ( difficulty_setting ) {
                                case easy:
                                    difficulty = 8;
                                    tft_setCursor(100, 100);
                                    tft_setTextColor(ILI9340_RED);
                                    tft_writeString("Easy");
                                    tft_setTextColor(ILI9340_WHITE);
                                    tft_setCursor(100, 120);
                                    tft_writeString("Medium");
                                    tft_setCursor(100, 140);
                                    tft_writeString("Hard");
                                    // down
                                    if ( StrummerDownOn ) {
                                        difficulty_setting = medium;
                                        Strummermenu = 0;
                                    }
                                    //up
                                    else if ( StrummerUpOn ) {
                                        difficulty_setting = hard;
                                        Strummermenu = 0;
                                    }
                                    break;
                                case medium:
                                    difficulty = 4;
                                    tft_setCursor(100, 100);
                                    tft_writeString("Easy");
                                    tft_setCursor(100, 120);
                                    tft_setTextColor(ILI9340_RED);
                                    tft_writeString("Medium");
                                    tft_setTextColor(ILI9340_WHITE);
                                    tft_setCursor(100, 140);
                                    tft_writeString("Hard");
                                    // down
                                    if ( StrummerDownOn ) {
                                        difficulty_setting = hard;
                                        Strummermenu = 0;
                                    }
                                    //up
                                    else if ( StrummerUpOn ) {
                                        difficulty_setting = easy;
                                        Strummermenu = 0;
                                    }
                                    break;
                                case hard:
                                    difficulty = 1;
                                    tft_setCursor(100, 100);
                                    tft_writeString("Easy");
                                    tft_setCursor(100, 120);
                                    tft_writeString("Medium");
                                    tft_setCursor(100, 140);
                                    tft_setTextColor(ILI9340_RED);
                                    tft_writeString("Hard");
                                    tft_setTextColor(ILI9340_WHITE);
                                    // down
                                    if ( StrummerDownOn ) {
                                        difficulty_setting = easy;
                                        Strummermenu = 0;
                                    }
                                    //up
                                    else if ( StrummerUpOn ) {
                                        difficulty_setting = medium;
                                        Strummermenu = 0;
                                    }
                                    break;
                                default:
                                    difficulty_setting = easy;
                                    break;
                            }
                            // if they press green, start the game
                            if ( greenPushed ) {
                                 tft_fillRoundRect( 0, 0, 300, 300, 2, ILI9340_BLACK );
                                 tft_setRotation(1);
                                 done_with_menu = 1;
                                 menuOn = 0;   
                                 Strummermenu = 0;
                                 points = 0;
                            }
                            else if ( redPushed ) {
                                tft_fillRoundRect( 100, 100, 80, 100, 2, ILI9340_BLACK );
                                menu_status = songs;
                                proceed = 0;
                            }
                            break;
                             
                        default:
                            menu_status = main_menu;
                            break;
                    }
                }
               PT_YIELD_TIME_msec(67);
            }
        }
  PT_END(pt);
} // thread 4






// === Main  ======================================================

int main(void)
{
  // === Config timer and output compare to make PWM ======== 
    // set up timer2 to generate the wave period -- SET this to 1 mSec!
    OpenTimer2(T2_ON | T2_SOURCE_INT | T2_PS_1_1, 800);
    // Need ISR to compute PID controller 
    ConfigIntTimer2(T2_INT_ON | T2_INT_PRIOR_2); 
    mT2ClearIntFlag(); // and clear the interrupt flag 
    // set up compare3 for PWM mode 
    //OpenOC3(OC_ON | OC_TIMER2_SRC | OC_PWM_FAULT_PIN_DISABLE , pwm_on_time, pwm_on_time); // 
    // OC3 is PPS group 4, map to RPB9 (pin 18) 
    //PPSOutput(4, RPB9, OC3);

  // === config the uart, DMA, vref, timer5 ISR ===========
  PT_setup();

  // === setup system wide interrupts  ====================
  INTEnableSystemMultiVectoredInt();
    
  // === set up i/o port pin ===============================
  mPORTBSetPinsDigitalIn( BIT_13 | BIT_8 );    //Set port as output
  EnablePullUpB( BIT_13 | BIT_8 );
  
  // init the display
  tft_init_hw();
  tft_begin();
  tft_fillScreen(ILI9340_BLACK);
  //240x320 vertical display
  tft_setRotation(1); // Use tft_setRotation(1) for 320x240
  //tft_fillRoundRect(0,0, 320, 240, 1, ILI9340_GREEN);// x,y,w,h,radius,color
  
    //bottom barrier
  //tft_drawLine(80, 1, 80, 60, ILI9340_GREEN);

  // Buttons
  mPORTASetPinsDigitalIn( BIT_2 | BIT_3 );
  EnablePullUpA( BIT_2 | BIT_3  );

  // SCK2 is pin 26
  // SDO2 (MOSI) is in PPS output group 2, could be connected to RB5 which is pin 14
  PPSOutput(2, RPB5, SDO2);

  // control CS for DAC
  mPORTBSetPinsDigitalOut(BIT_4);
  mPORTBSetBits(BIT_4);

  // divide Fpb by 2, configure the I/O ports. Not using SS in this example
  // 16 bit transfer CKP=1 CKE=1
  // possibles SPI_OPEN_CKP_HIGH;   SPI_OPEN_SMP_END;  SPI_OPEN_CKE_REV
  // For any given peripherial, you will need to match these
  // clk divider set to 2 for 20 MHz
  SpiChnOpen(SPI_CHANNEL2, SPI_OPEN_ON | SPI_OPEN_MODE16 | SPI_OPEN_MSTEN | SPI_OPEN_CKE_REV , 2);
// end DAC setup

  // initialize the notes
  unsigned int color_array[5] = {ILI9340_GREEN, ILI9340_RED, ILI9340_YELLOW, ILI9340_BLUE, 0xFB00};
  int k;
  for(k = 0; k < 5; k ++) {
      for ( i = 0; i < num_per_row; i++ ) {
        notes_array[k][i].x = 240;
        notes_array[k][i].y = 15 + (48*k);
        notes_array[k][i].color = color_array[k];
        notes_array[k][i].to_display = 0;
      }
  }
  // generate the sine table
  int ii;
  for (ii = 0; ii < sine_table_size; ii++) {
      sin_table[ii] = (int)(1023*sin((float)ii*6.283/(float)sine_table_size));
  }

  // === now the threads ===================================
  
  // init the threads
  PT_INIT(&pt_print);
  PT_INIT(&pt_button1);
  PT_INIT(&pt_button2);
  PT_INIT(&pt_button3);
  PT_INIT(&pt_button4);
  PT_INIT(&pt_button5);
  PT_INIT(&pt_notehit);
  PT_INIT(&pt_draw);
  PT_INIT(&pt_time);
  PT_INIT(&pt_menu);
  PT_INIT(&pt_spawnandsound);
  PT_INIT(&pt_rockerbuttondown);
  PT_INIT(&pt_rockerbuttonup);

  // schedule the threads
  while(1) { 
    PT_SCHEDULE(protothread_print(&pt_menu));
    PT_SCHEDULE(protothread_print(&pt_print));
    PT_SCHEDULE(protothread_button1(&pt_button1));
    PT_SCHEDULE(protothread_button2(&pt_button2));
    PT_SCHEDULE(protothread_button3(&pt_button3));
    PT_SCHEDULE(protothread_button4(&pt_button4));
    PT_SCHEDULE(protothread_button5(&pt_button5));
    PT_SCHEDULE(protothread_notehit(&pt_notehit));
    PT_SCHEDULE(protothread_draw(&pt_draw));
    PT_SCHEDULE(protothread_time(&pt_time));
    PT_SCHEDULE(protothread_spawnandsound(&pt_spawnandsound));
    PT_SCHEDULE(protothread_rockerbuttondown(&pt_rockerbuttondown));
    PT_SCHEDULE(protothread_rockerbuttonup(&pt_rockerbuttonup));
    
  }
} // main