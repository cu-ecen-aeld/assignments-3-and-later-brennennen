#
# Example commands:
# make
# CROSS_COMPILE=aarch64-none-linux-gnu- make
# make clean
#

# setting CC breaks yocto
# ifdef CROSS_COMPILE
# CC := ${CROSS_COMPILE}gcc
# else
# CC := gcc
# endif

.DEFAULT_GOAL := aesdsocket

# debug symbols break yocto
#${CC} -c aesdsocket.c -I. -Wall -g
aesdsocket.o:
	${CC} -c aesdsocket.c -I. -Wall

.PHONY: all clean

all: aesdsocket

aesdsocket: aesdsocket.o

clean:
	rm -f aesdsocket.o aesdsocket
