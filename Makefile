CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra
LDLIBS  ?= -lncurses

TARGET  := bgtop
SRC     := bgtop.c

PREFIX  ?= /usr/local
BINDIR  := $(PREFIX)/bin
LIBDIR  := $(PREFIX)/lib/bosgametop

SYSTEMD_DIR ?= /etc/systemd/system

.PHONY: all clean install uninstall install-fan-profile uninstall-fan-profile

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDLIBS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

install-fan-profile:
	install -Dm755 scripts/apply-fan-profile.sh \
		$(DESTDIR)$(LIBDIR)/apply-fan-profile.sh
	install -Dm644 systemd/bosgametop-fan-profile.service \
		$(DESTDIR)$(SYSTEMD_DIR)/bosgametop-fan-profile.service

uninstall-fan-profile:
	rm -f $(DESTDIR)$(LIBDIR)/apply-fan-profile.sh
	rm -f $(DESTDIR)$(SYSTEMD_DIR)/bosgametop-fan-profile.service