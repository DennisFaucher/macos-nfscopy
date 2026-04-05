# nfscopy Makefile
#
# By default looks for libnfs under /usr/local (Homebrew or manual install).
# Override with: make LIBNFS_PREFIX=/path/to/libnfs
#
# To build against a locally compiled libnfs repo (not installed):
#   make LIBNFS_PREFIX=/path/to/libnfs/repo  LIBNFS_LIB=/path/to/libnfs/repo/lib/.libs

LIBNFS_PREFIX ?= /usr/local
LIBNFS_LIB    ?= $(LIBNFS_PREFIX)/lib

CC      = cc
CFLAGS  = -Wall -Wextra -O2 -I$(LIBNFS_PREFIX)/include
# Link the static archive directly so the binary has no dylib runtime dependency
LDFLAGS = $(LIBNFS_LIB)/libnfs.a

TARGET  = nfscopy

.PHONY: all clean install

all: $(TARGET)

$(TARGET): nfscopy.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/$(TARGET)
	@echo "Installed to /usr/local/bin/$(TARGET)"

clean:
	rm -f $(TARGET)
