# TATR Desktop

A small C desktop browser for [TATR](https://github.com/tsoding/tatr) task
directories. It reads each task folder's `TASK.md` and the optional `tasks/tags`
documentation file. Tasks can be created or edited from the desktop app.

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

Run the sorting, parser, and task persistence regression tests with `make test`.

Fenster uses native platform APIs, so this application is built natively for
each platform rather than as one Cosmopolitan single-binary artifact.

## Run

Pass a project folder or its TATR `tasks/` directory to open it directly:

```sh
build/tatr-ui path/to/project
```

Without an argument, the app opens a welcome window with a **Choose project
folder** button. Use **Open project** in the task browser to switch projects.
The chosen folder can be either the project root or its `tasks/` directory.

If no system font is found automatically, set `TATR_FONT` to a TrueType font
file. The application tries `TATR_FONT` first.

## Controls

- Up/down arrows move the task selection; Page Up/Page Down move by a page.
- `F` cycles All, Open, and Closed filters.
- `P` sorts by priority; `I` sorts by task ID; `O` reverses the order.
- Click the filter/sort buttons or a task row to select it.
- Use **New task** or **Edit task** to create or update a task. The editor saves
  changes directly to `TASK.md`, retaining unrecognized task properties.
- In the editor, choose multiple tags from the project tag list. **Create tag**
  adds a tag to `tasks/tags`, creating that file when it does not exist.
- With the tag list open, Up/Down selects a tag and Enter/Space toggles it;
  **Create tag...** is the last menu item.
- In text fields, Left/Right moves by a character and Ctrl+Left/Right moves by
  a word; Backspace deletes one character and Ctrl+Backspace deletes the
  previous word. The description editor shows a blinking caret.
- In the editor, click a field or use Tab to change focus. Space toggles status;
  Enter advances fields or inserts a description newline, and Ctrl+Enter (or
  Command+Enter on macOS) saves. Escape cancels the edit.
- Escape exits the application.

Tasks are initially sorted by descending numeric priority; larger values rank
higher, and tasks without a priority stay at the end in either sort direction.
Priority colors are relative to the set numeric priorities: red is highest,
yellow is middle, green is lowest, and gray means unset. A priority of `1000`
is displayed and sorted as the numeric value `1000`. Use the ID sort button to
browse in timestamp order.

## Third-party licenses

Fenster and Lite are included under their MIT licenses in `src/vendor`.
tinyfiledialogs is included under its zlib license, and `stb_truetype.h` is
public domain.
