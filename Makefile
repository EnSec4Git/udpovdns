CC=gcc
CXX=g++
CFLAGS=-Wall

all: udpserver

udpserver:
	@gcc -o udp-server udp-server.c

clean:
	@rm udp-server

