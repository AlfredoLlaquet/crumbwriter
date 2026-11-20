# BSD make and GNU make. Override CC, CFLAGS, PREFIX or DESTDIR as usual.
PREFIX ?= /usr/local
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2
GTK_CFLAGS != $(PKG_CONFIG) --cflags gtk+-3.0 gtksourceview-4
GTK_LIBS != $(PKG_CONFIG) --libs gtk+-3.0 gtksourceview-4
CORE_CFLAGS != $(PKG_CONFIG) --cflags gio-2.0
CORE_LIBS != $(PKG_CONFIG) --libs gio-2.0
FLAGS = -std=c11 -Wall -Wextra -Wpedantic -Isrc $(CPPFLAGS) $(CFLAGS)
OBJECTS = src/main.o src/window.o src/preferences.o src/editor.o src/search.o \
          src/typography.o src/crumb.o src/storage.o src/draft.o
HEADERS = src/app.h src/window.h src/preferences.h src/editor.h src/search.h \
          src/typography.h src/crumb.h src/storage.h src/draft.h

all: crumbwriter

crumbwriter: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(GTK_LIBS) $(LDLIBS)

$(OBJECTS): $(HEADERS)

.c.o:
	$(CC) $(FLAGS) $(GTK_CFLAGS) -c $< -o $@

tests/test-crumb: tests/test-crumb.c src/crumb.c src/crumb.h
	$(CC) $(FLAGS) $(CORE_CFLAGS) $(LDFLAGS) -o $@ tests/test-crumb.c src/crumb.c $(CORE_LIBS) $(LDLIBS)

tests/test-storage: tests/test-storage.c src/storage.c src/storage.h
	$(CC) $(FLAGS) $(CORE_CFLAGS) $(LDFLAGS) -o $@ tests/test-storage.c src/storage.c $(CORE_LIBS) $(LDLIBS)

tests/test-editor: tests/test-editor.c src/editor.o src/search.o src/crumb.o
	$(CC) $(FLAGS) $(GTK_CFLAGS) $(LDFLAGS) -o $@ tests/test-editor.c src/editor.o src/search.o src/crumb.o $(GTK_LIBS) $(LDLIBS)

tests/test-typography: tests/test-typography.c src/typography.o
	$(CC) $(FLAGS) $(GTK_CFLAGS) $(LDFLAGS) -o $@ tests/test-typography.c src/typography.o $(GTK_LIBS) $(LDLIBS)

check-core: tests/test-crumb tests/test-storage
	./tests/test-crumb
	./tests/test-storage

check-gui: tests/test-editor tests/test-typography
	./tests/test-editor
	./tests/test-typography

check: check-core check-gui

install: crumbwriter
	install -d "$(DESTDIR)$(PREFIX)/bin" "$(DESTDIR)$(PREFIX)/share/applications" "$(DESTDIR)$(PREFIX)/share/doc/crumbwriter"
	install -m 755 crumbwriter "$(DESTDIR)$(PREFIX)/bin/crumbwriter"
	install -m 644 data/org.thecrumbs.CrumbWriter.desktop "$(DESTDIR)$(PREFIX)/share/applications/"
	install -m 644 CrumbWriter.txt "$(DESTDIR)$(PREFIX)/share/doc/crumbwriter/"

clean:
	rm -f $(OBJECTS) crumbwriter tests/test-crumb tests/test-storage tests/test-editor tests/test-typography

.PHONY: all check check-core check-gui install clean
