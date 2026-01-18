CFLAGS = -Wall -Wextra -g3 -lm

pong: ping.c
	$(CC) $(CFLAGS) ping.c -o pong

.PHONY: run clean

run: pong
	./pong

clean:
	rm -f ./pong
