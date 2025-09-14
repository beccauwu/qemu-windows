PREFIX ?= /usr/local
OUTFILE ?= qemu-windows
CFLAGS ?= -Wall -Wextra -I.
LDFLAGS ?= 
$(OUTFILE): main.c
	$(CC) $(CFLAGS) -o $(OUTFILE) $< $(LDFLAGS)

.PHONY: install
install: $(OUTFILE)
	cp -f $< "$(PREFIX)/bin/$(OUTFILE)"

.PHONY: uninstall
uninstall:
	rm "$(PREFIX)/bin/$(OUTFILE)"

.PHONY: clean
clean:
	rm ./$(OUTFILE)