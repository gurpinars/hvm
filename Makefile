OBJS = utils.o
TARGET = hvm
CC = gcc
MODE = -std=c99
DEBUG = -g
DEPS = utils.h
CFLAGS =  -Wall -c $(DEBUG)
LFLAGS =  -Wall $(DEBUG)

%.o: %.c $(DEPS)
	$(CC) $< $(CFLAGS) $(MODE)
all: $(OBJS)
	$(CC) hvm.c $^ $(LFLAGS) -o $(TARGET) $(MODE)
clean:
	rm -f *.o hvm
