# Try uncommenting this if the build fails
#OLD = 1

include geckonator/include.mk

CHIP = EFM32HG322F64

sources += swd-usart1.c
sources += swd.c
sources += ihex.c
sources += nRF51prog.c

ihex: ihex.c
	gcc -o '$@' -O2 -Wall -Wextra -DSTANDALONE '$^'
