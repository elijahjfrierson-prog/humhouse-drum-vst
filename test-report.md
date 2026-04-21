# v1.6.0 Test Report — PR #2

Built and ran the JUCE Standalone locally from `build/AIDrumVST_artefacts/Release/Standalone/HumHouse Drums` and exercised the four v1.6.0 primary flows through the UI. No audible playback (the VM has no ALSA sink), so pattern density was evaluated via the on-screen arrangement visualization.

Devin session: https://app.devin.ai/sessions/ede2a6dfb48e4230b03a14f49711c80f

## Results

- **Test 1 — STARTER dropdown populates and appends a region**: **passed**
- **Test 2 — COPY / PASTE duplicates the region**: **passed**
- **Test 3 — Manual Mode shows exactly 5 rows and ADD TO ARRANGEMENT commits**: **passed**
- **Test 4 — XY-pad COMPLEXITY/VELOCITY visibly changes pattern density/intensity**: **passed**

## Evidence

### Test 1 — STARTER dropdown

| Dropdown opened — many grooves listed | After selecting "60s Shuffle Drumset 01" — `1 REGION` |
|---|---|
| ![STARTER dropdown populated](https://app.devin.ai/attachments/a3580824-3f38-45a3-b094-9034ea8ddcf8/screenshot_fdbfc6dad94f4f3f8ecb7b1dfcb8ce43.png) | ![1 region appended](https://app.devin.ai/attachments/1172a314-800f-44d5-a948-03566c2a2590/screenshot_f2eeebdd44934d9daf33c3170d79747f.png) |
| Bar Band Basic, Bebop, Brush Train etc. all visible | Combobox snapped back to placeholder; notes visible |

### Test 2 — COPY / PASTE

| After COPY + two PASTEs — `3 REGIONS` |
|---|
| ![3 regions after two pastes](https://app.devin.ai/attachments/fabfd5c2-a533-4206-adc9-6f0e4e284731/screenshot_5bca11d2935f4338a57a862a181472e0.png) |
| Region count: 1 → 2 → 3; pasted regions match the original pattern shape |

### Test 3 — Manual Mode (5 rows × 16 bars, ADD TO ARRANGEMENT)

| 🟢 Manual grid — exactly 5 rows (CRASH / HI-HAT / TOM / SNARE / KICK) | 🟢 After ADD TO ARRANGEMENT — `4 REGIONS` |
|---|---|
| ![5-row grid](https://app.devin.ai/attachments/3c2115af-816b-40ed-ad44-c008771cb007/screenshot_9e374c46073e4ba9bf0695e5a3c47d84.png) | ![manual region committed](https://app.devin.ai/attachments/1b56ac6d-0d74-48c1-94fd-d964407abd07/screenshot_d1cfe7ed05ce45b78983aafb4a02c1ad.png) |
| 16 bar markers visible along the top | 3 pasted + 1 manual commit = 4 regions |

### Test 4 — XY-pad COMPLEXITY / VELOCITY

| 🔴 Bottom-left pad (low COMPLEXITY + low VELOCITY) | 🟢 Top-right pad (high COMPLEXITY + high VELOCITY) |
|---|---|
| ![low complexity low velocity](https://app.devin.ai/attachments/d48ee668-e97a-4876-bb03-27f76329cf4e/screenshot_3794b04a20d04274821e9495a5cac8d5.png) | ![high complexity high velocity](https://app.devin.ai/attachments/6090826c-29d9-4c8c-9477-0c7a612a1e54/screenshot_ea5d19aa248543a0bb6be84e5e454b48.png) |
| Top row shows ~5 sparse hat blocks, dimmer | Top row shows ~14 dense hat blocks, brighter accent |

## Not verified

- **Live audible playback** — the VM has no ALSA audio sink. All pattern assertions used the on-screen arrangement strip visualization as a proxy for note density and velocity brightness. Audible differences between the 5 bundled kits (PopRock vs NuRock vs AltRock vs IndieLofi vs Thrash) and the cymbal −6 dB / tom −1.5 dB mix trims were **not** ear-tested.
- **Exact note-count / velocity ratio via MIDI export** — I stopped short of driving the SAVE MIDI file dialog and comparing `mido` dumps; the visible density contrast in the arrangement strip was decisive enough for this pass.
- **Per-kit groove profile differences** — requires audible playback or MIDI dump.
- **X-Y pad drive-vs-laid-back Y axis** — deferred in this PR; Y axis is still wired to VELOCITY, not micro-timing.

## Recording

Full UI walkthrough with annotations attached. Watch this if you want the shortest path to verifying the change visually.
