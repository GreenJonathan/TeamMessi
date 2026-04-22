CC      = gcc
CFLAGS  = -Wall -Werror
TARGET  = project
SRCS    = main.c process.c sim.c stats.c

$(TARGET): $(SRCS) process.h sim.h stats.h
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) -lm

clean:
	rm -f $(TARGET) simout.txt
