# BSD make and GNU make. Override CC, CFLAGS, PREFIX or DESTDIR as usual.
PREFIX ?= /usr/local
PKG_CONFIG ?= pkg-config
GLIB_COMPILE_RESOURCES ?= glib-compile-resources
CFLAGS ?= -O2
GTK_CFLAGS != $(PKG_CONFIG) --cflags gtk+-3.0 gtksourceview-4
GTK_LIBS != $(PKG_CONFIG) --libs gtk+-3.0 gtksourceview-4
CORE_CFLAGS != $(PKG_CONFIG) --cflags gio-2.0
CORE_LIBS != $(PKG_CONFIG) --libs gio-2.0
ICON_FILES = data/icons/*/apps/org.thecrumbs.CrumbWriter.png
FLAGS = -std=c11 -Wall -Wextra -Wpedantic -Isrc $(CPPFLAGS) $(CFLAGS)
SEARCH_OBJECTS = src/search.o src/crumb.o src/reveal.o src/editor-spacing.o src/spacing.o
EDITOR_OBJECTS = src/editor.o src/genres.o src/virtual.o $(SEARCH_OBJECTS)
OBJECTS = src/main.o src/window.o src/preferences.o src/shortcuts.o src/typography.o \
          src/storage.o src/draft.o $(EDITOR_OBJECTS) data/crumbwriter-resources.o
HEADERS = src/app.h src/window.h src/preferences.h src/editor.h src/search.h \
          src/shortcuts.h src/typography.h src/crumb.h src/storage.h src/draft.h src/genres.h \
          src/reveal.h src/virtual.h src/editor-spacing.h src/spacing.h
GUI_TESTS = tests/test-editor tests/test-typography tests/test-genres tests/test-numbering \
            tests/test-search-controls tests/test-shortcuts tests/test-virtual tests/test-viewport

all: crumbwriter

crumbwriter: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(GTK_LIBS) $(LDLIBS)

$(OBJECTS): $(HEADERS)

.c.o:
	$(CC) $(FLAGS) $(GTK_CFLAGS) -c $< -o $@

data/crumbwriter-resources.c: data/crumbwriter.gresource.xml $(ICON_FILES)
	$(GLIB_COMPILE_RESOURCES) --sourcedir=data --generate-source --internal --target=$@ data/crumbwriter.gresource.xml

data/crumbwriter-resources.o: data/crumbwriter-resources.c
	$(CC) $(FLAGS) -Wno-overlength-strings $(CORE_CFLAGS) -c data/crumbwriter-resources.c -o $@

tests/test-crumb: tests/test-crumb.c src/crumb.c src/crumb.h
	$(CC) $(FLAGS) $(CORE_CFLAGS) $(LDFLAGS) -o $@ tests/test-crumb.c src/crumb.c $(CORE_LIBS) $(LDLIBS)

tests/test-storage: tests/test-storage.c src/storage.c src/storage.h src/spacing.c src/spacing.h
	$(CC) $(FLAGS) $(CORE_CFLAGS) $(LDFLAGS) -o $@ tests/test-storage.c src/storage.c src/spacing.c $(CORE_LIBS) $(LDLIBS)

tests/test-editor: tests/test-editor.c $(EDITOR_OBJECTS)
	$(CC) $(FLAGS) $(GTK_CFLAGS) $(LDFLAGS) -o $@ tests/test-editor.c $(EDITOR_OBJECTS) $(GTK_LIBS) $(LDLIBS)

tests/test-typography: tests/test-typography.c src/typography.o src/editor-spacing.o src/spacing.o
	$(CC) $(FLAGS) $(GTK_CFLAGS) $(LDFLAGS) -o $@ tests/test-typography.c src/typography.o src/editor-spacing.o src/spacing.o $(GTK_LIBS) $(LDLIBS)

tests/test-genres: tests/test-genres.c src/genres.o
	$(CC) $(FLAGS) $(GTK_CFLAGS) $(LDFLAGS) -o $@ tests/test-genres.c src/genres.o $(GTK_LIBS) $(LDLIBS)

tests/test-numbering: tests/test-numbering.c $(EDITOR_OBJECTS)
	$(CC) $(FLAGS) $(GTK_CFLAGS) $(LDFLAGS) -o $@ tests/test-numbering.c $(EDITOR_OBJECTS) $(GTK_LIBS) $(LDLIBS)

tests/test-search-controls: tests/test-search-controls.c $(SEARCH_OBJECTS)
	$(CC) $(FLAGS) $(GTK_CFLAGS) $(LDFLAGS) -o $@ tests/test-search-controls.c $(SEARCH_OBJECTS) $(GTK_LIBS) $(LDLIBS)

tests/test-shortcuts: tests/test-shortcuts.c src/shortcuts.o
	$(CC) $(FLAGS) $(GTK_CFLAGS) $(LDFLAGS) -o $@ tests/test-shortcuts.c src/shortcuts.o $(GTK_LIBS) $(LDLIBS)

tests/test-virtual: tests/test-virtual.c $(EDITOR_OBJECTS) src/typography.o
	$(CC) $(FLAGS) $(GTK_CFLAGS) $(LDFLAGS) -o $@ tests/test-virtual.c $(EDITOR_OBJECTS) src/typography.o $(GTK_LIBS) $(LDLIBS)

tests/test-viewport: tests/test-viewport.c src/reveal.o
	$(CC) $(FLAGS) $(GTK_CFLAGS) $(LDFLAGS) -o $@ tests/test-viewport.c src/reveal.o $(GTK_LIBS) $(LDLIBS)

check-core: tests/test-crumb tests/test-storage
	./tests/test-crumb
	./tests/test-storage

check-gui: $(GUI_TESTS)
	./tests/test-editor
	./tests/test-typography
	./tests/test-genres
	./tests/test-numbering
	./tests/test-search-controls
	./tests/test-shortcuts
	./tests/test-virtual
	./tests/test-viewport

check: check-core check-gui

install: crumbwriter
	install -d "$(DESTDIR)$(PREFIX)/bin" "$(DESTDIR)$(PREFIX)/share/applications" "$(DESTDIR)$(PREFIX)/share/doc/crumbwriter"
	install -m 755 crumbwriter "$(DESTDIR)$(PREFIX)/bin/crumbwriter"
	install -m 644 data/org.thecrumbs.CrumbWriter.desktop "$(DESTDIR)$(PREFIX)/share/applications/"
	@set -e; for icon in $(ICON_FILES); do \
		path=$${icon#data/icons/}; \
		dir="$(DESTDIR)$(PREFIX)/share/icons/hicolor/$${path%/*}"; \
		install -d "$$dir"; \
		install -m 644 "$$icon" "$$dir/"; \
	done
	touch "$(DESTDIR)$(PREFIX)/share/icons/hicolor"
	install -m 644 CrumbWriter.txt "$(DESTDIR)$(PREFIX)/share/doc/crumbwriter/"

clean:
	rm -f $(OBJECTS) data/crumbwriter-resources.c crumbwriter tests/test-crumb tests/test-storage $(GUI_TESTS)

.PHONY: all check check-core check-gui install clean
