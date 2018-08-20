# Try uncommenting this if the build fails
#OLD = 1

include geckonator/include.mk

CHIP = EFM32HG322F64

sources += i2c0.c
sources += display.c
