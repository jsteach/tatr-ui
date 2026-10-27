CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -pedantic
CPPFLAGS += -Isrc -Isrc/vendor/lite
SOURCES = src/main.c src/tatr.c src/ui.c src/renderer.c src/vendor/lite/rencache.c \
	src/vendor/tinyfiledialogs/tinyfiledialogs.c
TINYFD_OBJECT = build/tinyfiledialogs.o
TARGET = build/tatr-ui
TEST_TARGET = build/test-tatr-sort

ifeq ($(OS),Windows_NT)
TARGET := build/tatr-ui.exe
TEST_TARGET := build/test-tatr-sort.exe
PLATFORM_LIBS = -lgdi32 -luser32 -lcomdlg32 -lole32 -lm
else ifeq ($(shell uname -s),Darwin)
PLATFORM_LIBS = -framework Cocoa -framework CoreGraphics -lm
else
PLATFORM_LIBS = -lX11 -lm
endif

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(filter-out src/vendor/tinyfiledialogs/tinyfiledialogs.c,$(SOURCES)) \
		$(TINYFD_OBJECT) src/tatr.h src/ui.h src/renderer.h src/vendor/fenster.h \
		src/vendor/tinyfiledialogs/tinyfiledialogs.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		$(filter-out src/vendor/tinyfiledialogs/tinyfiledialogs.c,$(SOURCES)) \
		$(TINYFD_OBJECT) -o $@ $(PLATFORM_LIBS)

$(TINYFD_OBJECT): src/vendor/tinyfiledialogs/tinyfiledialogs.c \
		src/vendor/tinyfiledialogs/tinyfiledialogs.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wno-cast-function-type -c $< -o $@

$(TEST_TARGET): tests/test_tatr_sort.c src/tatr.c src/tatr.h \
		tests/fixtures/priority/20261027-000001/TASK.md \
		tests/fixtures/priority/20261027-000002/TASK.md \
		tests/fixtures/project/tasks/20261027-000003/TASK.md
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_tatr_sort.c src/tatr.c -o $@

test: $(TEST_TARGET)
	$(TEST_TARGET)

clean:
	rm -rf build
