all: loghup

loghup: loghup.c
	gcc -Wall -g -o loghup loghup.c

install: loghup
	install -m 0755 loghup /usr/local/bin

clean:
	rm -f *.o loghup
