CC      = gcc
CFLAGS  = -Wall -Werror
TARGET  = project
SRCS    = main.c process.c sim.c

$(TARGET): $(SRCS) process.h sim.h
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) -lm

clean:
	rm -f $(TARGET)
