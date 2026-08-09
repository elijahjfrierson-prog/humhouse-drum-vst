# HumHouse Drums X kit format

A kit is a folder of WAVs plus an optional `kit.json`. Load one with
**KIT / MIX → LOAD KIT FOLDER…**.

## Articulations

Samples are addressed by piece name, which maps onto the 30 canonical
articulations:

`kick`, `snare`, `snare_rim`, `sidestick`, `snare_ghost`, `snare_flam`,
`snare_roll`, `hat_closed`, `hat_tight`, `hat_open_1` … `hat_open_4`,
`hat_pedal`, `hat_splash`, `hat_bell`, `ride`, `ride_bell`, `ride_edge`,
`ride_crash`, `left_crash`, `right_crash`, `crash_3`, `china`, `splash`,
`tom_1` … `tom_4`, `perc`.

An articulation with no samples falls back to its nearest relative (a ghost note
plays on the snare, `hat_open_4` on the widest hat that exists), so a small kit
still plays a full 30-lane performance.

## Layers, round robins and mics

Each articulation holds **velocity layers**, softest first, and each layer holds
**round-robin variants**; each variant holds one recording per **mic**
(`close`, `oh`, `room`). Velocity picks the layer, the performance engine's
round-robin slot picks the variant, so consecutive strokes are never
bit-identical. `close` is the only mic a kit must supply — the mixer's
**Mic Blend** and **Bleed** controls fold the others in behind it, and generate
the leak from a delayed mono copy when a kit is close-mic only.

The target depth is 8 layers × 4 round robins per articulation. Fewer works:
missing layers are spread across the velocity range, and with a single variant
the engine nudges to a neighbouring layer instead.

## kit.json

```json
{
  "name": "SoCal Rock",
  "version": "1",
  "pieces": [
    { "piece": "snare", "layer": 1, "variant": 1, "mic": "close", "file": "snare_v1_rr1.wav" },
    { "piece": "snare", "layer": 1, "variant": 1, "mic": "oh",    "file": "snare_v1_rr1_oh.wav" },
    { "piece": "snare", "layer": 1, "variant": 2, "mic": "close", "file": "snare_v1_rr2.wav" }
  ]
}
```

`layer` and `variant` are 1-based. `layer` may be omitted, in which case each
file becomes its own layer and the set is ordered by measured loudness.

## Filename convention

Without a `kit.json`, the placement is read from the filename:

```
<piece>[_v<layer>][_rr<variant>][_<mic>].wav
```

e.g. `snare_v3_rr2_oh.wav` = snare, third velocity layer, second round robin,
overhead mic. `snare_1.wav` still works: it becomes one more layer of the snare,
ordered by loudness.
