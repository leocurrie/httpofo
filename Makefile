# Makefile for Portfolio network tools

CC=wcl
CFLAGS=-bt=dos -ms -wx -we -zq

all: httpofo.exe

httpofo.exe: httpofo.c network.c network.h
	$(CC) $(CFLAGS) -fe=httpofo.exe httpofo.c network.c

clean:
	rm -f *.com *.exe *.obj *.err

.PHONY: all clean
