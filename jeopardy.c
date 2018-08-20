#include "geckonator/display.h"
#include "jeopardy.h"

static volatile int S_events_a;
static volatile int S_events_b;
static volatile int S_trigger;

static volatile int S_delay_cycles;


static const int ACTIVATION_DELAY = 3   ;


inline static void write_screen(uint8_t row, char *const txt) {
    display_clear();
    display_text_location(0, row);
    display_puts(txt);
    display_update();
}


void jeopardy_init(void) {
    S_events_a = 0;
    S_events_b = 0;
    S_trigger = 0;
    S_delay_cycles = 0;
    write_screen(0, "Welcome");
}

void jeopardy_handle_a(int state) {
    if (S_delay_cycles > 0) {
        return;
    }
    if (state > 0) {
        S_events_a++;
        S_delay_cycles = ACTIVATION_DELAY;        
    }
}

void jeopardy_handle_b(int state) {
    if (S_delay_cycles > 0) {
        return;
    }
    if (state > 0) {
        S_events_b++;
        S_delay_cycles = ACTIVATION_DELAY;        
    }
}


#include "badge2018.h"
#include "geckonator/gpio.h"

void jeopardy_mainloop(void) {
    if (S_events_a > 0) {
        S_delay_cycles = ACTIVATION_DELAY;
        S_events_a = 0;
        gpio_clear(LED_A);
        write_screen(0, "Pressed button");
    }
    if (S_events_b > 0) {
        S_delay_cycles = ACTIVATION_DELAY;
        S_events_b = 0;
        gpio_clear(LED_B);
        write_screen(0, "Reset");
    }
    if (S_delay_cycles > 0) {
        S_delay_cycles--;
        if (S_delay_cycles == 0) {
            gpio_set(LED_A);
            gpio_set(LED_B);
        }
    }
}
