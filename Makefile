fractal:fractal.c
	gcc -Wall -g -o $@ $^ -std=c99 -pthread -lglut -lGL -lGLU -lGLEW -lm
.PHONY:run clean
run:fractal
	./fractal
clean:
	rm -f fractal
