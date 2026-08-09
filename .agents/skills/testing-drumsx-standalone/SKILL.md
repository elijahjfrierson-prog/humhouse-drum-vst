---
name: testing-drumsx-standalone
description: How to run and GUI-test the HumHouse Drums X JUCE standalone on a headless Linux VM - launching, working around the intermittent degenerate window, measuring CPU correctly, and validating MIDI export.
---

# Testing the HumHouse Drums X standalone (JUCE)

## Build / binary

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure     # engine unit tests
```

Standalone binary (note the spaces in the name — always quote it):

```
build/AIDrumVST_artefacts/Release/Standalone/HumHouse Drums X
```

Editor base size is 1040x660; the outer window is therefore **1042x688** at 100% UI scale.

## Launching

Launch detached so the shell doesn't block, and always capture stderr:

```bash
BIN="<repo>/build/AIDrumVST_artefacts/Release/Standalone/HumHouse Drums X"
(cd /tmp && setsid "$BIN" > /tmp/hhx.out 2> /tmp/hhx_err.log < /dev/null &)
sleep 8
```

Do **not** use `xdotool windowactivate` in a helper script — it can block and hang the shell.

Find the process by exe link rather than by name (the name has spaces):

```bash
for p in $(ls /proc | grep -E '^[0-9]+$'); do
  [ "$(readlink /proc/$p/exe 2>/dev/null)" = "$BIN" ] && echo "pid $p"
done
```

## Gotcha: intermittent degenerate 2x28 window on launch

Roughly **1 in 5 cold launches** the process starts fine (alive, no stderr) but its X window is
`2x28` — nothing is visible on screen except a taskbar entry. It never self-heals (still 2x28 after
26s). This looks exactly like "the app crashes on open" to a user but it is a **window-sizing race,
not a crash**.

Always check geometry after launching, and force-resize to recover:

```bash
w=$(xdotool search --name "^HumHouse Drums X$" | head -1)
xdotool getwindowgeometry $w        # expect 1042x688
xdotool windowsize $w 1040 690 && xdotool windowmove $w 30 30
```

After the forced resize the full UI renders and is completely functional, which is the quickest way
to prove the editor itself is healthy. If you need a good window for a recorded run, loop
launch→check-geometry→kill until you get 1042x688 before you start recording.

## Gotcha: measuring CPU of a process whose name contains spaces

`/proc/<pid>/stat` puts `comm` in parentheses, and `HumHouse Drums X` contains spaces, so naive
`awk '{print $14+$15}'` reads the **wrong fields** and reports a bogus 0.0%. Strip everything
through the final `)` first:

```bash
read_cpu() { sed 's/.*) //' /proc/$pid/stat | awk '{print $12+$13}'; }   # utime+stime
```

Typical healthy numbers on this app: ~2-3% of one core in every state (idle, playing, 150% scale).
MAIN page costs more than DETAILS/KIT because `PhraseView::timerCallback` repaints unconditionally
at 30 Hz.

## No audio device

Headless VMs here have no `/dev/snd`, no PulseAudio/JACK, and the build does not define
`JUCE_JACK`, so Options -> Audio/MIDI Settings shows only `<< none >>`. The app still opens and the
whole UI works. Consequence: **`playheadBeats` only advances inside `processBlock`, so playhead
motion is not testable in this environment** — PLAY/STOP button state is testable, playhead travel
is not. Don't report playhead animation as passing here. `snd-dummy` is not available
(`modprobe: FATAL: Module snd-dummy not found`).

The only benign stderr line to expect is:

```
ALSA lib seq_hw.c:466:(snd_seq_hw_open) open /dev/snd/seq failed: No such file or directory
```

Anything matching `assert|leaked|segmentation|abort` is a real failure.

## Gotcha: stale knob readouts

`LabelledKnob::paint` draws the numeric % text on the *parent* component, which the slider
attachment never repaints. After changing a parameter programmatically (character select) or by
dragging the XY pad, the **needle moves but the % text keeps the old value**. To read the true
value, force a repaint by switching tabs away and back. Budget for this when asserting values —
a "wrong" number on screen may just be stale.

## Gotcha: em dash renders as mojibake

Strings containing U+2014 (character names like `Ethan — Pop Rock`, `KIT — SOCALROCK`, the empty-lane
`—` marker, and `…` in menu items) render as a tofu box + `â€"` mojibake, even though the source
bytes are valid UTF-8. Verify source bytes before blaming the data:

```bash
python3 -c "print(open('SourceX/DrumsXProcessor.cpp','rb').read().count(b'\xe2\x80\x94'))"
```

## Validating MIDI export

Export writes to a **subfolder named after the plugin** for per-instrument export, i.e. choosing
`/tmp/hhx_perlane` produces `/tmp/hhx_perlane/HumHouse Drums X/01 Kick.mid` etc. Only lanes with
hits get a file. In the JUCE file chooser you may need to click "Choose"/"Save" twice for folder
selection.

Validate byte-level rather than trusting file existence — parse `MThd`, track count, and count
note-on events (status `0x9n` with velocity > 0, honouring running status). A correct 16-bar
export is format 1, 960 PPQ, and the sum of per-lane note-ons should equal the combined export's.

## Useful coordinate math for screenshots

Screen is 1600x1200 but screenshots come back 1024x768 (factor 0.64). With the window placed at
(30,30) the editor origin lands at screenshot (20, 37), so
`editor (ex,ey) -> screenshot (20 + 0.64*ex, 37 + 0.64*ey)`. Use the `zoom` action on small regions
to read 11-12px UI text reliably.

## Devin Secrets Needed

None — this is a purely local standalone GUI test.
