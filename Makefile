all: squirt squirtd.amiga squirtd.unix
CFLAGS=-fsanitize=address -fsanitize=undefined -O2 -Wall

squirt: squirt.c Makefile
	gcc $(CFLAGS) squirt.c -o squirt

squirtd.unix: squirtd.c Makefile
	gcc $(CFLAGS) squirtd.c -o squirtd.unix

squirtd.amiga: squirtd.c Makefile
	vc -DAMIGA -static -c99 +aos68k squirtd.c -o squirtd.amiga -lauto

clean:
	rm -f squirt squirtd.unix squirtd.amiga
