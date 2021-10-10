getdev.out: getdev.cpp
	g++ getdev.cpp -o getdev.out -lwayland-client 
kbdcros.out: kbdcros.cpp
	g++ kbdcros.cpp -o kbdcros.out -pthread
macrokeys.out: macrokeys.cpp
	g++ macrokeys.cpp -o macrokeys.out -pthread
test.out: test.c
	gcc test.c -o test.out -lpthread
.PHONY:clean
clean:
	rm *.out -rf
