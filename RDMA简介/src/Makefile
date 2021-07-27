CC = gcc
CFLAGS = -std=c99 -O2

all: tcp_latency tcp_bandwidth

tcp_latency: tcp_latency.c
	$(CC) $(CFLAGS) -o $@ $^

tcp_bandwidth: tcp_bandwidth.c
	$(CC) $(CFLAGS) -o $@ $^
