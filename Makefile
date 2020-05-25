all: squirt squirtd.amiga squirtd.unix
CFLAGS=-fsanitize=address -fsanitize=undefined -O2 -Wall

squirt: squirt.c squirt.h Makefile
	gcc $(CFLAGS) squirt.c -o squirt -lncurses

squirtd.unix: squirtd.c squirt.h Makefile
	gcc $(CFLAGS) squirtd.c -o squirtd.unix

squirtd.amiga: squirtd.c squirt.h Makefile
	vc -O1 -DAMIGA -static -c99 +aos68k squirtd.c -o squirtd.amiga -lauto

clean:
	rm -f squirt squirtd.unix squirtd.amiga
