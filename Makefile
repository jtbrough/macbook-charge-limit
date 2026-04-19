PREFIX ?= /usr/local
SYSCONFDIR ?= /etc
SYSTEMDUNITDIR ?= /etc/systemd/system

CC ?= cc
CFLAGS ?= -O2 -g
CPPFLAGS ?=
LDFLAGS ?=

BIN := macbook-charge-limit
SRC := src/macbook-charge-limit.c
SERVICE := macbook-charge-limit.service
SERVICE_IN := packaging/systemd/macbook-charge-limit.service.in

.PHONY: all clean install uninstall check

all: $(BIN) $(SERVICE)

$(BIN): $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wall -Wextra -o $@ $< $(LDFLAGS)

$(SERVICE): $(SERVICE_IN)
	sed \
		-e 's|@PREFIX@|$(PREFIX)|g' \
		-e 's|@SYSCONFDIR@|$(SYSCONFDIR)|g' \
		$< > $@

check: $(BIN)
	./$(BIN) >/dev/null 2>&1; test $$? -eq 2
	./$(BIN) --help >/dev/null 2>&1
	./$(BIN) set 10 >/dev/null 2>&1; test $$? -eq 2

install: $(BIN) $(SERVICE)
	install -Dm0755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)
	install -Dm0644 $(SERVICE) $(DESTDIR)$(SYSTEMDUNITDIR)/$(SERVICE)
	if test ! -e $(DESTDIR)$(SYSCONFDIR)/macbook-charge-limit.conf; then \
		install -Dm0644 examples/macbook-charge-limit.conf $(DESTDIR)$(SYSCONFDIR)/macbook-charge-limit.conf; \
	fi

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)
	rm -f $(DESTDIR)$(SYSTEMDUNITDIR)/macbook-charge-limit.service

clean:
	rm -f $(BIN) $(SERVICE)
