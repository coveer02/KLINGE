# KLINGE 2.0 – Distortion-Multitool (VST3 / AU / CLAP)

KLINGE ist ein Verzerrer für Produzenten und Mixing Engineers, gebaut nach einer Idee:
**Charakter und Kontrolle, ohne dass der Sound seine Lebendigkeit verliert.**
Die Härte kommt aus den Algorithmen, die Kontrolle aus allem, was davor, danach und darum herum passiert.

## Neu in 2.0

- **5 Schaltungsmodelle** mit Gedächtnis: Triode (12AX7), Diode (RC-Clipper als Wave Digital Filter),
  Hysteresis (Tape-Magnetisierung nach Jiles-Atherton), Iron (Übertrager-Sättigung) und Stack
  (zweistufiger Amp mit Sag, Klangregelung und Box).
- **Bis zu 5 Bänder**, pro Band Stereo/Mid/Side, eigene Dynamik (Kompressor bis 8:1 oder Expander) und Sag.
- **Feedback-Schleife** mit Zeit und Ton, **Analog**-Regler für Kanaltoleranzen, Drift und Rauschen.
- **Modulation**: 2 LFOs, 2 Hüllkurvenfolger (auch Sidechain), ein zeichenbarer Shaper im Songtempo,
  MIDI-Velocity und Modulationsrad. Quelle per Drag & Drop auf jeden Regler ziehen.
- **Capture**: lädt Neural-Amp-Modeler-Profile (`.nam`) echter Amps und Pedale, vor oder nach den Bändern.
- **Analyse**: Spektrum, Oszilloskop, Pegel pro Band.
- **Workflow**: A/B/C/D-Vergleich, Undo/Redo, Preset-Browser mit Suche, Kategorien, Tags und Favoriten,
  Sperren einzelner Parameter, Zufallsgenerator in musikalischen Grenzen.
- **Offline-Qualität**: beim Bounce automatisch mehr Oversampling.
- **CLAP** zusätzlich zu VST3 und AU. Sessions von KLINGE 1.0 laden unverändert.

## Signalfluss

```
Input ─► Pre Low/High Cut ─► [Capture: vor den Bändern] ─► Focus ─► Oversampling
                                                                      ▼
          Routing: Single │ Multiband (2–5 Bänder) │ Mid/Side   ◄── Feedback (Zeit, Ton)
                                                                      ▼
  pro Band: Stereo/Mid/Side ─► Dynamik (vor) ─► Drive, Sag, Dynamics ─► Algorithmus
            ─► Harmonic Designer ─► Dynamik (nach) ─► Analog ─► Mix/Level
                                                                      ▼
  Downsampling ─► De-Emphasis ─► [Capture: nach den Bändern] ─► Faithful ─► Post Low/High Cut
            ─► Punch ─► Auto Gain ─► Shaper ─► Mix mit phasengleichem Dry ─► Output
```

Das Dry-Signal läuft durch exakt dieselbe Oversampling-Kette und dieselben Crossover-Allpässe wie das
Wet-Signal. Parallel-Mischen erzeugt deshalb in keinem Modus Kammfilter, auch nicht mit 5 Bändern.

## Bedienung

**Basic** zeigt das Wesentliche: Typ, Drive, Character, Focus, Punch, Faithful, Mix, Output,
Transferkurve, Obertöne und den Shaper mit Pattern und Rate.

**Advanced** hat links Input, Dynamics, Feedback & Analog und Output (inklusive Oversampling-Filter und
Offline-Qualität), rechts Kurve, Obertöne und Pegel. In der Mitte fünf Tabs:

| Tab | Inhalt |
|---|---|
| Bands | Routing, Bandanzahl, ziehbare Trennfrequenzen, pro Band Typ, Drive, Shape, Mix, Level, On/Solo/Mute. Darunter Details des gewählten Bands: Bias, Harmonics, Sag, Stereo, Dynamik mit Threshold und Position. |
| Harmonics | Harmonic Designer (Obertöne 2–8, auch negativ), Lock, Anteil pro Band |
| Modulation | Quellen, ihre Editoren und die Liste aller Zuweisungen mit Tiefe |
| Capture | NAM-Profil laden, an/aus, Position, True Stereo, Input/Output/Mix |
| Analyse | Spektrum (Input grau, Output gold), Oszilloskop, Pegel pro Band |

Die Statuszeile unten erklärt jedes Element, auf das du zeigst. Doppelklick setzt einen Regler zurück,
Rechtsklick öffnet Modulation, Sperre und Reset. Das Fenster ist frei skalierbar.
Tastatur: Strg/Cmd+Z Undo, Strg/Cmd+Shift+Z oder Strg+Y Redo.

## Die 16 Algorithmen

| Gruppe | Typ | Charakter | Shape-Regler |
|---|---|---|---|
| Warm | Silk | Konsolen-Sättigung, sehr nah am Original | Density |
| | Tape | weiches Knie | Wear |
| | Hysteresis | Tape-Magnetisierung mit Gedächtnis: dick, leicht verschmiert | Width (Schleifenbreite) |
| | Iron | Übertrager: Tiefen sättigen zuerst, Höhen bleiben offen | Core |
| | Tube | asymmetrisch, gerade Obertöne | Sag |
| | Triode | 12AX7-Stufe: laute Schläge verschieben den Arbeitspunkt, der Sound blüht auf und erholt sich | Drift |
| Clip | Detail | Clipper, der die leisen Details zurückholt | Detail |
| | Diode | Dioden-Clipper mit Kondensator: Tiefen clippen härter | Tone |
| | Amp | zweistufiger High-Gain-Amp mit Box | Body |
| | Stack | zwei modellierte Trioden, Klangregelung, Netzteil-Sag, Endstufe, Box | Voice (tight ↔ loose) |
| | Fuzz | asymmetrisches Fuzz | Gate |
| | Grind | Crossover-Verzerrung und Hard Clip: Körnung in Tails | Grain |
| Creative | Fold | Wavefolder: mehr Pegel = andere Klangfarbe | Edge |
| | Rectify | Gleichrichter bis zur Oktave | Octave |
| | Crush | Bit- und Samplerate-Reduktion | Bits/Rate |
| | Slew | schnelle Flanken werden zu Grit | Smear |

Die gedächtnislosen Kurven laufen durch Antiderivative Anti-Aliasing. Die Schaltungsmodelle lösen ihre
Gleichungen pro Sample (Wave Digital Filter, Trapez- bzw. Runge-Kutta-Verfahren) und sind auf gleiche
Lautheit kalibriert.
Für Kurven mit Gedächtnis zeigt das Display eine Lissajous-Schleife (offene Schleife = Gedächtnis),
für Amp und Stack eine Periode der Ausgangswelle.

## Werkzeuge gegen „ausgewaschen“

- **Punch** legt die Mikrodynamik des Dry-Signals wieder auf den verzerrten Sound. Über 100 % übertreibt es sie.
- **Dynamics** (global, bipolar): Positiv zerren Schläge mehr, negativ zerren Tails mehr (der „Woosh“-Crunch).
- **Band-Dynamik**: Expander pro Band holt Bewegung zurück, die Verzerrung plattmacht; Kompressor zähmt Spitzen.
  *Vor* der Verzerrung formt sie, was zerrt; *nach* der Verzerrung formt sie das Ergebnis.
- **Shaper**: Lautstärkekurve im Songtempo, z. B. Pump, Gate oder Off-Beat. Er kann auch jeden Regler modulieren.
- **Faithful** hält die Tief/Mitte/Hoch-Balance des Originals.
- **Focus** kippt das Signal vor der Verzerrung und exakt invers danach.

## Modulation

1. Im Tab *Modulation* eine Quelle anklicken, um sie einzustellen.
2. Die Quelle auf einen beliebigen Regler ziehen (oder Rechtsklick auf den Regler → *Modulate with*).
3. Der Ring um den Regler zeigt Bereich und aktuellen Wert. Die Tiefe stellst du in der Liste
   oder per Rechtsklick ein. Bis zu 16 Zuweisungen.

Quellen: **LFO 1/2** (7 Formen, frei oder im Songtempo, Phase, Glättung, MIDI-Retrigger),
**Env 1/2** (Input oder Sidechain, Attack, Release, Empfindlichkeit), **Shaper** (zeichenbar,
10 Muster, Raster 1/32, Shift für frei, Doppelklick fügt Punkte hinzu oder entfernt sie,
Segment ziehen biegt die Kurve), **Velocity** und **Mod wheel** (MIDI).

**Sidechain in Ableton Live:** Im Gerätekopf von KLINGE den Sidechain-Bereich aufklappen und die
Quellspur (z. B. Kick) wählen. Dann bei Env 1 oder Env 2 *Sidechain* aktivieren.
**MIDI:** Eine MIDI-Spur auf KLINGE routen, dann Velocity oder Mod wheel auf Regler ziehen.

## Capture (Neural Amp Modeler)

*Load profile…* lädt eine `.nam`-Datei. Das Profil wird beim Laden auf gleichen Pegel normalisiert und
läuft vor oder nach den Bändern, mono (sparsam) oder True Stereo. Profile sind auf eine Samplerate
trainiert, meist 48 kHz; KLINGE zeigt an, wenn die Session anders läuft. Der Dateipfad wird mit der
Session gespeichert. Standard-Profile sind rechenintensiv: ein Profil in Mono kostet etwa so viel wie
KLINGE selbst im Multiband-Modus mit 8x Oversampling, True Stereo das Doppelte.

## Schranz-Startpunkte

- **Woosh Restore / Woosh Shaper**: Crunch in den Tails, Punch hält die Schläge, der Shaper pumpt im Takt.
- **Schranz Stack**: das Stack-Modell als kontrollierbare Alternative zum Ableton Amp.
- **Kick Drive Low Only**, **Kick Duck Sidechain**, **Hats Grit Side**, **Gated Stutter**, **Rumble Grind**.

## KLINGE an den Ableton Amp angleichen

Das Profiling-Werkzeug misst, was ein beliebiger Verzerrer mit einem Testsignal macht:

```
KlingeProfiler make-signal  KLINGE_Profiling_Signal.wav
KlingeProfiler analyze      Ableton_Amp_render.wav  report/
KlingeProfiler compare      Ableton_Amp_render.wav  KLINGE_render.wav  report/
```

Das Testsignal enthält Sweeps auf mehreren Pegeln, Pegelrampen, Intermodulationstests (SMPTE, CCIF) und
Bursts. Der Bericht (`report.md` mit Grafiken) zeigt Frequenzgang, Obertöne über Pegel, Kompression,
Intermodulation und Erholzeit. `compare` stellt zwei Renderings gegenüber, so lässt sich Stack Schritt
für Schritt auf deine Amp-Einstellungen abstimmen.

## Bauen

### Ohne eigene Entwicklungsumgebung: GitHub Actions
1. Repository anlegen und den Inhalt dieses Ordners hochladen.
2. Unter **Actions** läuft „Build KLINGE“: Windows, macOS (Universal), Linux, Sanitizer.
3. Downloads pro Lauf: `KLINGE-Windows` (Installer), `KLINGE-macOS` (pkg), `KLINGE-Linux` (tar.gz)
   und `KLINGE-Screenshots`. Jeder Build wird mit pluginval (Strenge 10), clap-validator und auf macOS
   mit auval geprüft.
4. Ein Tag `v2.0.0` erzeugt zusätzlich einen Release-Entwurf mit allen Installern.

Signieren ist vorbereitet und läuft, sobald die Secrets gesetzt sind:
Windows `WIN_CERT_PFX` (Base64) und `WIN_CERT_PASSWORD`; macOS `MACOS_CERT_P12`, `MACOS_CERT_PASSWORD`,
`MACOS_SIGN_APP`, `MACOS_SIGN_INSTALLER` und für die Notarisierung `APPLE_ID`, `APPLE_TEAM_ID`,
`APPLE_APP_PASSWORD`. Ohne Secrets werden die macOS-Plugins ad-hoc signiert.

### Lokal
Voraussetzungen: CMake ≥ 3.22, ein C++20-Compiler (Visual Studio 2022, Xcode 15, GCC 11+), git.
```
bash tools/fetch-deps.sh          # JUCE, NAM Core, Eigen, clap-juce-extensions (feste Versionen)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target KLINGE_VST3 KLINGE_CLAP
```
Tests: `cmake -B build -DKLINGE_BUILD_TESTS=ON`, dann `build/KlingeTests_artefacts/Release/KlingeTests`
(einzelne Gruppen per Name, z. B. `KlingeTests golden`). `KlingeTests ui <ordner>` schreibt Screenshots.

## Installieren

- **Windows**: Installer `KLINGE-2.0.0-Windows-Setup.exe` (VST3 und CLAP wählbar).
- **macOS**: `KLINGE-2.0.0-macOS.pkg` (VST3, AU, CLAP). Unsignierte Builds:
  `xattr -cr /Library/Audio/Plug-Ins/VST3/KLINGE.vst3` (entsprechend für .component und .clap).
- **Linux**: Archiv entpacken, `./install.sh` (nach `~/.vst3` und `~/.clap`, mit `--system` für alle Nutzer).

In Ableton danach unter *Einstellungen → Plug-Ins* neu scannen.

## Getestet

Die Testsuite (`tests/TestHarness.cpp`, 254 Prüfungen, Protokoll in `tests/test-results.txt`) rendert echtes Audio durch das Plugin:
Pegeltreue und Aliasing aller Algorithmen, Kammfreiheit beim Parallel-Mischen in allen Routings und
Bandzahlen, Punch, Faithful, Auto Gain, Harmonic Designer, Schaltungsmodelle (Stabilität, Gedächtnis,
Kalibrierung), Modulation (LFO-Tempo-Sync, Hüllkurven, Shaper, MIDI, Sidechain), Feedback, Capture,
Undo/Snapshots/Sperren/Zufall, Klickfreiheit bei 36 Schaltvorgängen, Präzision bei 3 MHz interner Rate,
Bypass, Nebenläufigkeit mit offenem Editor, Editor-Logik (Drag & Drop, Tastatur, Browser, Tabs),
Sessions von Version 1.0 und **Golden Fingerprints** aller Factory-Presets, die jede ungewollte
Klangänderung melden.

Alles läuft auch unter AddressSanitizer, UndefinedBehaviorSanitizer und ThreadSanitizer.
VST3 besteht pluginval auf Strenge 10, CLAP den clap-validator. Details in `REVIEW.md`, Änderungen in `CHANGELOG.md`.

## Lizenzen

KLINGE nutzt JUCE (AGPLv3 oder kommerzielle JUCE-Lizenz), Neural Amp Modeler Core (MIT), Eigen (MPL 2.0),
nlohmann/json (MIT), clap-juce-extensions und das CLAP-SDK (MIT) sowie die Schriften Space Grotesk und
JetBrains Mono (SIL OFL). Für eigenen Gebrauch ist das unproblematisch. Wer KLINGE verteilt oder verkauft,
braucht eine JUCE-Lizenz oder veröffentlicht den Quellcode unter AGPLv3. Details: `THIRD_PARTY_NOTICES.md`.
