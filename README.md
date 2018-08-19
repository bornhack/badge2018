# Bornhack Badge 2018

## About the hardware

Like last year the Bornhack 2018 badge has a SiLabs Happy Gecko microcontroller,
but this year it also sports a Nordic nRF51 Bluetooth enabled microcontroller.

The Happy Gecko is a Cortex-M0+ with built in USB, specifically we are using
the EFM32HG322F64G. You can download the Happy Gecko [reference manual][HG-manual] and
[datasheet][HG-datasheet] directly from [SiliconLabs][silabs].

The Nordic chip as a nRF51822 that has a built-in Cortex-M0 that you can also program.

We have separated the hardware design files from the code and put them in the
[hardware branch][hardware] for you to have a look at and build on.

If you just need to check the pinout or want to see how stuff is connected, you can
have a look at the [schematic as a pdf file][badge-schematic].

We have also made a bunch of different add-on boards, and those are to be found in the
[breakoutboards branch][breakoutboards], which also includes a bit of information about
the parts needed for each boards.

[hardware]: https://github.com/bornhack/badge2018/tree/hardware
[breakoutboards]: https://github.com/bornhack/badge2018/tree/breakoutboards
[silabs]: https://www.silabs.com/
[HG-manual]: https://www.silabs.com/documents/public/reference-manuals/EFM32HG-RM.pdf
[HG-datasheet]: https://www.silabs.com/documents/public/data-sheets/EFM32HG322.pdf
[badge-schematic]: https://github.com/bornhack/badge2018/raw/hardware/schematic.pdf

## Binary firmware files for quick programming or restore

To program the nRF51822 radio controller, you can use the [nRF51 programmer][nrf51prog]
and if you don't want to compile that yourself, you can [download a binary][nrf51prog-bin]
ready to be programmed on your badge.

This piece of firmware runs at full speed and is not power optimized, so to have a decent
battery life, you can put back the default firmware on the Happy Gecko, when you have
finished programming the nRF51822.

For doing a restore of the firmware that the badge was delivered with, you can either
checkout and compile this [master branch][default-fw], or simply
[download the binary][default-fw-bin] version and program the badge with it using the
bootloader.

[nrf51prog]: https://github.com/bornhack/badge2018/tree/nrf51prog
[nrf51prog-bin]: https://esmil.dk/badge2018/nrf51prog.bin
[default-fw]: https://github.com/bornhack/badge2018/tree/master
[default-fw-bin]: https://esmil.dk/badge2018/test.bin

## Develop software for the Happy Gecko

### 1. Install dependencies

##### Archlinux
```sh
pacman -S arm-none-eabi-gcc arm-none-eabi-newlib make
```

##### Debian/Ubuntu
```sh
apt-get install gcc-arm-none-eabi libnewlib-arm-none-eabi make
```
Unfortunately the toolchain in Ubuntu Trusty 14.04 is too old to work out of the box.

##### Fedora
```sh
dnf install arm-none-eabi-gcc arm-none-eabi-newlib make
```

##### OSX

Download an arm-none-eabi toolchain from ARM [here][arm-toolchain].
Unpack the tarball and update your PATH variable to point to the unpacked `bin` directory.

##### Windows
###### Option 1
Download the apropriate installer from ARM [here][arm-toolchain].
Install it and update your path.
You'll also need GNU Make installed.
A pre-built version can be downloaded [here](http://gnuwin32.sourceforge.net/packages/make.htm).

###### Option 2

Use Windows Subsystem for Linux with Ubuntu 16.04 Xenial or newer and proceed as on Ubuntu above.

### 2. Get the source code

If you already have git installed
```
git clone https://github.com/bornhack/badge2018.git
cd badge2018
```

Otherwise you can download a tarball or zip file from
[https://github.com/bornhack/badge2018](https://github.com/bornhack/badge2018)

### 3. Build the code
Simply type `make` in the downloaded directory.
If the build fails (fx. on Debian Jessie and Ubuntu Xenial)
try editing the `Makefile` and uncommenting the line which says `OLD=1`.

If there are no compilation errors this will result in a `code.bin` in the `out` directory.

[arm-toolchain]: https://developer.arm.com/open-source/gnu-toolchain/gnu-rm/downloads

### 4. Program the badge with your compiled code

###### Option 1

The [bootloader](https://github.com/flummer/geckoboot) emulates a USB mass storage device
with a fat12 filesystem. Writing a .bin file to this filesystem will program the chip.
To use it:

1. Connect the USB cable to the board and your computer.
2. Press the **BOOT** button on the badge.
3. Copy the generated `out/code.bin`-file to the `GECKOBOOT` USB stick that appeared on your computer.
4. Eject (or unmount) the USB stick, and watch your code run.

###### Option 2

Alternatively you can install a program called *dfu-util*. With this program installed
you can simply put the badge in bootloader mode by pressing the **BOOT** button,
and then type `make dfu`. Usually your dfu-util package comes with rules to give
your user (and hence dfu-util) access to the raw USB device. If not you might need
`sudo make dfu`.

