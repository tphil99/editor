all: editor

editor: term.c process.c main.c highlights.c draw.c
	$(CC) -o editor *.c -std=c99

clean:
	rm editor
