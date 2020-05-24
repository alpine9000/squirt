all: squirtd squirt
CFLAGS=-fsanitize=address -fsanitize=undefined -O2 -Wall

squirt: squirt.c
	gcc $(CFLAGS) squirt.c -o squirt

squirtd: squirtd.c
	vc -sd -O4 -c99 +aos68k squirtd.c -o squirtd -lautos
	cp squirtd squirtd.1

clean:
	rm squirtd squirt squirtd.1
