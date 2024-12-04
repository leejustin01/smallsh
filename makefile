smallsh: main.o smallsh.o
	gcc main.o smallsh.o -o smallsh

main.o: main.c smallsh.h
	gcc -c main.c -o main.o

smallsh.o: smallsh.c
	gcc -c smallsh.c -o smallsh.o

clean:
	rm -f *.o

cleanall:
	rm -f *.o smallsh