# Code-Review KLINGE 2.0 – Befunde und Korrekturen

Geprüft: alle neuen Teile (Schaltungsmodelle, 5 Bänder, Band-Dynamik, Feedback, Modulation, Capture,
Session-Funktionen, Editor) und ihre Wechselwirkung mit dem Bestand. Gleiche Maßstäbe wie beim Review
von 1.0: numerische Stabilität, Klickfreiheit, Echtzeit- und Thread-Sicherheit, Host-Verhalten.

## Theoretische Stabilität der neuen Teile

- **Feedback** ist die einzige globale Rückkopplung. Das Rücksignal läuft durch Tiefpass, Hochpass und
  `tanh` und ist damit auf |x| < 1 begrenzt; die Schleifenverstärkung ist ≤ 0,95 und wird zusätzlich vom
  Eingangspegel gegatet. Der Eingang der Bänder bleibt also immer beschränkt: BIBO-stabil, unabhängig davon,
  was die Verzerrung in der Schleife macht.
- **Triode**: Kathoden-RC trapezförmig integriert (A-stabil), Gitterstrom lädt einen leckenden Koppelkondensator,
  Ausgang über die beschränkte Röhrenkennlinie.
- **Diode**: Wave Digital Filter mit geschlossener Lösung (Wright-Omega) für das Diodenpaar, passiv und damit stabil.
- **Hysteresis**: Jiles-Atherton mit Runge-Kutta 4, Magnetisierung auf ±1,05 Ms begrenzt.
- **Iron**: leckender Fluss-Integrator (Pol < 1), Wicklung über `tanh` begrenzt.
- **Stack**: zwei beschränkte Triodenstufen, Sag-Follower und RBJ-Biquads mit Polen im Einheitskreis.
- **Band-Dynamik** und **Sag** verändern nur Verstärkungen innerhalb fester Grenzen (max. 8:1 Kompression,
  Expansion bis 1:4, Sag nur abschwächend).
- **Modulation** wirkt auf normierte Werte und wird auf [0, 1] begrenzt: jeder Parameter bleibt in seinem
  Bereich, egal wie viele Quellen auf ihm liegen. Änderungen werden wie Reglerbewegungen geglättet.
- **Capture**: WaveNet- und LSTM-Profile haben beschränkte Aktivierungen (tanh/sigmoid) und lineare Ausgänge,
  ihr Ausgang ist damit beschränkt. Zusätzlich NaN/Inf-Schutz pro Sample.

## Echtzeit- und Thread-Sicherheit

- NAM-Profile werden auf einem Hintergrund-Thread geladen und über einen einzelnen atomaren Slot an den
  Audio-Thread übergeben; das alte Modell wird im Message-Thread gelöscht. Der Audio-Thread alloziert nicht.
- Modulations-Slots, Shaper-Tabelle und Anzeige-Werte sind Atomics; Analyse-Daten laufen über
  Single-Producer/Single-Consumer-FIFOs. Snapshots, Sperren und Presetname sind per SpinLock geschützt.
- Undo-Schritte entstehen nur aus Bediengesten, nicht aus Automation.

## Befunde und Korrekturen

| # | Befund | Auswirkung | Korrektur | Nachweis |
|---|---|---|---|---|
| 1 | Feedback-Leseposition in float: `Position − Verzögerung + Länge` konnte genau auf die Länge runden | Lesen eines Samples hinter dem Puffer (von AddressSanitizer gefunden) | Rechnung in double, Verzögerung begrenzt, Schutz am Umbruch | ASan/UBSan: alle Gruppen ohne Befund |
| 2 | Glättung: Startwert NaN, `same()` hält NaN für gleich | Glättung aus, Klicks bei Focus, Sperren, Dynamik (20× über Grenze) | Startwert −1e30 | Klicktest: alle 36 Fälle bestanden |
| 3 | NAM registriert Architekturen in statischen Initialisierern, die der Linker aus einer statischen Bibliothek entfernt | „No config parser registered for LSTM“ | NAM als OBJECT-Bibliothek | Capture-Tests (LSTM, WaveNet, Standard-WaveNet) |
| 4 | Feedback konnte nach Stille weiterbrummen | Dauerton ohne Eingang | Gate über Eingangshüllkurve (−40 dBFS, 250 ms) | Feature-Test: 95 % Feedback bleibt beschränkt und verstummt nach dem Signal |
| 5 | Iron: zu viel Pegel und Klirr in den Höhen | Kalibrierung daneben | Wicklungsbegrenzung, Kompensationstabellen neu erzeugt | Algorithmus- und Kalibriertest |
| 6 | **CLAP**: Text ↔ Wert nicht umkehrbar (z. B. „10 Hz“ ↔ „10.00 Hz“, „kHz“ als Hz gelesen) | Eingetippte Werte falsch, clap-validator rot | Formatierung entscheidet an gerundeten Werten, eigene Parser für Hz/kHz/Off, dB, %, ms, ° | 58 145 Umwandlungen ohne Abweichung, clap-validator grün |
| 7 | **CLAP**: Nach dem Laden eines States wurde der Host nicht zum Neueinlesen der Werte aufgefordert | Host zeigt alte Werte | Rescan-Anforderung nach `setStateInformation` (nur CLAP) | clap-validator State-Tests grün |
| 8 | Analyse: Eingangs- und Ausgangs-FIFO konnten auseinanderlaufen | Versatz im Oszilloskop | Immer gleich viele Samples aus beiden lesen | Sichtprüfung |
| 9 | Analyse blieb aus, wenn der Host das Fenster erst nach dem Tab anzeigte | Leeres Spektrum | Aktivierung zusätzlich bei jedem Refresh prüfen | Editor-Logiktest |
| 10 | Gespeicherter Tab auf 4 von 5 begrenzt | Analyse-Tab nicht wiederhergestellt | Grenze 0…4 | — |
| 11 | Kurvenanzeige für Amp/Stack: Phasendrehung der Box ergibt sinnlose Figur | Irreführend | Anzeige einer Periode der Ausgangswelle | Screenshots |
| 12 | Undo/Redo-Symbole vertauscht | Bedienfehler | Richtung korrigiert | Screenshots |
| 13 | Fehlende Abhängigkeit führte zu kryptischem CMake-Fehler | Schwer zu bauen | Prüfung mit klarer Meldung | — |
| 14 | Tastaturfokus wurde auch angefordert, wenn das Fenster (noch) nicht sichtbar war | JUCE-Assertion im Debug-Build | Fokus nur auf sichtbaren Komponenten | Debug-Build: 0 Assertions |
| 15 | Testsignal mit `std::normal_distribution` (je nach Standardbibliothek verschieden) | Golden-Werte nicht portabel | Eigener Generator für Golden-Tests | identische Fingerprints auch mit FMA/AVX2-Build |

## Verifikation

- Release-Testsuite: 254 Prüfungen bestanden (`tests/test-results.txt`), darunter 36 Klick-Szenarien,
  Golden Fingerprints aller 33 Factory-Presets und Editor-Logik.
- Golden Fingerprints mit `-march=x86-64-v3 -ffp-contract=fast` gebaut: 0,000 dB Abweichung.
- AddressSanitizer + UndefinedBehaviorSanitizer (16 Testgruppen) und ThreadSanitizer (Audio-Thread parallel zu
  State, Presets, Session, Editor): ohne Befund.
- Debug-Build mit JUCE-Assertions und Leak-Detector (State, Presets, Session, Modulation, Editor, Golden): 0 Assertions, 0 Leaks.
- Alle Plugin-Quellen mit clang 18 und JUCEs Warnstufe geprüft: keine Fehler, keine Warnungen. Windows (MSVC) und
  macOS (Xcode) bauen in der CI; lokal standen dafür keine Compiler zur Verfügung.
- pluginval Strenge 10 (VST3): bestanden. clap-validator 0.4.1 (CLAP): 34 bestanden, 0 fehlgeschlagen,
  1 Warnung, 9 nicht anwendbar.
- Rechenlast (diese Testmaschine, 1 Kern): Single Amp 4x ≈ 9 %, Multiband 8x mit Harmonics ≈ 48 %,
  Standard-NAM-Profil mono ≈ 38 % (stereo etwa doppelt). Die NAM-Last ist die des NAM-Kerns selbst
  und hängt kaum von der Blockgröße ab (gemessen 16 bis 512 Samples).

## Bewusst so belassen

- **clap-validator „state-invalid-random“** meldet eine Warnung: Der CLAP-Wrapper (clap-juce-extensions) meldet jedes
  Laden als erfolgreich. Zufällige Daten werden von KLINGE verworfen, es stürzt nichts ab.
- **NAM-Profile** werden nicht umgerechnet, wenn die Session eine andere Samplerate hat; KLINGE zeigt das an.

---

# Code-Review KLINGE 1.0 – Befunde und Korrekturen

Geprüft: kompletter Quellcode (DSP, Processor, Presets, UI) auf numerische Stabilität,
Klickfreiheit, Echtzeit- und Thread-Sicherheit. Jede Korrektur ist durch einen Test belegt.

## Theoretische Stabilität (unverändert gültig)

- Es gibt **keine globale Rückkopplung**. Rekursiv sind nur Filter und Hüllkurvenfolger.
  - Alle Biquads sind RBJ-Entwürfe mit Q > 0 und einer Grenzfrequenz in (0,1 Hz … 0,49 fs). Ihre Pole liegen damit strikt im Einheitskreis.
  - Die TPT-SVFs sind für jedes g > 0 stabil.
  - Der Tilt-Shelf und seine exakte Inverse sind für jedes G > 0 stabil, weil der Pol der Inversen bei (G−K)/(G+K) liegt.
  - DC-Blocker und Follower haben Koeffizienten < 1.
- **Alle Kennlinien sind beschränkt** (tanh, Clip, Fold, Kubik, Fuzz, Rectify). Der Harmonic Designer ist
  durch die Hüllkurve bzw. eine Begrenzung auf ±1 beschränkt.
- **Alle Verstärkungen sind begrenzt:** Kompensation ≤ +12 dB, Band-Level ≤ +12 dB, Punch ≤ +10 dB,
  Auto Gain ≤ +18 dB (zusätzlich gegen Überschwingen gesichert), Output ≤ +24 dB.
  → Das System ist BIBO-stabil: Ein beschränkter Eingang ergibt immer einen beschränkten Ausgang.
- NaN- und Inf-Schutz greift pro Band und am Ausgang; Denormals werden per FTZ/DAZ und zusätzlich im Code abgefangen.

## Befunde und Korrekturen

| # | Befund | Auswirkung | Korrektur | Nachweis |
|---|---|---|---|---|
| 1 | Filter- und Follower-Zustände in 32-Bit-float | Bei 16x × 192 kHz (3 MHz) liegen Pole ~1e-5 am Einheitskreis: Crossover-Fehler, langsame Follower können „einfrieren“ | Alle rekursiven Zustände und Koeffizienten in 64 Bit | Präzisionstest bei 3 MHz: Crossover-Abweichung 0,14 dB, Follower korrekt |
| 2 | Band On/Solo/Mute, Delta, Clip schalten hart | Klicks | Rampen (4 ms / 20 ms) | Klicktest: alle ≤ 1,1 |
| 3 | Algorithmus-Wechsel hart | Klick | Wet-Anteil blendet aus, Wechsel, blendet ein (Dry bleibt durchgehend) | Klicktest |
| 4 | Routing- und Oversampling-Wechsel hart, Filter schwingen nach | Klick | Ausblenden → Umschalten → Stille, bis die neuen Filter gefüllt sind → Einblenden | Klicktest: Ratio 1,00 |
| 5 | Filterfrequenzen, Shape, Dynamics, Punch, Harmonics, Lock springen pro Host-Block | Zipper bei Automation, abhängig von der Blockgröße | Kontrollrate 32 Samples, exponentielle Glättung (30 ms), Grenzfrequenz pro Sample interpoliert | Klicktest für alle 36 Regler |
| 6 | Pre/Post-Filter: Biquad-Koeffizientensprünge, harter „Off“-Umschalter mit veraltetem Zustand | Zipper/Klick bei Sweeps | TPT-State-Variable-Filter (modulationsstabil) mit Überblendung bei Off | Klicktest: 20k → 2k Sweep 1,00 |
| 7 | Focus: De-Emphasis wechselte gleichzeitig mit Pre-Emphasis, obwohl die Latenz dazwischen liegt | Kurzer Tonfehler beim Verstellen | De-Emphasis folgt mit der Latenz verzögert (Verlauf pro Chunk) | Klicktest Focus |
| 8 | Crush hielt Samples auch bei Rate 0 | Leichter Höhenverlust | Haltelänge in Oversampling-Samples, bei Rate 0 transparent | — |
| 9 | Rectify-ADAA genau bei Shape 50 % inkonsistent, logcosh mit Auslöschung bei kleinen Werten | Winziger Sprung, Ungenauigkeit leiser Signale | Taylor-Reihe für kleine Argumente, exakte Behandlung c → 0 | Ableitungstest F′ = f |
| 10 | `setLatencySamples` im Audio-Thread | Nicht echtzeitsicher (Host-Benachrichtigung) | Latenz wird per Timer im Message-Thread gemeldet | pluginval 10 |
| 11 | UI-State und Presetname ohne Synchronisation, Host kann aus anderem Thread speichern | Datenrennen | Atomics bzw. SpinLock, `getStateInformation` verändert nichts mehr | ThreadSanitizer: 0 Befunde |
| 12 | Host-Bypass ohne Latenzkompensation | Zeitversatz und Klick beim Bypass | `processBlockBypassed` lässt die Engine neutral weiterlaufen | Bypass-Null −74,8 dB, Klicktest |
| 13 | `reset()` nicht überschrieben | Altes Signal nach Transport-Stopp | `reset()` leert alle Zustände | — |
| 14 | Compiler-Warnungen (float-equal, switch-enum, shadow) | Code-Hygiene | Behoben | 0 Warnungen mit JUCE-Warnstufe |

## Verifikation

- Release-Testsuite: 165 Prüfungen bestanden, darunter 36 Klick-Szenarien mit Negativkontrolle.
- AddressSanitizer + UndefinedBehaviorSanitizer: alle Testgruppen ohne Befund.
- ThreadSanitizer: Audio-Thread parallel zu State-/Preset-/Parameter-Operationen und offenem Editor ohne Befund.
- Debug-Build mit JUCE-Assertions und Leak-Detector: 0 Assertions, 0 Leaks.
- pluginval Strenge 10: bestanden (Release- und Debug-Build).

## Bewusst so belassen

- **Low-Latency-Oversampling** (IIR) ist nicht phasenlinear. Dry und Wet laufen aber durch identische
  Filter, Parallel-Mischen bleibt deshalb exakt (0,00 dB Abweichung).
- **Amp bei extremem Drive** erzeugt nahezu Rechteckwellen. Das Aliasing ist dann physikalisch bedingt höher.
  Mit 16x liegt es bei −47 dB, bei normalen Einstellungen deutlich darunter.
- **Gemeldete Latenz** folgt einem Oversampling-Wechsel innerhalb von 50 ms. So arbeiten Plugins üblicherweise.
