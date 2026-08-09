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

## Gotcha: degenerate 2x28 window on launch (fixed at 07acceb, may regress)

Historically, roughly **1 in 5 cold launches** the process started fine (alive, no stderr) but its X
window was `2x28` — nothing visible except a taskbar entry, and it never self-healed. This looks
exactly like "the app crashes on open" to a user but it is a **window-sizing race, not a crash**.
The root cause was `DrumsXEditor::applyScale()` calling `setSize(scaled)` before `setTransform`.
As of commit 07acceb the transform is applied first, a real bounds change is forced
(`setSize(base, base-1)` then `setSize(base, base)`), and `ensureWindowSize()` runs on the first 16
ticks of the 8 Hz editor timer (~2 s) to re-apply the scale if the top-level window is smaller than
half of `1040*scale x 660*scale`. Verified 50/50 clean cold launches after the fix (0 collapses,
0 self-heals needed).

If you see it again, treat a **transient** collapse that repairs itself within ~2 s as acceptable and
a permanently invisible window as a failure. Always check geometry after launching; force-resize to
recover so you can prove the editor itself is healthy:

```bash
w=$(xdotool search --name "^HumHouse Drums X$" | head -1)
xdotool getwindowgeometry $w        # expect 1042x688
xdotool windowsize $w 1040 690 && xdotool windowmove $w 30 30
```

After a forced resize the full UI renders and is completely functional. If you need a good window
for a recorded run, loop launch→check-geometry→kill until you get 1042x688 before you start
recording.

To quantify a launch-collapse rate, sample geometry at ~1.5 s (before the self-heal window closes),
~4.5 s and ~6.5 s per launch, then classify each run as clean / self-healed / permanently collapsed.
`/tmp/hhx_launch30.sh N` in past sessions did exactly this.

## Gotcha: measuring CPU of a process whose name contains spaces

`/proc/<pid>/stat` puts `comm` in parentheses, and `HumHouse Drums X` contains spaces, so naive
`awk '{print $14+$15}'` reads the **wrong fields** and reports a bogus 0.0%. Strip everything
through the final `)` first:

```bash
read_cpu() { sed 's/.*) //' /proc/$pid/stat | awk '{print $12+$13}'; }   # utime+stime
```

Also beware `pgrep -f "HumHouse Drums X"` matching your own helper shell — resolve the pid via the
`/proc/*/exe` symlink instead, or you will silently measure the wrong process (2 MB RSS, 0.0% CPU).

Typical healthy numbers: before the `PhraseView::timerCallback` repaint guard, MAIN was ~2.4-2.8% of
one core in every state and ~5x DETAILS. After the guard (07acceb: repaint only when the timeline
hash changes or the transport is playing), idle MAIN is ~0.7% at both 100% and 150% scale and
playing MAIN ~1.7%. RSS sits ~40 MB at 100% and grows to ~54 MB at 150% without shrinking back —
bounded, not a leak.

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

## Gotcha: stale knob readouts (fixed at 07acceb, may regress)

`LabelledKnob::paint` draws the numeric % text on the *parent* component, which a slider attachment
does not repaint by itself. Before the fix, changing a parameter programmatically (character select)
or by dragging the XY pad moved the needle but left the % text at the old value until a tab switch
forced a repaint. The fix is `slider.onValueChange = [this]{ repaint(); }`.

Test this correctly by **never switching tabs**: click through characters and read the % after each
(known values: Jesse 55/78, Max 72/90, Ethan 35/45), and screenshot mid-XY-drag with the mouse
button still held. If any custom control paints text belonging to a child slider, check for the same
class of bug.

## Gotcha: non-ASCII glyphs render as mojibake

The UI typeface here has no glyph for U+2014/U+2026, so em dashes and ellipses render as a tofu box
plus `â€"` even though the source bytes are valid UTF-8. As of 07acceb every user-visible string was
changed to ASCII (`Ethan - Pop Rock`, `KIT - SOCALROCK`, empty lane `-`, `Export 16 bars...`,
`LOAD KIT FOLDER...`). **Keep user-visible strings ASCII.** Remaining U+2014 bytes in `SourceX/` are
in comments only; check whether a hit is user-visible before reporting it:

```bash
grep -rn $'\u2014' SourceX/          # then confirm each hit is a comment, not a string literal
```

The places to eyeball for mojibake: MAIN character list, KIT panel header, KIT empty-lane marker,
the EXPORT MIDI popup menu, and the LOAD KIT FOLDER button.

## Validating MIDI export

Export writes to a **subfolder named after the plugin** for per-instrument export, i.e. choosing
`/tmp/hhx_perlane` produces `/tmp/hhx_perlane/HumHouse Drums X/01 Kick.mid` etc. Only lanes with
hits get a file. In the JUCE file chooser you may need to click "Choose"/"Save" twice for folder
selection.

Validate byte-level rather than trusting file existence — parse `MThd`, track count, and count
note-on events (status `0x9n` with velocity > 0, honouring running status). A correct 16-bar
export is format 1, 960 PPQ, and the sum of per-lane note-ons should equal the combined export's.

## DETAILS metre row uses hard-coded coordinates

The METRE & TEMPO panel is `(16, 230, W-32, 110)`; `drawPanel` puts its title at x28..~120,
y238..252. Captions (TIME SIG / TEMPO / BPM / SWING GRID) are hard-coded at y=240 and x=150/330/478/736,
controls at y=258, toggles and CLEAR at y=296 — all inside the panel, verified clean at 75/100/150%.
Any future edit to these literals needs re-checking at all three scales, since nothing lays them out
relatively.

## Useful coordinate math for screenshots

Screen is 1600x1200 but screenshots come back 1024x768 (factor 0.64). With the window placed at
(30,30) the editor origin lands at screenshot (20, 37), so
`editor (ex,ey) -> screenshot (20 + 0.64*ex, 37 + 0.64*ey)`. Use the `zoom` action on small regions
to read 11-12px UI text reliably. At other scales recompute the factor: at 75% the outer window is
782x523 and at 150% it is 1562x1018 (content exactly 1040*s x 660*s), so the editor-to-screenshot
factor becomes 0.64*s.

Careful with checkbox hit targets on DETAILS: `Ride instead of hats` (x=28), `Half time` (x=232) and
`Manual pattern` (x=392) are close together at y=296 — clicking the wrong one silently changes
unrelated state. Zoom the toggle row after clicking to confirm which one ticked.

## Devin Secrets Needed

None — this is a purely local standalone GUI test.
