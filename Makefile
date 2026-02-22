# Makefile for Portfolio network tools

CC=wcl
CFLAGS=-bt=dos -ms -wx -we -zq

all: webserver.exe

webserver.exe: webserver.c network.c network.h
	$(CC) $(CFLAGS) -fe=webserver.exe webserver.c network.c

clean:
	rm -f *.com *.exe *.obj *.err

.PHONY: all clean
