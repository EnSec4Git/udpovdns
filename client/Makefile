CC=gcc
CXX=g++
CFLAGS=-Wall

all: example

example: base32 socket_dns
	@gcc -Wall -g -o bin/udp-client udp-client.c bin/socket_dns.o bin/libbase32.a -lstdc++ -lm
	@cp extract.pl bin/extract.pl
	@cp extract_and_decode.pl bin/extract_and_decode.pl

base32: base32.o
	@ar rcs bin/libbase32.a bin/base32.o

base32.o: mkbin
	@$(CC) -Wall -g -std=c99 -c -o bin/base32.o base32.c

socket_dns: mkbin
	@$(CXX) -g -o bin/socket_dns.o -c -Wall socket_dns.cpp
	
mkbin:
	@mkdir -p bin

clean:
	@rm -rf bin/*o bin/example bin/libbase32.a bin/udp-client
	@rm -rf bin/*.pl
