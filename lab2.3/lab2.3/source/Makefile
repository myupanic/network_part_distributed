all: three
two:
	gcc -g -std=gnu99 -o server server1/*.c *.c -Iserver1 -lpthread -lm
	gcc -g -std=gnu99 -o client client1/*.c *.c -Iclient1 -lpthread -lm
three: two
	gcc -g -std=gnu99 -o server server2/*.c *.c -Iserver2 -lpthread -lm

clean:
	rm client
	rm server
