CC      = gcc
CFLAGS  = -Wall -Werror
TARGET  = project
SRCS    = main.c process.c

$(TARGET): $(SRCS) process.h
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) -lm

clean:
	rm -f $(TARGET)
