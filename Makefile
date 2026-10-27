CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -pedantic
CPPFLAGS += -Isrc -Isrc/vendor/lite
SOURCES = src/main.c src/tatr.c src/ui.c src/renderer.c src/vendor/lite/rencache.c
TARGET = build/tatr-ui
TEST_TARGET = build/test-tatr-sort

ifeq ($(OS),Windows_NT)
TARGET := build/tatr-ui.exe
TEST_TARGET := build/test-tatr-sort.exe
PLATFORM_LIBS = -lgdi32 -luser32 -lm
else ifeq ($(shell uname -s),Darwin)
PLATFORM_LIBS = -framework Cocoa -framework CoreGraphics -lm
else
PLATFORM_LIBS = -lX11 -lm
endif

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SOURCES) src/tatr.h src/ui.h src/renderer.h src/vendor/fenster.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) -o $@ $(PLATFORM_LIBS)

$(TEST_TARGET): tests/test_tatr_sort.c src/tatr.c src/tatr.h \
		tests/fixtures/priority/20261027-000001/TASK.md \
		tests/fixtures/priority/20261027-000002/TASK.md
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_tatr_sort.c src/tatr.c -o $@

test: $(TEST_TARGET)
	$(TEST_TARGET)

clean:
	rm -rf build
