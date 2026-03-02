CC ?= gcc
CFLAGS ?= -O2 -fPIC -Wall -Wextra
LDFLAGS ?= -shared -ldl

TARGET = libaa_redirect.so
SRC = aa_redirect.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
