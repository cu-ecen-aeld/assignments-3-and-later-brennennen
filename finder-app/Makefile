# 
# Example commands:
# make
# CROSS_COMPILE=aarch64-none-linux-gnu- make
# make clean
#

ifdef CROSS_COMPILE
CC := ${CROSS_COMPILE}gcc
else
CC := gcc
endif

.DEFAULT_GOAL := writer

writer.o:
	${CC} -c writer.c

.PHONY: clean

clean:
	rm -f writer.o writer
