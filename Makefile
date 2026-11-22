# FreeBSD base make and GNU make. Qt 6 supplies the desktop controls.
PREFIX ?= /usr/local
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2
CXXFLAGS ?= -O2
QT_CFLAGS != $(PKG_CONFIG) --cflags Qt6Widgets Qt6Network
QT_LIBS != $(PKG_CONFIG) --libs Qt6Widgets Qt6Network
HUNSPELL_CFLAGS != $(PKG_CONFIG) --cflags hunspell
HUNSPELL_LIBS != $(PKG_CONFIG) --libs hunspell
QT_TOOL_DIR != $(PKG_CONFIG) --variable=libexecdir Qt6Core
RCC ?= $(QT_TOOL_DIR)/rcc
CORE_CFLAGS != $(PKG_CONFIG) --cflags gio-2.0
CORE_LIBS != $(PKG_CONFIG) --libs gio-2.0
C_FLAGS = -std=c11 -Wall -Wextra -Wpedantic -Isrc $(CORE_CFLAGS) $(CPPFLAGS) $(CFLAGS)
CXX_FLAGS = -std=c++17 -fPIC -pthread -Wall -Wextra -Wpedantic -DQT_NO_KEYWORDS -Isrc -Isrc/qt \
            $(QT_CFLAGS) $(HUNSPELL_CFLAGS) $(CORE_CFLAGS) $(CPPFLAGS) $(CXXFLAGS)
LIBS = $(QT_LIBS) $(HUNSPELL_LIBS) $(CORE_LIBS) -pthread $(LDLIBS)

CORE_OBJECTS = src/crumb.o src/spacing.o src/storage.o src/storage-posix.o src/draft.o
CORE_HEADERS = src/crumb.h src/spacing.h src/storage.h src/storage-platform.h src/draft.h
MODEL_OBJECTS = src/qt/crumbmodel.o src/qt/dailycount.o src/qt/manuscripttext.o
EDITOR_OBJECTS = $(MODEL_OBJECTS) src/qt/manuscripteditor.o src/qt/editorinput.o \
                 src/qt/editorviewport.o src/qt/committedpunctuation.o src/qt/vimotion.o src/qt/viselection.o
SEARCH_OBJECTS = $(EDITOR_OBJECTS) src/qt/searchpanel.o src/qt/searchengine.o src/qt/searchhistory.o
BOOKMARK_OBJECTS = src/qt/bookmarks.o src/qt/bookmarkfile.o src/qt/bookmarkanchors.o src/qt/manuscripttext.o
SYNONYM_OBJECTS = src/qt/textcompanion.o src/qt/synonymfile.o src/qt/synonympanel.o src/qt/synonymresults.o src/qt/synonymmatch.o src/qt/panelsearch.o
SPELL_OBJECTS = src/qt/textcompanion.o src/qt/spellcheck.o src/qt/spellreview.o src/qt/spellwords.o
STORE_OBJECTS = src/qt/documentstore.o
QT_SOURCES != find src/qt -name '*.cpp' ! -name 'main.cpp' | sort
QT_HEADERS != find src/qt -name '*.h' | sort
QT_OBJECTS = $(QT_SOURCES:.cpp=.o)
RESOURCE = data/qt-icons.o
ICON_FILES = data/icons/*/apps/org.thecrumbs.CrumbWriter.png
APP_OBJECTS = $(QT_OBJECTS) $(CORE_OBJECTS) $(RESOURCE)
QT_TESTS = tests/qt/test-model tests/qt/test-documentstore tests/qt/test-bookmarks \
           tests/qt/test-editor tests/qt/test-search tests/qt/test-navigation tests/qt/test-synonyms tests/qt/test-spelling \
           tests/qt/test-window tests/qt/test-vi tests/qt/test-resize tests/qt/test-symbols \
           tests/qt/test-synonymwriting tests/qt/test-appearance tests/qt/test-layout

all: crumbwriter

crumbwriter: src/qt/main.o $(APP_OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ src/qt/main.o $(APP_OBJECTS) $(LIBS)

.SUFFIXES: .cpp .c .o
.c.o:
	$(CC) $(C_FLAGS) -c $< -o $@
.cpp.o:
	$(CXX) $(CXX_FLAGS) -c $< -o $@

$(CORE_OBJECTS): $(CORE_HEADERS)
src/qt/main.o $(QT_OBJECTS): $(QT_HEADERS) $(CORE_HEADERS)

data/qt-icons.cpp: data/crumbwriter.qrc data/symbols.tsv $(ICON_FILES) LICENSE
	$(RCC) --name crumbwriter -o $@ data/crumbwriter.qrc

tests/test-crumb: tests/test-crumb.c src/crumb.o
	$(CC) $(C_FLAGS) $(LDFLAGS) -o $@ tests/test-crumb.c src/crumb.o $(CORE_LIBS) $(LDLIBS)

tests/test-storage: tests/test-storage.c src/storage.o src/storage-posix.o src/spacing.o
	$(CC) $(C_FLAGS) $(LDFLAGS) -o $@ tests/test-storage.c src/storage.o src/storage-posix.o src/spacing.o $(CORE_LIBS) $(LDLIBS)

TEST_LINK = $(CXX) $(CXX_FLAGS) $(LDFLAGS) -o $@ $@.cpp
tests/qt/test-model: tests/qt/test-model.cpp $(MODEL_OBJECTS) $(CORE_OBJECTS)
	$(TEST_LINK) $(MODEL_OBJECTS) $(CORE_OBJECTS) $(LIBS)

tests/qt/test-documentstore: tests/qt/test-documentstore.cpp $(STORE_OBJECTS) $(CORE_OBJECTS)
	$(TEST_LINK) $(STORE_OBJECTS) $(CORE_OBJECTS) $(LIBS)

tests/qt/test-bookmarks: tests/qt/test-bookmarks.cpp $(BOOKMARK_OBJECTS)
	$(TEST_LINK) $(BOOKMARK_OBJECTS) $(LIBS)

tests/qt/test-editor: tests/qt/test-editor.cpp $(EDITOR_OBJECTS) $(CORE_OBJECTS)
	$(TEST_LINK) $(EDITOR_OBJECTS) $(CORE_OBJECTS) $(LIBS)

tests/qt/test-vi: tests/qt/test-vi.cpp $(EDITOR_OBJECTS) $(CORE_OBJECTS)
	$(TEST_LINK) $(EDITOR_OBJECTS) $(CORE_OBJECTS) $(LIBS)

tests/qt/test-search: tests/qt/test-search.cpp $(SEARCH_OBJECTS) $(CORE_OBJECTS)
	$(TEST_LINK) $(SEARCH_OBJECTS) $(CORE_OBJECTS) $(LIBS)

tests/qt/test-navigation: tests/qt/test-navigation.cpp src/qt/navigation.o src/qt/panelsearch.o $(EDITOR_OBJECTS) $(CORE_OBJECTS)
	$(TEST_LINK) src/qt/navigation.o src/qt/panelsearch.o $(EDITOR_OBJECTS) $(CORE_OBJECTS) $(LIBS)

tests/qt/test-synonyms: tests/qt/test-synonyms.cpp $(SYNONYM_OBJECTS) src/qt/manuscripttext.o
	$(TEST_LINK) $(SYNONYM_OBJECTS) src/qt/manuscripttext.o $(LIBS)

tests/qt/test-spelling: tests/qt/test-spelling.cpp $(SPELL_OBJECTS) $(EDITOR_OBJECTS) $(CORE_OBJECTS)
	$(TEST_LINK) $(SPELL_OBJECTS) $(EDITOR_OBJECTS) $(CORE_OBJECTS) $(LIBS)

tests/qt/test-window: tests/qt/test-window.cpp $(APP_OBJECTS)
	$(TEST_LINK) $(APP_OBJECTS) $(LIBS)

tests/qt/test-resize: tests/qt/test-resize.cpp $(APP_OBJECTS)
	$(TEST_LINK) $(APP_OBJECTS) $(LIBS)

tests/qt/test-symbols: tests/qt/test-symbols.cpp
tests/qt/test-synonymwriting: tests/qt/test-synonymwriting.cpp
tests/qt/test-appearance: tests/qt/test-appearance.cpp
tests/qt/test-layout: tests/qt/test-layout.cpp
tests/qt/test-symbols tests/qt/test-synonymwriting tests/qt/test-appearance tests/qt/test-layout: $(APP_OBJECTS)
	$(TEST_LINK) $(APP_OBJECTS) $(LIBS)

check-core: tests/test-crumb tests/test-storage
	./tests/test-crumb
	./tests/test-storage

check-gui: $(QT_TESTS)
	@result=0; for test_program in $(QT_TESTS); do \
		echo "$$test_program"; "./$$test_program" || result=1; \
	done; exit $$result

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
	install -m 644 CrumbWriter.txt LICENSE "$(DESTDIR)$(PREFIX)/share/doc/crumbwriter/"

clean:
	rm -f $(APP_OBJECTS) src/qt/main.o data/qt-icons.cpp crumbwriter tests/test-crumb tests/test-storage $(QT_TESTS)

.PHONY: all check check-core check-gui install clean
