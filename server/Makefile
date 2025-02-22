#
# Example commands:
# make
# CROSS_COMPILE=aarch64-none-linux-gnu- make
# make clean
#

# breaks yocto
# ifdef CROSS_COMPILE
# CC := ${CROSS_COMPILE}gcc
# else
# CC := gcc
# endif

.DEFAULT_GOAL := aesdsocket



aesdsocket.o:
	${CC} -c aesdsocket.c -I. -Wall -g

.PHONY: all clean

all: aesdsocket

aesdsocket: aesdsocket.o

clean:
	rm -f aesdsocket.o aesdsocket
