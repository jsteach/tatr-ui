# TATR Desktop

A small C desktop browser for [TATR](https://github.com/tsoding/tatr) task
directories. It reads each task folder's `TASK.md` and the optional `tasks/tags`
documentation file. The viewer does not edit task files.

The window uses [Fenster](https://github.com/mundusnine/fenster) for its
cross-platform framebuffer and keyboard/mouse input, with Lite's software
renderer cache and a framebuffer-backed renderer. The panels resize with the
window and switch to a vertical layout when the window is narrow.

## Build

Requires a C compiler and the platform windowing development files.

```sh
make
```

On Linux, this requires X11 development headers and libraries. On macOS, use
the system Clang and Cocoa frameworks. On Windows, use MinGW-w64/GCC and run
`make` from a compatible shell.

Run the priority sorting regression test with `make test`.

Fenster uses native platform APIs, so this application is built natively for
each platform rather than as one Cosmopolitan single-binary artifact.

## Run

Pass a TATR `tasks/` directory, or run the application inside a project that
has a `tasks/` directory in the current or a parent folder:

```sh
build/tatr-ui path/to/project/tasks
```

If no system font is found automatically, set `TATR_FONT` to a TrueType font
file. The application tries `TATR_FONT` first.

## Controls

- Up/down arrows move the task selection; Page Up/Page Down move by a page.
- `F` cycles All, Open, and Closed filters.
- `P` sorts by priority; `I` sorts by task ID; `O` reverses the order.
- Click the filter/sort buttons or a task row to select it.
- Escape exits the application.

Tasks are initially sorted by descending numeric priority; larger values rank
higher, and tasks without a priority stay at the end in either sort direction.
Priority colors are relative to the set numeric priorities: red is highest,
yellow is middle, green is lowest, and gray means unset. A priority of `1000`
is displayed and sorted as the numeric value `1000`. Use the ID sort button to
browse in timestamp order.

## Third-party licenses

Fenster and Lite are included under their MIT licenses in `src/vendor`.
`stb_truetype.h` is public domain.
