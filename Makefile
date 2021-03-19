NATIVE_CC = gcc
CC = gcc
CFLAGS = -std=c99  -Wall -ledit -lm -O2

mlisp: binaries
	$(CC) $(CFLAGS) bin/mpc.o bin/main.o bin/stdlib.o -o build/mlisp

binaries: outdirs
	$(NATIVE_CC) ./util/hexembed.c -o ./build/hexembed
	./build/hexembed ./stdlib.mlisp stdlib_mlisp > ./temp/stdlib_mlisp.c
	$(CC) $(CFLAGS) -c ./temp/stdlib_mlisp.c -o bin/stdlib.o
	$(CC) $(CFLAGS) -c main.c -o bin/main.o
	$(CC) $(CFLAGS) -c mpc.c -o bin/mpc.o

mlisp_wasm: outdirs
	$(NATIVE_CC) ./util/hexembed.c -o ./build/hexembed
	./build/hexembed ./stdlib.mlisp stdlib_mlisp > ./temp/stdlib_mlisp.c
	emcc -std=c99  -Wall -O3 -s WASM=1 -s EXTRA_EXPORTED_RUNTIME_METHODS='["cwrap"]' -c ./temp/stdlib_mlisp.c -o bin/stdlib.o
	emcc -std=c99  -Wall -O3 -s WASM=1 -s EXTRA_EXPORTED_RUNTIME_METHODS='["cwrap"]' -c main.c -o bin/main.o
	emcc -std=c99  -Wall -O3 -s WASM=1 -s EXTRA_EXPORTED_RUNTIME_METHODS='["cwrap"]' -c mpc.c -o bin/mpc.o
	emcc -std=c99  -Wall -O3 -s WASM=1 -s EXTRA_EXPORTED_RUNTIME_METHODS='["cwrap"]' bin/mpc.o bin/main.o bin/stdlib.o -o build/mlisp.js

outdirs:
	mkdir -p build/ bin/ temp/

clean:
	rm -rf bin/ build/ temp/
