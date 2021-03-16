mlisp: outdirs
	gcc -std=c99 -Wall main.c mpc.c -ledit -lm -o build/mlisp

outdirs:
	mkdir -p build/ bin/


clean:
	rm -rf bin/ build/
