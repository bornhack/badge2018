#ifndef _DISPLAY
#define _DISPLAY

#include <stdint.h>

typedef struct display display;

void i2c0_init(void);

int display_init(void);
int display_on(void);
int display_off(void);
int display_update(void);
void display_clear(void);

void display_text_location(uint8_t x, uint8_t y);
void display_puts(char* const txt);


#endif
