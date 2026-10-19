# Changelog

## 2.0.0

### Sound
- Five stateful circuit models: **Triode** (12AX7 stage with cathode bias, grid conduction and a wandering
  operating point), **Diode** (RC diode clipper as a wave digital filter), **Hysteresis** (Jiles-Atherton tape
  magnetisation), **Iron** (transformer flux saturation) and **Stack** (two triode stages, tone stack, supply
  sag, power stage and cabinet). All are loudness-calibrated like the existing types.
- Multiband with **2 to 5 bands** (Linkwitz-Riley crossovers with allpass compensation, so parallel mixing
  stays free of comb filtering).
- Per band: **Stereo / Mid / Side** processing, **band dynamics** (compressor up to 8:1 or expander, threshold,
  before or after the distortion) and **Sag**.
- **Feedback** loop around the distortion (amount, time 0.05–30 ms, tone), gated so it never drones on silence.
- **Analog**: channel tolerances, operating-point drift, signal-dependent noise and bandwidth.
- **Offline render quality**: more oversampling while the host bounces, with the same reported latency.

### Modulation
- 2 LFOs (7 shapes, tempo sync with 18 divisions, phase, smoothing, MIDI retrigger), 2 envelope followers
  (input or sidechain), a drawable **Shaper** locked to the host tempo (10 patterns), MIDI velocity and mod wheel.
- 16 modulation slots; drag a source onto any continuous parameter or use the right-click menu.
  Knobs show the modulation range and the current value.
- Sidechain input bus (off by default; hosts enable it on demand).

### Capture
- Loads **Neural Amp Modeler** profiles (`.nam`, WaveNet and LSTM) before or after the bands, mono or
  true stereo, with input, output and mix. Models load on a background thread and swap in without clicks.

### Workflow and UI
- New Advanced layout with the tabs Bands, Harmonics, Modulation, Capture and Analyse.
- **Analyse**: spectrum (input and output), oscilloscope and band levels.
- **A/B/C/D** snapshots, **undo/redo** (64 steps, keyboard shortcuts), **preset browser** with search,
  categories, tags and favourites, user presets with tags.
- **Locks**: parameters can be kept when presets load or when randomising. **Random** within musical ranges.
- 14 new factory presets (33 in total).
- Curve display shows a Lissajous loop for models with memory and one output cycle for Amp and Stack.

### Platform
- **CLAP** format (via clap-juce-extensions), in addition to VST3 and AU.
- Installers: Inno Setup (Windows), pkg (macOS, signing and notarisation when secrets are configured),
  tar.gz with install script (Linux). Tagged builds create a draft release.
- CI validates with pluginval (strictness 10), clap-validator and auval, and runs the test suite under
  AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer.

### Compatibility
- All parameter IDs of 1.0 are unchanged; sessions and presets of 1.0 load with their values.
  New parameters take their defaults.

### Tools
- `KlingeProfiler`: test signal, analysis and comparison of any distortion (frequency response, harmonics
  over level, compression, intermodulation, recovery).
- Golden fingerprints of all factory presets in the test suite.

## 1.0.0
- First release: 11 algorithms with ADAA, Single / Multiband (3) / Mid-Side, Punch, Dynamics, Faithful,
  Focus, harmonic designer, auto gain, delta, oversampling up to 16x, Basic and Advanced pages.
