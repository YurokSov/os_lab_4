all: main.c
	gcc -Werror -pedantic main.c -o main -lpthread
clean:
	rm main