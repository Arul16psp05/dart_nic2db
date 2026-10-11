CC = gcc
CFLAGS = -O3 -Wall -Wextra -pthread
# Adjust these paths if your PSRDADA installation is in a custom directory
INCLUDES = -I/usr/local/include
LDFLAGS = -L/usr/local/lib
LIBS = -lpsrdada -lm

TARGET = dart_nic2db
SRC = dart_nic2db.c
OBJ = $(SRC:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

%.o: %.c dart_nic2db.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)