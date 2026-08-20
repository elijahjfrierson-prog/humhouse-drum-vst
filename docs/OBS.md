# Recording HumHouse Drums X in OBS

OBS cannot load the plugin. Its "VST 2.x Plug-in" filter is the only plugin host
OBS has, and it loads VST **2** only - a VST3 is invisible to it no matter where
it is installed. Steinberg no longer licenses the VST2 SDK, so there is no VST2
build to ship.

Record the **standalone app** instead and send its audio to OBS through a virtual
audio device. Five minutes, once.

## macOS

1. Install [BlackHole 2ch](https://existential.audio/blackhole/) (free).
2. Open *Audio MIDI Setup* -> **+** -> *Create Multi-Output Device*, and tick
   both **BlackHole 2ch** and your speakers or headphones. This is what lets you
   hear the kit while OBS records it.
3. Launch **HumHouse Drums X** (Applications), then *Options -> Audio/MIDI
   Settings* and pick that Multi-Output Device as the output.
4. In OBS: **+** -> *Audio Input Capture* -> device **BlackHole 2ch**.

## Windows

1. Install [VB-CABLE](https://vb-audio.com/Cable/) (free).
2. Launch **HumHouse Drums X**, then *Options -> Audio/MIDI Settings* and set the
   output to **CABLE Input (VB-Audio Virtual Cable)**.
   To hear it as well, open Windows *Sound settings -> CABLE Output ->
   Listen to this device* and choose your speakers.
3. In OBS: **+** -> *Audio Input Capture* -> device **CABLE Output**.

## Capturing the window too

*Window Capture* -> **HumHouse Drums X** records the UI, so the arrangement strip
and the pad move on screen while the kit plays.

## If OBS records silence

- The standalone's own output must be the virtual device, not the speakers - a
  virtual cable carries only what is sent into it.
- On macOS, macOS asks for microphone permission the first time OBS opens an
  Audio Input Capture; without it the source stays flat.
