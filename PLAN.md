# satl-source — project plan and handoff

GTK 4 desktop editor for **Satellite**, the user's programming language.
Written so a session with no prior context can pick the work straight up.

Last updated: 2026-09-20.

---

## 1. The Satellite language — PRESERVE THIS SECTION

**Corrected 2026-09-20: this is NOT the only record.** It was written when
the user said the syntax existed nowhere else, and that was wrong — they had
forgotten. The canonical sources are:

- **<https://github.com/satellitemadness1/satellite>** — "The Satellite
  Programming Language For Ubuntu/Linux", C++, the actual implementation.
- **<https://satellite.foundation>** — live (HTTP 200), where the earlier
  note said there was nothing.

Everything below came directly from the user by dictation and **has never
been reconciled against either source**. Treat it as notes, not as the
specification: where it disagrees with the implementation, the
implementation wins. It is still worth keeping, because it records what the
user asked the editor to support and in their own words.

Do not invent or extend syntax. Check the repository above first, and ask
the user when it is still unclear.

### Verified against the canonical repo, 2026-09-20

Fetched from `satellitemadness1/satellite@main`. Everything in this
subsection is **real Satellite**, copied from programs that ship with the
implementation — unlike the dictated notes that follow it.

The organising rule, in the language author's own words (README.md):

> a dotted path rooted at `satellite` names something the language owns, and
> a bare identifier names something the user owns.

**That single sentence is the whole design of the highlighter.** Style the
dotted `satellite.…` paths; leave bare identifiers alone.

`examples/hello_world.satl`, verbatim:

```satellite
// satellite-004's first program

satellite.include(satellite)

satellite.capsule satellite.main(satellite.container.list<satellite.variable.string> arguments)
{
    satellite.console.display("Hello, World!")
    satellite.console.display("a // inside a string is not a comment") // this one is
    satellite.console.display(42)
    satellite.console.display(satellite.bool.true)

    satellite.return(satellite)
}
```

What that adds to the dictated notes below:

| fact | evidence |
|---|---|
| `satellite.bool.true` — booleans are a dotted path too | `hello_world.satl` |
| bare integer literals: `42` | `hello_world.satl` |
| integers are arbitrarily large: `99999999999999999999999` | `tests/big_number.satl` |
| **a `//` inside a string is not a comment** | `hello_world.satl`, deliberately |
| `"some" + "str"` — `+` concatenates | `tests/two_strings.satl` |
| `satellite.variable.string s = "not yet"` | `tests/not_understood.satl` |

The last row matters: the dictated note below shows
`satellite.variable.string my_number = 9` and warns not to infer type rules
from it. The real test file assigns a *string* to a string, so that example
was probably misremembered. **Do not design anything around it.**

The `//`-inside-a-string line is a direct instruction to whoever writes
`satellite.lang`: strings must be matched **before** comments, or the
language's own hello-world highlights wrongly. It is in their example on
purpose.

### The authoritative word registry

`old_versions/second_satellite/` holds a complete front-end — lexer, parser,
AST — from the previous interpreter. The current top level is *satellite 004
revision 02*, a rewrite in progress with no lexer in it yet, so **the old
version is the best enumeration of the language that exists**, with the
caveat that 004 may change it.

The registry is one X-macro file:

    old_versions/second_satellite/src/satellite_words/words.def

371 `SAT_NODE(parent, ident, text, kind)` rows, plus `SAT_BUILT` and
`SAT_ALIAS`. Its own header says `WORD_NUMBERS.md` is the authority and it
is the copy a compiler can check. Reconstruct the dotted paths with:

```bash
curl -s https://raw.githubusercontent.com/satellitemadness1/satellite/main/\
old_versions/second_satellite/src/satellite_words/words.def |
python3 -c "
import re,sys
src=sys.stdin.read()
rows=re.findall(r'SAT_NODE\(\s*(\w+)\s*,\s*(\w+)\s*,\s*\"((?:[^\"\\\\]|\\\\.)*)\"', src)
par={i:p for p,i,t in rows}; txt={i:t for p,i,t in rows}
def path(i):
    out=[]
    while i in par: out.append(txt[i]); i=par[i]
    return '.'.join(reversed(out))
print('\n'.join(sorted({path(i) for i in par})))
"
```

That yields **313 dotted names** under 25 top-level namespaces:

| namespace | names | namespace | names |
|---|---|---|---|
| `satellite.variable` | 99 | `satellite.directory` | 6 |
| `satellite.container` | 51 | `satellite.file` | 6 |
| `satellite.library` | 50 | `satellite.statement` | 5 |
| `satellite.system` | 34 | `satellite.time` | 5 |
| `satellite.random` | 17 | `satellite.window` | 4 |
| `satellite.console` | 10 | `satellite.bool` | 3 |
| `satellite.network` | 8 | `satellite.capsule` | 2 |
| `satellite.thread` | 2 | | |

and singletons: `analyze`, `constructor`, `help`, `include`, `main`,
`protected`, `public`, `return`, `returns`, `spacesuit`.

**Where the dictated notes are WRONG.** The registry contradicts them:

| dictated below | what the registry has |
|---|---|
| `satellite.statement.finally`, `.switch`, `.case` | **absent.** Only `if`, `else`, `for`, `while` |
| `satellite.variable.infinity` | **absent.** The types are binary, bool, capsule, date, duration, expression, file, float, hex, network, number, string, thread, time, variant, window |
| `my_thread.stop()` | **absent.** Only `.start()` and `.join()` on `satellite.variable.thread` |
| containers are `list` | also `map`, `arguments`, `result` |

Treat the dictated notes as a record of what the user *said*, and the
registry as what the language *has*. Where they differ, ask — the user may
be describing 004's intended direction rather than misremembering.

### The dictated notes

Source files use the extension **`.satl`**.

Nearly every token is prefixed `satellite.` — the exceptions are identifiers
the programmer names themselves (variables, capsule names, arguments).

### Program structure

```satellite
satellite.include(satellite)

satellite.capsule satellite.main(satellite.container.list<satellite.variable.string> arguments)
{
    satellite.console.display("hello, claude!")

    satellite.return(satellite)
}

// this is a comment
```

- `satellite.capsule` declares a function ("capsule").
- Braces on their own lines; no semicolons anywhere in any example given.
- `//` starts a line comment. No block-comment syntax has been described.

### Variables

```satellite
satellite.variable.string my_number = 9

my_number = my_number + 1
```

Note the user's own example assigns `9` to a `string` — take the syntax as
given, and do not infer type rules from it.

Known variable types:

| type | notes |
|---|---|
| `satellite.variable.string` | |
| `satellite.variable.thread` | see threading below |
| `satellite.variable.file` | `satellite.file.new("filename.ext", "text")` |
| `satellite.variable.infinity` | exists; semantics not yet described |
| `satellite.variable.window` | exists; semantics not yet described |

Containers: `satellite.container.list<T>` — generic, angle brackets.

### Threading

```satellite
satellite.variable.thread my_thread = satellite.thread.new(capsule_name(args))

my_thread.start()

my_thread.join()

my_thread.stop()
```

Methods are called on the identifier with a plain dot — `my_thread.start()`,
NOT `satellite.thread.start(my_thread)`. (In that example `stop()` after
`join()` does nothing; the user included it to show the API.)

### Statements

```satellite
satellite.statement.while(this_number < target_number)
{
    satellite.console.display("this function has run " + this_number)

    this_number = this_number + 1
}
```

Known statement keywords:

- `satellite.statement.if` — pairs with `satellite.statement.else`
- `satellite.statement.while`
- `satellite.statement.for`
- `satellite.statement.finally` — pairs with the loops
- `satellite.statement.switch` — contains `satellite.statement.case`

```satellite
satellite.statement.switch(some_object)
{
    satellite.statement.case(some_variance_of some_object)
    {
        satellite.console.display("special case!")
    }
}
```

### Other known namespaces

- `satellite.console.display(...)` — print
- `satellite.include(...)`
- `satellite.return(...)`
- `satellite.file.new(name, mode)`
- `satellite.thread.new(capsule(args))`
- String concatenation with `+`.

---

## 2. What exists now

A GTK 4 window, maximized on open, split by a `GtkPaned`:

- **left, 250 px** — directory pane: file listing with icons and per-file
  measurements, plus a path entry pinned along the bottom
- **right** — a **search bar** across the top, then a `GtkNotebook` of open
  documents: one `GtkSourceView` per tab
  (no syntax highlighting yet), **`NEW TAB`** and **`NEW WINDOW`** buttons at
  the end of the strip (the user asked for words, not a `+`), a close button
  on each tab

The tab strip is inside the paned's end child, so it spans the editing half
and stops where the directory pane begins — measured at runtime: pane 250 px
wide, notebook starts at x=251, the first tab at x=272.

**Double-clicking a file row opens it**, or raises the tab it is already
open in. Directory rows do not activate. Untitled documents are named
`Untitled1.txt`, `Untitled2.txt`, … reusing the lowest free number.

### Files

| file | role |
|---|---|
| `src/main.cpp` | starts the GTK thread, then blocks; the initial thread does nothing |
| `src/gtk_host.{hpp,cpp}` | owns the GTK thread; `post()` / `call()` marshal work onto it |
| `src/actor.{hpp,cpp}` | per-widget thread + job queue; base class for widgets |
| `src/directory_view.{hpp,cpp}` | the whole left pane (596 lines — the big one) |
| `src/source_editor_view.{hpp,cpp}` | one document: GtkSourceView via the C API, reads its file on its own thread |
| `src/editor_tabs.{hpp,cpp}` | the notebook: open documents, tab names, `NEW TAB`, close buttons |
| `src/search_bar.{hpp,cpp}` | the search bar: walks the tree on its own thread, results under the entry |
| `src/session.{hpp,cpp}` | what each window had open, remembered between runs |
| `src/main_window.{hpp,cpp}` | `Gtk::ApplicationWindow`, the paned split, wires the pane's double-click to the tabs, spawns new windows |
| `meson.build` | gtkmm-4.0 + gtksourceview-5, C++20, `warning_level=2` |
| `install.sh` | deps, icon theme, build, install to `~/.satl`, offer symlink |
| `data/` | **empty** — reserved for `satellite.lang` (see §6) |

### Build and run

```bash
meson setup build          # once
ninja -C build
./build/satl-source
```

`./install.sh` does deps + theme + build + install. Flags: `--dry-run`,
`--no-deps`, `--no-theme`, `--no-install`, `--symlink`, `--no-symlink`,
`--prefix DIR`.

---

## 3. Architecture — the rules that must not be broken

The threading model is the user's explicit design request. Honour it.

- `main()` starts a thread and blocks. **That** thread runs the GTK main
  loop. The process's initial thread does nothing else.
- **Every widget owns a private thread** (`Actor`) with a job queue, for
  work that would otherwise stall the interface.
- `DirectoryView` owns a **second** thread beyond its actor — the scanner —
  which measures files. This is the "sub thread of the directory display
  widget thread" the user asked for.
- **Every open tab carries a thread**, since every tab is a
  `SourceEditorView` and every widget is an `Actor`. That thread is what
  reads the file off the disk. `EditorTabs` has one of its own as well,
  where a double-clicked path is canonicalised and stat'ed before anything
  reaches the screen.

**GTK is not thread-safe.** Only the GTK thread may touch a widget pointer,
GObject, or any GTK/GDK/Pango/GtkSourceView call. Widget threads reach the
interface only through `GtkHost::post()`. This is not a style preference —
violating it corrupts state and aborts at random.

Thread ownership inside `DirectoryView`:

| state | owner |
|---|---|
| `m_entries`, `m_generation` | actor + scanner, under `m_entries_mutex` |
| `m_rows`, `m_shown_directory`, all widgets | **GTK thread only** |
| `m_scan_stopping`, `m_scan_prodded` | under `m_scan_mutex` |

`EditorTabs::m_tabs` and every widget in it are **GTK thread only**; its
actor thread touches nothing but the path string handed to `open_file()`.

### Two lifetime rules already paid for in blood

1. **`Actor::stop()` must be called at the top of a subclass destructor.**
   C++ destroys members *before* the base destructor runs, so waiting for
   `~Actor()` to join leaves the thread reading freed memory.
2. **Closures crossing to the GTK thread capture a `shared_ptr<atomic<bool>>`
   alive flag** (`m_alive`), checked before touching anything. A closure
   queued when the widget dies must become a no-op, not a use-after-free.

### Tunable constants (top of `src/directory_view.cpp`)

| constant | value | meaning |
|---|---|---|
| `kBetweenFiles` | `5'000'000` ns = **5 ms** | pause between measuring files |
| `kBetweenPasses` | 2 s | pause between full rescans |
| `kSampleBytes` | 8192 | bytes sampled to decide text vs binary |
| `kIconPixelSize` | 24 | row icon size |

(For reference, since it came up: 50,000 ns = 50 µs = 0.00005 s. The user
first said 50,000 ns, then revised to 5,000,000 ns.)

---

## 4. Requirements as specified, and how they were met

All of these were **verified by running the app**, not by reading the code.

- Directory pane is **250 px** on the left (`gtk_paned_set_position`).
- Text files show `(58 lines)`; a final line with no trailing newline still
  counts. Empty file → `(0 lines)`.
- Binary files show kilobytes, **rounded down**: 1023 B → `(0 kb)`,
  1024 B → `(1 kb)`, 5500 B → `(5 kb)`.
- Text vs binary: NUL byte or invalid UTF-8 in the first 8 KiB → binary.
- Directories sort **first**, shown as `dir_name  (dir)` with a folder icon.
- Path entry at the bottom of the pane, **outside** the scrolled window so
  no listing length can scroll it away (verified with 1888 rows).
  Enter navigates. Accepts absolute, relative, `~`, and `..`. A bad path
  leaves the listing alone and marks the entry with the `error` CSS class.
- File type icons via `g_content_type_guess()` → `g_content_type_get_icon()`
  → `gtk_icon_theme_lookup_by_gicon()`, which walks the fallback chain.
- **Double click opens a file.** `gtk_list_box_set_activate_on_single_click`
  is `FALSE`, so the first click only selects and `row-activated` waits for
  the second (or for Enter on the selected row). Each file row carries its
  path in `g_object_set_data_full`; directory rows carry none and are set
  non-activatable, so they fall straight through.
- **A file already open is raised, not opened twice** — matched on the
  canonical path.
- **Tabs are over the editing half only** (numbers above).
- **Search as you type.** A `GtkSearchEntry` across the top of the editing
  half searches the directory the pane is showing **and everything inside
  it**, matching file names case-insensitively. The walk runs on the search
  bar's own thread; every keystroke bumps a generation and a walk that finds
  itself out of date abandons its tree part-way. Results appear in a
  revealer under the entry — one click opens one in a tab, Enter takes the
  first, Escape clears. Capped at 200 matches / 200,000 entries visited,
  and it says so when it stops early. Hidden files and directories are
  skipped, which keeps it out of `.git` entirely. Verified: typing `tab` in
  the project root returned `src/editor_tabs.hpp`, `src/editor_tabs.cpp` and
  `build/satl-source.p/src_editor_tabs.cpp.o` — so it does descend.
- **Open tabs come back.** Each window writes what it has open to
  `~/.satl/windows/window-N.tabs` on every change — a tab opened, closed or
  switched to — so nothing is lost if a window is killed. Untitled documents
  are remembered as placeholders; their contents are not, because nothing
  saves to disk yet.
- **Windows come back last-closed-first.** A window is a process, so which
  window is which has to be settled between processes: each claims a
  numbered slot with an exclusive `flock` held for its lifetime. The kernel
  arbitrates, so two windows can never share a slot and a killed window
  still gives its slot back. A starting window takes the free slot whose
  state file carries the **most recent `closed` stamp** — close two windows
  and open two again and they return in the reverse of the order you closed
  them. The survey-and-claim runs under a directory-wide lock, so two
  windows starting at the same instant cannot both pick the same slot.
  Verified: with `hello.txt` in one window and `notes.md` in the other,
  closing hello last then opening one window returned hello; closing notes
  last returned notes.
- **A tab you close is forgotten.** The state is rewritten on every change,
  so a closed tab is simply gone from it. Verified.
- **`NEW WINDOW` is a second copy of the program**, not a second window in
  this process — the user's own suggestion, and the process boundary keeps
  one window's trouble away from the others. `g_spawn_async()` runs
  `/proc/self/exe` with its working directory set to **whatever the pane is
  showing**, so the new window opens looking at the same place. Verified:
  the parent navigated its pane to `…/diagdir/sub`, clicked, and the child
  came up with that as its cwd — not the parent's — drew its own window, and
  outlived the parent.
- **`Untitled1.txt`, `Untitled2.txt`** — the startup document is
  `Untitled1.txt` and holds the welcome program; `NEW TAB` adds the next
  free number. Closing the last tab leaves a fresh `Untitled1.txt` rather than an
  empty window.
- A file that is **not text** (invalid UTF-8 or an embedded NUL), is larger
  than **64 MiB**, or cannot be read opens read-only showing a one-line
  notice — `(bin.dat is not text)` — rather than feeding GtkTextBuffer bytes
  it cannot hold.

---

## 5. Hard-won gotchas — re-reading these will save hours

- **`GTK_SOURCE_IS_BUFFER`, not `GTK_IS_SOURCE_BUFFER`.** GtkSourceView
  namespaces its macros on `GTK_SOURCE`, against the usual GTK convention.
- **GtkSourceView 5 has no C++ binding.** `gtksourceviewmm` died in the GTK 3
  era. Cross from gtkmm into C with `.gobj()`; never `Glib::wrap()` back,
  as there is no C++ wrapper to hand out for a `GtkSourceView`.
- **`gtk_icon_theme_set_theme_name()` refuses to work on the display's
  theme** (`is_display_singleton` assertion) and fails *silently* in effect.
  `DirectoryView` therefore builds a **private** `GtkIconTheme` set to
  Papirus, leaving the desktop alone.
- **`condition_variable::wait_for` with a predicate ignores plain notifies.**
  A predicate testing only "stopping" wakes, sees false, and sleeps out the
  remaining duration. Any wake flag must be *in the predicate* and set
  under the mutex.
- **gtkmm installs a global C++ locale from the environment.** Under a
  grouping locale an `ostringstream` writes a timestamp as
  `1,789,949,570,794,034`, which `std::stoll` reads back as **1** — so every
  window looked equally recently closed and the ordering collapsed. Numbers
  in a file format are not prose: `imbue(std::locale::classic())` on any
  stream that writes one. (Caught by reading the state file during testing,
  not by the compiler or any test that passed.)
- **A tab exists before its file does.** The page is appended, made
  current and focused while the read is still in flight, so for that window
  the document is an empty, editable buffer that the load is about to
  replace inside an irreversible action — anything typed into it is gone
  with no undo. Hold the buffer read-only until the read lands.
- **Never join a widget's thread from the GTK thread.** Closing a tab
  destroys a `SourceEditorView`, whose destructor joins the thread reading
  its file — on the GTK thread, because that is where the close button was
  clicked. The interface stops until the read finishes. Any thread that can
  be joined from the interface needs a way to be told to give up: here an
  `std::atomic<bool> m_abandon` checked between 64 KiB chunks.
- **`std::filesystem::file_size()` is not the length to read.** It is a
  stat, and a stat can be stale or a lie: anything under `/proc` reports 0
  while having plenty to say, and an ordinary file can grow between the stat
  and the read. Use it to turn away something enormous, then read to EOF and
  believe only what you actually got.
- **`gtk_widget_grab_focus()` does nothing until the widget is in a
  window.** It returns FALSE and no error. A widget built before its
  container is in the window cannot focus itself; something above it has to
  do it afterwards.
- **`gtk_icon_theme_get_theme_name()` is transfer-full**, unlike most GTK
  getters. Free it.
- **`g_main_context_invoke()` does NOT always queue.** When nobody owns the
  main context it *acquires* it and runs the closure **inline, on the
  calling thread** — proved with a ten-line glib program: a worker posting
  while no loop was running had its closure executed on the worker itself.
  `GtkHost::post()` was built on it. The loop owns the context for as long
  as it runs, so this was never a startup hazard; it was a **shutdown** one —
  once the loop returns, a widget thread still finishing work would run GTK
  code against widgets being destroyed around it. `post()` now attaches an
  idle source with `g_source_attach()`, which only ever queues.
- **A `GApplication` with an id is a singleton on D-Bus.** This was the
  whole obstacle to `NEW WINDOW`: running the executable again does *not*
  give a second window. The second copy finds `foundation.satellite.SourceEditor`
  already owned on the session bus, hands its activation to the first copy
  and exits — **measured at 42 ms**, with nothing drawn. `Gtk::Application`
  is therefore created with `Gio::Application::Flags::NON_UNIQUE`
  (`gtk_host.cpp`). A side effect worth knowing: launching `satl-source`
  twice from a terminal now gives two windows, where before it gave one.
- **A `GtkListBox` inside a `GtkScrolledWindow` is wrapped in a
  `GtkViewport`.** It is not a `GtkScrollable`, so
  `gtk_scrolled_window_get_child()` hands back the viewport, not the list —
  walking the tree has to step through `GTK_IS_VIEWPORT`. (This cost a whole
  diagnostic run, which reported `-1` for everything it tried to read.)
- **`gtk_widget_compute_bounds()` is `warn_unused_result`.** It returns
  FALSE when the widgets share no common ancestor.
- **`GtkTextBuffer` holds UTF-8 and nothing else.** Validate with
  `g_utf8_validate()` before `gtk_text_buffer_set_text()`; given an explicit
  length it rejects embedded NULs too, which doubles as the binary test.
- **Loading a document is not an edit**: wrap it in
  `gtk_text_buffer_begin_irreversible_action()` /
  `end_irreversible_action()`, or the first ctrl-Z winds the file back to
  whatever the tab held before.
- `Entry` is private to `DirectoryView`, so a file-scope helper cannot name
  it; use a lambda or a member.
- Source files contain a **literal `…` character**, not `…`. Text
  patching must match the character.
- `g_print` output through two pipes can vanish on SIGTERM. Redirect to a
  file when capturing diagnostics, rather than piping.

### Verification technique that worked well

Temporarily patch an env-gated block into the constructor, build, run with
`SATELLITE_DIAG=1 timeout N`, capture **to a file**, then restore the source
from a backup and rebuild.

**Running the app puts a maximized window on the user's actual screen.**
There is no Xvfb, no nested compositor and no headless option on this
machine, so every test launch interrupts whatever they are looking at. Say
so before a batch of runs, keep them short, and never leave one running.

Two traps learned the hard way with this technique:

* **A spawned child inherits `SATELLITE_DIAG`**, so a diagnostic that clicks
  `NEW WINDOW` is a fork bomb. Gate the click on a marker file the child will
  find already written. (The child inheriting the parent's stdout is a gift,
  though: its own diagnostic lands in the same log. Print `getpid()`.)
* **Restore by removing the patch, not by copying the backup back**, once any
  real change has landed in the same file since the backup was taken. This caught several bugs that compiled fine and
looked correct. GNOME blocks D-Bus screenshots from unauthorized callers, so
visual confirmation is not available; assert on widget state instead
(`gtk_widget_get_mapped`, `get_width`, label text, resolved icon paths).

---

## 6. Next up

### 0. CLOSED as not reproducible: a startup freeze, reported once

**Status: the user could not reproduce it afterwards and considers it gone.
It was never diagnosed.** Kept here because "cannot reproduce" is not the
same as "understood", and if it comes back this is the head start.

On 2026-09-20 the user reported that the app "freezes now", answering: it
happens **right at startup**, without touching anything, and it appears as
GNOME's **"app is not responding — force quit / wait?"**. That dialog means
the GTK thread is not answering the compositor's ping. By the end of the
same session they reported it no longer happening.

Three explanations fit, and they were never told apart:

1. one of the hangs fixed that session covered it — the GTK-thread join on
   tab close, the FIFO that stopped the scanner for good, or `post()`
   running closures inline at shutdown (all in the review table below);
2. it was transient;
3. **they were watching one of the assistant's test windows.** Roughly
   thirty maximized instances were launched on their screen that session,
   several killed mid-startup by `timeout`, every one identical to their
   own. This costs nothing to believe and explains the symptom exactly.

What was ruled out, by measurement, on this machine:

- 10 consecutive launches: every one responsive, answering an external
  accessibility query in 0.04 s, GTK thread idle in `poll()`;
- every GSK renderer (default, `gl`, `cairo`, `vulkan`) — the GPU here has
  incomplete Vulkan support, so this was a real suspect;
- row building, the only long job on the GTK thread at startup: 13,070
  entries in 1.0 s, 1,888 in 0.16 s — not slow enough to trip a 5 s ping;
- widget work landing on a worker thread: `rebuild_rows` ran on the GTK
  thread in 8/8 launches, because `g_application_run()` acquires the main
  context before `activate`;
- every new signal handler driven synthetically (NEW WINDOW, NEW TAB,
  double-click open, close tab, navigate) while a 500 ms heartbeat kept
  ticking in both parent and child.

Several genuine hang mechanisms WERE found and fixed in the same session
(see the review table below) — the GTK-thread join on tab close, the FIFO
that stopped the scanner for good, `post()` running closures inline at
shutdown. **None of them fires at startup with nothing touched**, so none of
them can be claimed as the user's bug.

**If it returns, `./debug-freeze.sh` is in the project root.**
It launches the editor, probes it from outside every 3 seconds, and on two
consecutive misses writes `freeze-report.txt` with every thread's `wchan`
and a full `gdb` backtrace of all threads. Its capture path was tested by
freezing the app deliberately with SIGSTOP: it caught it and dumped all 19
threads. Ask the user to run the editor through it and read that file
first. Keep the script until the freeze is actually understood, or until
the user says to drop it — it cost nothing and it is already written.

1. **`data/satellite.lang`** — a GtkSourceView 5 language definition, the
   original goal. **Read <https://github.com/satellitemadness1/satellite>
   first**: the real grammar is there, and §1 below is only dictated notes. Needs: the `satellite.` prefix, the namespaces in §1,
   `//` comments, strings, numbers, user identifiers left unstyled. Register
   the directory with `gtk_source_language_manager_set_search_path()` and set
   it on the buffer. Glob: `*.satl`.
2. **`.satl` MIME type** — on machines where Satellite is installed, the
   system already provides a MIME type *and* an icon, and the editor picks
   both up automatically (Papirus inherits `hicolor`, where installed
   programs register icons). Where Satellite is absent, `.satl` falls back
   to `application-octet-stream`. Consider shipping a MIME definition so
   `.satl` reads as text; that also lets GtkSourceView auto-detect.
3. **Saving.** Opening works; nothing writes back yet. Note this now also
   limits the session store: an untitled document comes back as an empty
   untitled document, because there is nowhere to put its contents. A tab knows its path
   (`Tab::path`, empty while untitled) and its title, so save needs a key
   binding, a dirty flag on the tab label, and save-as for untitled
   documents. Nothing warns about unsaved changes on close.
4. A **concurrency review** was launched in an earlier session (workflow
   `wf_566807ca-2a9`) and never reported back. It was re-run as part of the
   tabs change (`wf_9e55a774-e43`) — five lenses, every finding checked by
   two independent verifiers. What it found and what was done:

   | finding | verdict | fixed |
   |---|---|---|
   | `close_tab()` joins the view's reading thread **on the GTK thread**, freezing the interface for the length of the read | held, 2/2 | yes — the read is chunked and abandonable; `~SourceEditorView` sets `m_abandon` before `stop()` |
   | `read_text_file()` trusted `file_size()`, so a file reporting size 0 opened blank | held, 2/2 | yes — reads to EOF; `file_size()` is only the cheap too-big pre-check. Verified on `/proc/self/status`: **1514 chars**, previously blank |
   | `view->focus()` at startup does nothing — `grab_focus()` fails on an unrooted widget | held, 2/2 | yes — `EditorTabs::focus_current()`, called by the shell once the notebook is in the window |
   | the scanner opens a **FIFO** and blocks there forever; `~DirectoryView`'s join then never returns | held | yes — non-regular entries are marked `is_special`, shown `(special)`, never opened, never activatable |
   | `gtk_icon_theme_get_theme_name()` is transfer-full and was leaked into `g_warning` | held / out-of-scope split | yes — freed |
   | closing the last tab steals focus from whatever you were typing in | held | yes — the replacement tab does not take focus |
   | **a loading tab is on screen, focused and editable**: type into it and `apply_text()` destroys what you typed, with the undo history cleared by `begin_irreversible_action()` so ctrl-Z cannot get it back | held, 2/2 | yes — the buffer is held read-only for the duration of the load. Sampled at 20 ms on a 60 MB file: `t=0 editable=0 chars=0`, `t=40ms editable=1 chars=61000000` |
   | a file deleted from disk could no longer raise the tab still holding it | contested, minor | yes — a failed resolve now still tries to raise an open tab |

   One curiosity worth knowing for next time: the FIFO finding came back
   **refuted**, and both verifiers quoted *the fix* as their evidence — they
   read the tree after it had been applied mid-run. A verifier reads the
   working tree, not the tree the reviewer saw. Do not take a refutation at
   face value if the code moved underneath it.

### Open questions for the user

- `(1 lines)` is shown for single-line files. Kept invariant to match
  `(5 kb)`, which never becomes `kbs`. Special-case to `(1 line)`?
- **Should double-clicking a directory row navigate into it?** Still
  unanswered, and now more visible: files respond to a double click and
  directories sit there. The row already carries everything needed.
- ~~Project `license` in `meson.build` is set to MIT, chosen arbitrarily.~~
  **Settled 2026-09-20**: the Satellite repo ships `LICENSE` reading "MIT
  License (Expat), Copyright (c) 2026 Terran Satellite", so MIT matches
  what the user already chose for the language itself. Worth confirming
  they want the same copyright line here.
- Closing the last tab opens a fresh `Untitled1.txt`, on the grounds that an
  empty notebook is a hole in the window. Leave it empty instead?
- `Untitled` numbering reuses the lowest free number, so closing
  `Untitled1.txt` and pressing `NEW TAB` gives `Untitled1.txt` again rather
  than `Untitled3.txt`. Some editors count up forever instead.
- Search results take **one** click, while directory rows take two. The
  reasoning: a result list is a transient answer to something just typed,
  not a place to browse. Should they match instead?
- The search matches **files only**, not directory names. Nothing can be
  done with a directory yet, which is why — but that is the same open
  question as the double-click one above.
- The session remembers tabs but **not** which directory the pane was
  showing, so a restored window lists the directory it was launched from
  while holding tabs from elsewhere. Paths are absolute so nothing breaks.
  Remember the directory too?
- Session state lives in `~/.satl/windows/`, beside the installed program,
  rather than under `~/.local/share`. Chosen to keep everything this
  project owns in one place.

---

## 7. Decisions the user has already made — do not relitigate

- **Thread-per-widget**, GTK loop on a spawned thread. Their explicit design.
- **5 ms** between file measurements (revised up from 50 µs).
- Executable is **`satl-source`** (with the dash).
- **`papirus-icon-theme` is a project dependency.** Adwaita ships only
  generic mimetype icons — no C++, Python or shell icons — so per-language
  icons require it.
- Install goes to **`~/.satl/satl-source`** (no root), then *offers* a
  `/usr/bin` symlink, described as recommended. `--prefix DIR` installs
  directly into `DIR`, **without** appending `bin/`. This is why the script
  does not use `ninja install`: meson always appends bindir to its prefix.
- The install must **overwrite** the existing executable every time.

---

## 8. Environment

- AlmaLinux 10.2, GNOME on Wayland, g++ 14.3, meson 1.4.1, ninja 1.11.1.
- GTK 4.16.7, gtkmm 4.13.2, GtkSourceView 5.14.2.
- Dev packages live in **CRB**; `papirus-icon-theme` lives in **EPEL**.
  `install.sh` enables both on RHEL rebuilds.
- **`sudo` requires a password**, and the assistant's shell has no tty — any
  step needing root must be handed to the user as a command to run.
- Desktop icon theme has been set to `Papirus`.
- `/bin` is a symlink to `/usr/bin`.
- Not a git repository. Nothing is committed.
