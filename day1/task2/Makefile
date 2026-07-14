# 目标文件为 fmt
fmt: fmt.o word.o line.o
	gcc -g -o fmt fmt.o word.o line.o

fmt.o: fmt.c word.h line.h
	gcc -g -c fmt.c

word.o: word.c word.h
	gcc -g -c word.c

line.o: line.c line.h
	gcc -g -c line.c

clean:
	rm -f fmt *.o