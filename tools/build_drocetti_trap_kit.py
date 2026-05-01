#!/usr/bin/env python3
"""
v1.6.1-rc.19 — Build the "Drocetti Trap" kit from the user's original
sample pack (formerly "Drocetti"; renamed to Drocetti per user request
on 2025-04-20).

Reads symlinks under tools/trap_kit_raw/, classifies them by category
(kick / snare / clap / hat_closed / hat_open / perc / tom / crash /
ride / 808 / bass / pad / synth / phrase / fx / vox / pattern /
hat_groove / drum_groove), normalises the file names
(`Drocetti__<category>_<NN>.wav`), copies them into
Resources/DefaultKit/, and emits MANIFEST entries appended to
DefaultKit/MANIFEST.json under the new "Drocetti" kit.

Notes
=====
* User pattern WAVs (`Pattern+*+consolidated*.wav`,
  `drumgrooves*.wav`, `evenmoredrum*.wav`, `moredrumgrooves*.wav`,
  `hatgrooves*.wav`) are tagged as `pattern_<n>` / `drum_groove_<n>` /
  `hat_groove_<n>` and copied so a follow-up step can extract MIDI.
* The kit is intended to be picked from the new SAMPLE PICKER
  dropdown (rc.19) per arrangement lane, so a user can compose entire
  trap beats inside HumHouse Drums without leaving the arrangement.
* The default 70's Yamaha and Heavy Studio kits are NOT touched —
  they are added alongside in MANIFEST.json under "Drocetti".
"""
from __future__ import annotations

import json
import re
import shutil
from pathlib import Path

REPO   = Path(__file__).resolve().parents[1]
RAW    = REPO / "tools" / "trap_kit_raw"
KITDIR = REPO / "Resources" / "DefaultKit"
MAN    = KITDIR / "MANIFEST.json"

# v1.6.1-rc.19 — bundled-binary cap. The full Drocetti pack is ~325
# user-original WAVs / ~800 MB which would explode the JUCE
# BinaryData blob into a multi-GB static lib. The bundled subset
# below covers the SAMPLE PICKER's "Drocetti Trap" sub-folders;
# the full kit is delivered via the macOS .pkg installer to a
# shared application-support folder in a follow-up release.
BUNDLE_LIMIT = {
    "kick":        12,
    "snare":       12,
    "clap":         5,
    "hat_closed":  10,
    "hat_open":     8,
    "perc":         8,
    "tom":          4,
    "crash":        6,
    "ride":         6,
    "bass_808":     8,
    "bass":         6,
    "pad":          8,
    "synth":        8,
    "phrase":       8,
    "vox":          5,
    "fx":           6,
    # Pattern files / drum_groove WAVs are kept on disk for the
    # MIDI-extraction tool but NOT bundled into the binary; the
    # plugin uses ripped MIDI patterns, not the rendered WAVs.
    "pattern":      0,
    "drum_groove":  0,
    "hat_groove":   0,
}

# A stricter token boundary than `\b`: filenames are normalised to
# `_`-separated tokens before matching, but `_` is itself a `\w`
# character, so `\bsnare\b` will *not* match `uncanny_long_arms_snare`.
# We use `(?:^|_)` / `(?:_|$|\d)` instead to honour token boundaries.
TOK_START = r"(?:^|_)"
TOK_END   = r"(?:_|$|\d)"

# -----------------------------------------------------------------------------
# Categorisation rules (ordered — first match wins).
# -----------------------------------------------------------------------------
RULES: list[tuple[str, str]] = [
    # Patterns / grooves first (must precede broad rules).
    (r"pattern_\d+_consolidated",                            "pattern"),
    (r"^drumgrooves\d+",                                     "drum_groove"),
    (r"^moredrumgrooves\d+",                                 "drum_groove"),
    (r"^evenmoredrum\d+",                                    "drum_groove"),
    (r"^hatgrooves\d+",                                      "hat_groove"),
    (rf"{TOK_START}drum_roll{TOK_END}",                      "drum_groove"),

    # Trap phrases / vocal chops.
    (r"trap_res_phrase",                                     "phrase"),
    (r"trap_one_shot",                                       "phrase"),
    (rf"{TOK_START}vox{TOK_END}",                            "vox"),
    (rf"{TOK_START}radioharp{TOK_END}",                      "phrase"),
    (rf"{TOK_START}nonsense_phrase{TOK_END}",                "phrase"),

    # 808 / bass.
    (r"(?:^|_)808(?:_|$|\d)",                                "bass_808"),
    (rf"{TOK_START}analog_bass",                             "bass"),
    (rf"{TOK_START}bass{TOK_END}",                           "bass"),
    (rf"{TOK_START}bassok",                                  "bass"),
    (rf"{TOK_START}pluck_bass",                              "bass"),
    (rf"{TOK_START}modo_bass",                               "bass"),
    (rf"{TOK_START}oliver_bass",                             "bass"),

    # Pad / synth / chord (run BEFORE the broader fx rules).
    (rf"{TOK_START}pad{TOK_END}",                            "pad"),
    (rf"{TOK_START}sweep_pad",                               "pad"),
    (rf"{TOK_START}suspiria_pad",                            "pad"),
    (rf"{TOK_START}ambi_pad",                                "pad"),
    (rf"{TOK_START}alien_wind",                              "pad"),
    (rf"{TOK_START}synth{TOK_END}",                          "synth"),
    (rf"{TOK_START}scan_(?:lead|synth)",                     "synth"),
    (rf"{TOK_START}growl_lead",                              "synth"),
    (rf"{TOK_START}peal_pluck",                              "synth"),
    (rf"{TOK_START}chord{TOK_END}",                          "synth"),
    (r"^syn_\d+",                                            "synth"),
    (r"^analog_lab",                                         "synth"),
    (r"^os_\d+",                                             "synth"),
    (r"^\d+_pad{TOK_END}",                                   "pad"),
    (r"^\d+_synth",                                          "synth"),
    (r"^\d+_os{TOK_END}",                                    "synth"),
    (rf"{TOK_START}fd\d{{4}}_pad",                           "pad"),
    (rf"{TOK_START}fd\d{{4}}_syn",                           "synth"),
    (rf"{TOK_START}\d+_pnochd",                              "synth"),

    # FX (after pad/synth so we don't swallow them).
    (rf"{TOK_START}fx{TOK_END}",                             "fx"),
    (rf"{TOK_START}asylum{TOK_END}",                         "fx"),
    (rf"{TOK_START}trekker{TOK_END}",                        "fx"),
    (r"^fd\d{4}_fx",                                         "fx"),
    (r"^r_\d+",                                              "fx"),

    # Drums proper.
    (rf"{TOK_START}kick{TOK_END}",                           "kick"),
    (rf"{TOK_START}snare{TOK_END}",                          "snare"),
    (rf"{TOK_START}clap{TOK_END}",                           "clap"),
    (rf"{TOK_START}openhat{TOK_END}",                        "hat_open"),
    (rf"{TOK_START}open_hat{TOK_END}",                       "hat_open"),
    (rf"(?:^|_)oh{TOK_END}",                                 "hat_open"),
    (rf"{TOK_START}hihat{TOK_END}",                          "hat_closed"),
    (rf"{TOK_START}hat{TOK_END}",                            "hat_closed"),
    (rf"{TOK_START}half_open{TOK_END}",                      "hat_open"),
    (rf"{TOK_START}perc{TOK_END}",                           "perc"),
    (rf"{TOK_START}tom{TOK_END}",                            "tom"),
    (rf"{TOK_START}crash{TOK_END}",                          "crash"),
    (rf"{TOK_START}ride{TOK_END}",                           "ride"),

    # Misc per-pack tags.
    (r"^\d+_st-l",                                           "synth"),
    (r"^\d+_lop\d+",                                         "drum_groove"),
    (r"^\d+_dr\.loop",                                       "drum_groove"),
]

CRE = [(re.compile(r, re.IGNORECASE), c) for (r, c) in RULES]

UUID_RE = re.compile(r"^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-"
                     r"[0-9a-f]{4}-[0-9a-f]{12}_", re.IGNORECASE)


def strip_uuid(name: str) -> str:
    return UUID_RE.sub("", name)


def canonical(name: str) -> str:
    """Drop UUID, normalise separators, and lowercase for matching."""
    s = strip_uuid(name)
    s = s.replace(".wav", "")
    s = re.sub(r"[+\-\s]+", "_", s)
    s = re.sub(r"_+", "_", s)
    return s.lower().strip("_")


def classify(canon: str) -> str:
    for cre, cat in CRE:
        if cre.search(canon):
            return cat
    return "fx"  # safe fallback — never silently lose a sample


def main() -> None:
    if not RAW.is_dir():
        raise SystemExit(f"raw kit dir missing: {RAW}")

    # 1. Collect + classify, deduplicating by canonical name (some
    #    attachments are uploaded multiple times with different UUIDs).
    seen: set[str] = set()
    buckets: dict[str, list[Path]] = {}
    for entry in sorted(RAW.iterdir()):
        if not entry.name.lower().endswith(".wav"):
            continue
        canon = canonical(entry.name)
        if canon in seen:
            continue
        seen.add(canon)
        cat = classify(canon)
        buckets.setdefault(cat, []).append(entry)

    print("Categorisation counts:")
    for cat in sorted(buckets):
        print(f"  {cat:<12}  {len(buckets[cat])}")

    # Wipe any prior Drocetti__ kit files (idempotent re-runs).
    for old in KITDIR.glob("Drocetti__*.wav"):
        old.unlink()

    # 2. Copy with normalised names, capped per-category by BUNDLE_LIMIT.
    KITDIR.mkdir(parents=True, exist_ok=True)
    manifest_files: list[str] = []
    skipped = 0
    for cat, paths in sorted(buckets.items()):
        cap = BUNDLE_LIMIT.get(cat, 4)
        ordered = sorted(paths, key=lambda p: p.name)
        for idx, src in enumerate(ordered[:cap], start=1):
            dst = KITDIR / f"Drocetti__{cat}_{idx:02d}.wav"
            real = src.resolve()
            shutil.copyfile(real, dst)
            manifest_files.append(dst.name)
        skipped += max(0, len(ordered) - cap)

    print(f"Wrote {len(manifest_files)} files to {KITDIR} "
          f"(skipped {skipped} not bundled — full pack ships via installer)")

    # 3. Update MANIFEST.json — add the Drocetti kit alongside the
    # existing NuRockYamaha / HeavyStudio entries.
    if MAN.is_file():
        manifest = json.loads(MAN.read_text())
    else:
        manifest = {"kits": {}}

    manifest.setdefault("kits", {})["Drocetti"] = sorted(manifest_files)
    MAN.write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Updated {MAN}")


if __name__ == "__main__":
    main()
