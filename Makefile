mlisp: binaries
	gcc -std=c99 -Wall -ledit -lm bin/mpc.o bin/main.o bin/stdlib.o -o build/mlisp

binaries: outdirs
	ld -r -b binary -o bin/stdlib.o stdlib.mlisp
	gcc -std=c99 -Wall -ledit -lm -c main.c -o bin/main.o
	gcc -std=c99 -Wall -ledit -lm -c mpc.c -o bin/mpc.o

outdirs:
	mkdir -p build/ bin/

clean:
	rm -rf bin/ build/
