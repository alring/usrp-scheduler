CC=g++
CFLAGS=-I.
DEPS = usrp_scheduler.h

%.o: %.cc $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

all: usrp_scheduler.o
	g++ -o usrp_scheduler usrp_scheduler.o -I.
