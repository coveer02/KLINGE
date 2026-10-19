#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Engine.h"
#include "dsp/Modulation.h"
#include "dsp/Capture.h"
#include "Presets.h"
#include "Session.h"
#if KLINGE_WITH_CLAP
 #include <clap-juce-extensions/clap-juce-extensions.h>
#endif

// Editor-only settings stored with the session (atomics: hosts may save from any thread)
struct UiSettings
{
    std::atomic<bool>  advanced { false };
    std::atomic<int>   band { 0 };
    std::atomic<int>   tab { 0 };
    std::atomic<int>   modSource { 1 };
    std::atomic<float> scale { 0.85f };
};

class KlingeProcessor : public juce::AudioProcessor,
                       #if KLINGE_WITH_CLAP
                        public clap_juce_extensions::clap_properties,
                       #endif
                        private juce::Timer
{
public:
    KlingeProcessor();
    ~KlingeProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "KLINGE"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.05; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- sound state (parameters + modulation + shaper + capture), used by presets, snapshots and undo
    juce::ValueTree captureSound() const;
    void restoreSound (const juce::ValueTree& sound, bool respectLocks);
    bool loadPreset (const klinge::PresetEntry& e);
    bool loadPresetByName (const juce::String& name);

    // ---- used by the editor
    juce::AudioProcessorValueTreeState apvts;
    klinge::PresetManager presets;
    klinge::ModMatrix modMatrix;
    klinge::ShaperCurve shaper;
    klinge::CaptureStage capture;
    klinge::Session session { *this };
    UiSettings ui;
    klinge::ParamIndex ix;

    klinge::EngineMeters& getMeters() noexcept { return engine.meters; }
    klinge::Engine& getEngine() noexcept { return engine; }
    const klinge::ModEngine& getModEngine() const noexcept { return modEngine; }
    int expectedLatency() const;
    int paramIndexOf (const juce::String& id) const;
    void readBaseValues (std::vector<float>& v) const;     // current (unmodulated) parameter values
    // modulated value of a parameter (normalised) or -1 when it is not modulated
    float modulatedValue (int index) const noexcept { return index >= 0 && index < numParams ? uiMod[(size_t) index].load (std::memory_order_relaxed) : -1.0f; }
    bool isModulatable (int index) const noexcept { return index >= 0 && index < numParams && runtime[(size_t) index].continuous; }

private:
    void timerCallback() override;
    void run (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, bool bypassed) noexcept;
    void computeEffective (int n, bool snap) noexcept;

    klinge::Engine engine;
    klinge::ModEngine modEngine;
    klinge::EngineParams params;
    int currentProgram = 0;
    double sampleRate = 48000.0;

    struct Runtime
    {
        juce::RangedAudioParameter* param = nullptr;
        std::atomic<float>* raw = nullptr;
        bool continuous = false, settled = true;
        float lastRaw = -1.0e30f;          // not NaN: same() treats NaN as equal to everything
        float target = 0.0f, base = 0.0f;
    };
    int numParams = 0;
    std::vector<Runtime> runtime;
    std::vector<float> eff, modSum;
    std::vector<int> touched;
    std::unique_ptr<std::atomic<float>[]> uiMod;
    std::map<juce::String, int> idToIndex;
    std::atomic<bool> snapNext { true };

    struct MidiEvent { int pos; int type; float value; };   // type 0 = note on, 1 = mod wheel
    std::array<MidiEvent, 256> midiEvents {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KlingeProcessor)
};
