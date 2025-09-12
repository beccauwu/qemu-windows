PREFIX ?= /usr/local
school: main.c
	gcc -I. -o school $<

.PHONY: install
install: school
	cp -f $< "$(PREFIX)/bin/school"

.PHONY: clean
clean:
	rm "$(PREFIX)/bin/school"
	rm ./school