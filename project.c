#include <stdint.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// graphics
#include <macros.h>
#include <lcd_model.h>
#include <ascii_font.h>
#include <graphics.h>
// sensors
#include "bh1750.h"
#include "dht11.h"
#include "hcsr04.h"
//uart definitions
#define BAUD (9600)
#define MYUBRR ((F_CPU/16/BAUD) - 1)
// These buffers may be any size from 2 to 256 bytes.
#define  RX_BUFFER_SIZE  64
#define  TX_BUFFER_SIZE  64
//timer definitions
#define FREQ (16000000.0)
#define PRESCALE (1024.0)

/*
**  Initialise the LCD display.
*/
void new_lcd_init(uint8_t contrast) {
    // Set up the pins connected to the LCD as outputs
    SET_OUTPUT(DDRD, SCEPIN); // Chip select -- when low, tells LCD we're sending data
    SET_OUTPUT(DDRD, RSTPIN); // Chip Reset
    SET_OUTPUT(DDRD, DCPIN);  // Data / Command selector
    SET_OUTPUT(DDRD, DINPIN); // Data input to LCD
    SET_OUTPUT(DDRD, SCKPIN); // Clock input to LCD

    CLEAR_BIT(PORTD, RSTPIN); // Reset LCD
    SET_BIT(PORTD, SCEPIN);   // Tell LCD we're not sending data.
    SET_BIT(PORTD, RSTPIN);   // Stop resetting LCD

    LCD_CMD(lcd_set_function, lcd_instr_extended);
    LCD_CMD(lcd_set_contrast, contrast);
    LCD_CMD(lcd_set_temp_coeff, 0);
    LCD_CMD(lcd_set_bias, 3);

    LCD_CMD(lcd_set_function, lcd_instr_basic);
    LCD_CMD(lcd_set_display_mode, lcd_display_normal);
    LCD_CMD(lcd_set_x_addr, 0);
    LCD_CMD(lcd_set_y_addr, 0);
}

/*
**  Declare the bits for drawing
*/
uint16_t dinosaur_head[16] = {
    0b00000000000000000,
    0b00000111111111000,
    0b00000111111011000,
    0b00000111111111000,
    0b00000111110000000,
    0b00000111111111000,
    0b00000111110000000,
    0b00011111110000000,
    
};

uint16_t dinosaur_body_org[16] = {
    0b00111111111100000,
    0b00111111111111000,
    0b00111111111001000,
    0b00111111111000000,
    0b00011111100000000,
    0b00001110000000000,
    0b00001100000000000,
    0b00001111000000000,

};

uint8_t rock_org[8] = {
    0b000000,
    0b000000,
    0b011000,
    0b011000,
    0b011000,
    0b011000,
    0b111100,
    0b011000,
};

uint8_t sky_org[8] = {
    0b00111100,
    0b01000010,
    0b10000001,
    0b01100010,
    0b10000001,
    0b01000010,
    0b01111110,
    0b00000000,
};

// Initialise variables 
uint16_t dinosaur[8];
uint16_t dinosaur_body[8];
uint8_t rock[8];
uint8_t sky[8];
uint16_t x = 0 , y = 0;
uint16_t row = 0, col = 0;
uint8_t x_b = 0, y_b = 0;
int game_over = 1;
char gaming_time[64];
int control_distance;

/*
**  Set up the 16 x 16 bits
**  Bypasses the screen buffer used by <lcd.h>.
**
**  Parameters: void   
**  Returns: void
*/
void setup_dinosaur(void) {
    // Visit each column of output bitmap
    for (int i = 0; i < 16; i++) {

        // Visit each row of output bitmap
        for (int j = 0; j < 16; j++) {
            // Kind of like: head_direct[i][j] = head_original[j][7-i].
            // Flip about the major diagonal.
            uint16_t bit_val = BIT_VALUE(dinosaur_head[j], (15 - i));
            WRITE_BIT(dinosaur[i], j, bit_val);
            uint16_t bit_val1 = BIT_VALUE(dinosaur_body_org[j], (15 - i));
            WRITE_BIT(dinosaur_body[i], j, bit_val1);
        }
    }
}

void setup_background(void) {
    // Visit each column of output bitmap
    for (int i = 0; i < 8; i++) {

        // Visit each row of output bitmap
        for (int j = 0; j < 8; j++) {
            // Kind of like: head_direct[i][j] = head_original[j][7-i].
            // Flip about the major diagonal.
            uint16_t bit_val3 = BIT_VALUE(rock_org[j], (5 - i));
            WRITE_BIT(rock[i], j, bit_val3);
            uint16_t bit_val4 = BIT_VALUE(sky_org[j], (7 - i));
            WRITE_BIT(sky[i], j, bit_val4);
        }
    }
}

/*
**  Draw character directly to LCD.
**  Bypasses the screen buffer used by <lcd.h>.
**
**  Parameters:
**      x, y:   Integer coordinates at which the character will be placed.
**              The y-position will be truncated to the nearest multiple of 8.
**      ch:     The character to draw.
**      colour: The colour. If FG_COLOUR, the characters are rendered normally.
**              If BG_COLOUR, the text is drawn as a negative (inverse).
**  Returns:
**      No result is returned.
*/
void draw_char_direct(int x, int y, char ch, colour_t colour) {
    // Do nothing if character does not fit on LCD.
    if (x < 0 || x > LCD_X - CHAR_WIDTH || y < 0 || y > LCD_Y - CHAR_HEIGHT) {
        return;
    }

    // Move LCD cursor to starting spot.
    LCD_CMD(lcd_set_function, lcd_instr_basic | lcd_addr_horizontal);
    LCD_CMD(lcd_set_x_addr, (x & 0x7f));
    LCD_CMD(lcd_set_y_addr, (y & 0x7f) / 8);

    // Send pixel blocks.
    for (int i = 0; i < CHAR_WIDTH; i++) {
        uint8_t pixelBlock = pgm_read_byte(&(ASCII[ch - ' '][i]));

        if (colour == BG_COLOUR) {
            pixelBlock = ~pixelBlock;
        }

        LCD_DATA(pixelBlock);
    }
}

/*
**  Draw string directly to LCD.
**  Bypasses the screen buffer used by <lcd.h>.
**
**  Parameters:
**      x, y:   Integer coordinates at which the character will be placed.
**              The y-position will be truncated to the nearest multiple of 8.
**      str:    The text to draw.
**      colour: The colour. If FG_COLOUR, the characters are rendered normally.
**              If BG_COLOUR, the text is drawn as a negative (inverse).
**  Returns:
**      No result is returned.
*/
void draw_string_direct(int x, int y, char * str, colour_t colour) {
    for (int i = 0; str[i] != 0; i++, x += CHAR_WIDTH) {
        draw_char_direct(x, y, str[i], colour);
    }
}


void draw_dinosaur_head(uint16_t x, uint16_t y) {
    LCD_CMD(lcd_set_function, lcd_instr_basic | lcd_addr_horizontal);
    LCD_CMD(lcd_set_x_addr, x);
    LCD_CMD(lcd_set_y_addr, y);
    for (int i = 0; i < 16; i++) {
        LCD_DATA(dinosaur[i]);
        
    }  
}
void draw_dinosaur_body(uint16_t x, uint16_t y) {
    LCD_CMD(lcd_set_function, lcd_instr_basic | lcd_addr_horizontal);
    LCD_CMD(lcd_set_x_addr, x);
    LCD_CMD(lcd_set_y_addr, y + 1);
    for (int i = 0; i < 16; i++) {
        LCD_DATA(dinosaur_body[i]);
    } 
}

void draw_rock(uint16_t x, uint16_t y) {
    LCD_CMD(lcd_set_function, lcd_instr_basic | lcd_addr_horizontal);
    LCD_CMD(lcd_set_x_addr, x);
    LCD_CMD(lcd_set_y_addr, y + 1);
    for (int i = 0; i < 4; i++) {
        LCD_DATA(rock[i]);
    } 
}

void draw_sky(uint8_t x, uint8_t y) {
    LCD_CMD(lcd_set_function, lcd_instr_basic | lcd_addr_horizontal);
    LCD_CMD(lcd_set_x_addr, x);
    LCD_CMD(lcd_set_y_addr, y);
    for (int i = 0; i < 8; i++) {
        LCD_DATA(sky[i]);
    }
}

// Uart communication init
void uart_init(unsigned int ubrr);


void setup(void) {
    // Game control button setup 
    CLEAR_BIT(DDRB, 0);
    // Game start button setup 
    CLEAR_BIT(DDRB, 1);
    // PB2 is builtin LED
    SET_BIT(DDRB, 2);

    // Timer 0 in normal mode, with pre-scaler 1024 ==> ~60Hz overflow.
    // Timer overflow on.
    TCCR0A = 0;
    TCCR0B = 5;
    TIMSK0 = 1;
        /*
    Alternatively:
        CLEAR_BIT(TCCR0B,WGM02);
        SET_BIT(TCCR0B,CS02);
        CLEAR_BIT(TCCR0B,CS01);
        SET_BIT(TCCR0B,CS00);
        SET_BIT(TIMSK0, TOIE0);
    */

    // Enable timer overflow, and turn on interrupts.
    sei();

    // initialise adc
    // ADC Enable and pre-scaler of 128: ref table 24-5 in datasheet
    // ADEN  = 1
    // ADPS2 = 1, ADPS1 = 1, ADPS0 = 1
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);

   // select channel and ref input voltage
   // channel 0, PC0 (A0 on the uno)
   // MUX0=0, MUX1=0, MUX2=0, MUX3=0
   // REFS0=1
   // REFS1=0
    ADMUX = (1 << REFS0);

    // set up the lcd 
    new_lcd_init(LCD_DEFAULT_CONTRAST);
    lcd_clear();
    
    setup_dinosaur();
    setup_background();
}

volatile int overflow_counter = 0;

ISR(TIMER0_OVF_vect) {
    overflow_counter ++;
}

void process_timer(void){
    char temp_buf[64];

    //compute elapsed time
    double time = ( overflow_counter * 256.0 + TCNT0 ) * PRESCALE  / FREQ;

    //convert float to a string
    dtostrf(time, 7, 3, temp_buf);
    strcpy(gaming_time, "Time=");
    strcat( gaming_time, temp_buf);
}

void process(void) {
    process_timer();
    lcd_clear();
    row = (row - 1) % (LCD_X + 8);
    x_b = (x_b - 1) % (LCD_X + 8);
    if( col == y && (row == x + 5)){
        // PORTB |= (1 << 5);
        game_over = 1;
    } else {
        // if press the game control button
        if ( BIT_IS_SET(PINB, 0)) {
            // Debouncing
            _delay_ms(1);
            while ( BIT_IS_SET(PINB, 0) ) {
            // Block until switch released.
            }
            _delay_ms(1);
            lcd_clear();
            y = y - 2;
            printf("JUMP!\n");
            
            SET_BIT(PORTB, 2);
        } else {
            CLEAR_BIT(PORTB, 2);
            draw_dinosaur_head(x + 3, y + 4);
            draw_dinosaur_body(x + 3, y + 4);

            draw_sky(x_b, y_b);
            draw_sky(x_b + 27, y_b);
            draw_sky(x_b + 7, y_b + 1);
            draw_sky(x_b + 9, y_b + 1);
            
            draw_rock(row, col + 4);
            draw_rock(row + 32, col + 4);
            y = 0;
        }

    }
}


void process_light(void) {
    // Start single conversion by setting ADSC bit in ADCSRA
    ADCSRA |= (1 << ADSC);
    // Wait for ADSC bit to clear, signalling conversion complete.
    while ( ADCSRA & (1 << ADSC) ) {}

    // Result now available in ADC
    uint16_t pot = ADC;

    //when converted value is above a threshold, perform an action
    control_distance = pot / 2;
}

int main(void) {
    // setting up Distance env_sensor
    // ECHO on B4
    // TRIG on B5
    hcsr04 distance_sensor;
    hcsr04_setup(&distance_sensor, &PORTB, 5, &PORTB, 4);
    double distance;
    setup();
    uart_init(MYUBRR);

    for (;;) {
        process_light();
       
        if (hcsr04_read(&distance_sensor, &distance)) {
            if((int) (distance * 1000) < control_distance) {
                // SET_BIT(PORTB, 3);
                if( game_over == 1 ) {
                    lcd_clear();
                    draw_string_direct((LCD_X - 6 * CHAR_WIDTH) / 2, CHAR_HEIGHT, "START!", FG_COLOUR);
                    draw_string_direct((LCD_X - 12 * CHAR_WIDTH) / 2, CHAR_HEIGHT + 10, gaming_time, FG_COLOUR);
                    row = 0;
                } 
                if ( game_over == 0 ){
                    process();
                    _delay_ms(70);
                }
                // press button for starting game
                if (BIT_IS_SET(PINB, 1)) {
                    game_over = 0;
                    printf("JUMP!\n");
                    lcd_clear();
                }
            }  
            else if ((int) (distance * 1000) > control_distance){
                // CLEAR_BIT(PORTB, 3);
                // _delay_ms(1000);
            }
        } else {}

        
    }
}

int uart_putch(char c, FILE *unused) {
    (void) unused;

    while (!(UCSR0A & (1 << UDRE0)));
    UDR0 = c;

    return 0;
}

FILE uart_output = FDEV_SETUP_STREAM(uart_putch, NULL, _FDEV_SETUP_WRITE);

void uart_init(unsigned int ubrr){
    UBRR0 = ubrr;
    
    //normal speed
    UCSR0A = (0 << U2X0);
    UCSR0B = (1 << RXEN0) | (1 << TXEN0) | (0 << UCSZ02);
    UCSR0C = (1 << UCSZ00) | (1 << UCSZ01);
    stdout = &uart_output;
}



