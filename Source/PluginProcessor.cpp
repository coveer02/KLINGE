#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace klinge;

// ===========================================================================
KlingeProcessor::KlingeProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "KLINGE", createParameterLayout())
{
    const auto& ps = getParameters();
    numParams = ps.size();
    ix.build (ps);
    runtime.resize ((size_t) numParams);
    eff.assign ((size_t) numParams, 0.0f);
    modSum.assign ((size_t) numParams, 0.0f);
    touched.reserve ((size_t) numModSlots);
    uiMod = std::make_unique<std::atomic<float>[]> ((size_t) numParams);
    for (int i = 0; i < numParams; ++i)
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (ps[i]);
        auto& r = runtime[(size_t) i];
        r.param = rp;
        r.raw = apvts.getRawParameterValue (rp->getParameterID());
        r.continuous = dynamic_cast<juce::AudioParameterFloat*> (rp) != nullptr;
        eff[(size_t) i] = r.raw->load();
        uiMod[(size_t) i].store (-1.0f);
        idToIndex[rp->getParameterID()] = i;
    }

    modMatrix.resolve = [this] (const juce::String& id)
    {
        const int i = paramIndexOf (id);
        return i >= 0 && runtime[(size_t) i].continuous ? i : -1;
    };
    modEngine.curve = &shaper;
    engine.setCapture (&capture);

    setLatencySamples (expectedLatency());
    startTimerHz (20);
}

KlingeProcessor::~KlingeProcessor()
{
    stopTimer();
}

int KlingeProcessor::paramIndexOf (const juce::String& id) const
{
    auto it = idToIndex.find (id);
    return it == idToIndex.end() ? -1 : it->second;
}

void KlingeProcessor::readBaseValues (std::vector<float>& v) const
{
    v.resize ((size_t) numParams);
    for (int i = 0; i < numParams; ++i) v[(size_t) i] = runtime[(size_t) i].raw->load (std::memory_order_relaxed);
}

int KlingeProcessor::expectedLatency() const
{
    return engine.latencyFor ((int) runtime[(size_t) ix.oversample].raw->load(),
                              (int) runtime[(size_t) ix.osQuality].raw->load(),
                              (int) runtime[(size_t) ix.renderOs].raw->load());
}

void KlingeProcessor::timerCallback()
{
    // latency follows the oversampling settings; reported from the message thread
    const int lat = expectedLatency();
    if (lat != getLatencySamples())
        setLatencySamples (lat);
}

void KlingeProcessor::reset()
{
    engine.reset();
    modEngine.reset();
    capture.reset();
    snapNext = true;
}

bool KlingeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainInputChannelSet() != out)
        return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.inputBuses[1];
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void KlingeProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr;
    engine.prepare (sr, samplesPerBlock, getMainBusNumOutputChannels());
    modEngine.prepare (sr);
    snapNext = true;
    setLatencySamples (expectedLatency());
}

// ---------------------------------------------------------------------------
// Parameter values for one chunk: smoothed base value (normalised, ~30 ms) plus modulation
// ---------------------------------------------------------------------------
void KlingeProcessor::computeEffective (int n, bool snap) noexcept
{
    for (int t : touched) modSum[(size_t) t] = 0.0f;
    touched.clear();
    for (auto& s : modMatrix.live)
    {
        const int src = s.source.load (std::memory_order_relaxed);
        const int tgt = s.target.load (std::memory_order_relaxed);
        if (src <= 0 || src >= numModSources || tgt < 0 || tgt >= numParams) continue;
        const float v = modEngine.value ((ModSource) src);
        if (std::find (touched.begin(), touched.end(), tgt) == touched.end()) touched.push_back (tgt);
        modSum[(size_t) tgt] += s.depth.load (std::memory_order_relaxed) * v;
    }

    const float k = (float) (1.0 - std::exp (-(double) n / (0.03 * sampleRate)));
    for (int i = 0; i < numParams; ++i)
    {
        auto& r = runtime[(size_t) i];
        const float raw = r.raw->load (std::memory_order_relaxed);
        if (! r.continuous) { eff[(size_t) i] = raw; continue; }
        if (! same (raw, r.lastRaw))
        {
            r.lastRaw = raw;
            r.target = r.param->convertTo0to1 (raw);
            r.settled = false;
        }
        if (snap) { r.base = r.target; r.settled = true; }
        else if (! r.settled)
        {
            r.base += k * (r.target - r.base);
            if (std::abs (r.target - r.base) < 1.0e-5f) { r.base = r.target; r.settled = true; }
        }
        const float m = modSum[(size_t) i];
        if (r.settled && same (m, 0.0f))
            eff[(size_t) i] = raw;
        else
            eff[(size_t) i] = r.param->convertFrom0to1 (clampf (r.base + m, 0.0f, 1.0f));
    }
}

void KlingeProcessor::run (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, bool bypassed) noexcept
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    auto mainBuf = getBusBuffer (buffer, true, 0);
    for (int c = getMainBusNumInputChannels(); c < getMainBusNumOutputChannels(); ++c)
        mainBuf.clear (c, 0, total);
    const int nc = std::min (2, mainBuf.getNumChannels());

    const float* scPtr[2] = { nullptr, nullptr };
    int scCh = 0;
    if (getBusCount (true) > 1)
        if (auto* bus = getBus (true, 1); bus != nullptr && bus->isEnabled())
        {
            auto sc = getBusBuffer (buffer, true, 1);
            scCh = std::min (2, sc.getNumChannels());
            for (int c = 0; c < scCh; ++c) scPtr[c] = sc.getReadPointer (c);
        }

    Transport tr;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            tr.bpm = pos->getBpm().orFallback (120.0);
            tr.ppq = pos->getPpqPosition().orFallback (0.0);
            tr.playing = pos->getIsPlaying() && pos->getPpqPosition().hasValue();
        }

    int numEvents = 0;
    for (const auto meta : midi)
    {
        if (numEvents >= (int) midiEvents.size()) break;
        const auto m = meta.getMessage();
        if (m.isNoteOn()) midiEvents[(size_t) numEvents++] = { meta.samplePosition, 0, m.getFloatVelocity() };
        else if (m.isController() && m.getControllerNumber() == 1)
            midiEvents[(size_t) numEvents++] = { meta.samplePosition, 1, (float) m.getControllerValue() / 127.0f };
    }
    int ev = 0;

    const bool offline = isNonRealtime();
    const double latency = engine.getLatencySamples();
    engine.beginBlock();

    for (int start = 0; start < total; start += Engine::kChunk)
    {
        const int n = std::min (Engine::kChunk, total - start);
        while (ev < numEvents && midiEvents[(size_t) ev].pos < start + n)
        {
            const auto& e = midiEvents[(size_t) ev++];
            if (e.type == 0) modEngine.noteOn (e.value); else modEngine.modWheel (e.value);
        }

        const float* in[2] = { mainBuf.getReadPointer (0, start), mainBuf.getReadPointer (nc > 1 ? 1 : 0, start) };
        const float* sc[2] = { scPtr[0] != nullptr ? scPtr[0] + start : nullptr, scPtr[1] != nullptr ? scPtr[1] + start : nullptr };
        Transport chunkTr = tr;
        chunkTr.ppq = tr.ppq + (double) start / sampleRate * tr.bpm / 60.0;

        modEngine.processChunk (in, nc, scCh > 0 ? sc : nullptr, scCh, n, chunkTr, latency, eff.data(), ix);
        computeEffective (n, snapNext.exchange (false));

        buildEngineParams (eff.data(), ix, params);
        params.bypass = bypassed;
        params.nonRealtime = offline;

        float* io[2] = { mainBuf.getWritePointer (0, start), mainBuf.getWritePointer (nc > 1 ? 1 : 0, start) };
        engine.processChunk (io, nc, n, params, modEngine.shaper());
    }
    engine.endBlock();

    // modulated positions for the knob rings
    for (int i = 0; i < numParams; ++i) uiMod[(size_t) i].store (-1.0f, std::memory_order_relaxed);
    for (int t : touched)
        uiMod[(size_t) t].store (clampf (runtime[(size_t) t].base + modSum[(size_t) t], 0.0f, 1.0f), std::memory_order_relaxed);
}

void KlingeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    run (buffer, midi, false);
}

// Host bypass: the engine keeps running with neutral settings, so latency stays constant and
// switching crossfades instead of clicking.
void KlingeProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    run (buffer, midi, true);
}

juce::AudioProcessorEditor* KlingeProcessor::createEditor()
{
    return new KlingeEditor (*this);
}

// ===========================================================================
// Sound state
// ===========================================================================
juce::ValueTree KlingeProcessor::captureSound() const
{
    juce::ValueTree t ("KLINGESOUND");
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            juce::ValueTree pt ("PARAM");
            pt.setProperty ("id", rp->getParameterID(), nullptr);
            pt.setProperty ("value", rp->convertFrom0to1 (rp->getValue()), nullptr);
            t.appendChild (pt, nullptr);
        }
    t.appendChild (modMatrix.toTree(), nullptr);
    t.appendChild (shaper.toTree(), nullptr);
    juce::ValueTree cap ("CAPTURE");
    cap.setProperty ("path", capture.getModelPath(), nullptr);
    t.appendChild (cap, nullptr);
    return t;
}

void KlingeProcessor::restoreSound (const juce::ValueTree& sound, bool respectLocks)
{
    if (! sound.isValid()) return;
    std::map<juce::String, float> values;
    for (auto c : sound)
        if (c.hasType ("PARAM")) values[c.getProperty ("id").toString()] = (float) c.getProperty ("value", 0.0);

    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto id = rp->getParameterID();
            if (respectLocks && session.isLocked (id)) continue;
            auto it = values.find (id);
            const float norm = it != values.end() ? rp->convertTo0to1 (it->second) : rp->getDefaultValue();
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (norm);
            rp->endChangeGesture();
        }

    const auto mod = sound.getChildWithName ("MOD");
    if (mod.isValid()) modMatrix.fromTree (mod); else modMatrix.clear();

    const auto shp = sound.getChildWithName ("SHAPER");
    if (shp.isValid() && shp.getNumChildren() > 0) shaper.fromTree (shp);
    else
    {
        const int pi = sound.getProperty ("shaperPreset", -1);
        shaper.setPoints (ShaperCurve::preset (pi >= 0 ? pi : 1));
    }

    const auto cap = sound.getChildWithName ("CAPTURE");
    if (cap.isValid())
    {
        const juce::String path = cap.getProperty ("path").toString();
        if (path.isEmpty()) { if (capture.hasModel()) capture.clearModel(); }
        else if (path != capture.getModelPath() && juce::File (path).existsAsFile()) capture.loadAsync (juce::File (path));
    }
}

bool KlingeProcessor::loadPreset (const PresetEntry& e)
{
    const auto sound = presets.soundFor (e);
    if (! sound.isValid()) return false;
    restoreSound (sound, true);
    presets.setCurrentName (e.name);
    session.markUserEdit();
    return true;
}

bool KlingeProcessor::loadPresetByName (const juce::String& name)
{
    for (auto& e : presets.list())
        if (e.name == name) return loadPreset (e);
    return false;
}

// ---- programs = factory presets (for hosts that show a program list)
int KlingeProcessor::getNumPrograms() { return (int) factoryPresets().size(); }

void KlingeProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= getNumPrograms()) return;
    PresetEntry e;
    e.factory = true;
    e.factoryIndex = index;
    e.name = factoryPresets()[(size_t) index].name;
    if (loadPreset (e)) currentProgram = index;
}

const juce::String KlingeProcessor::getProgramName (int index)
{
    return index >= 0 && index < getNumPrograms() ? juce::String (factoryPresets()[(size_t) index].name) : juce::String();
}

// ===========================================================================
// Session state
// ===========================================================================
void KlingeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    for (auto name : { "UI", "MOD", "SHAPER", "CAPTURE", "SNAPSHOTS", "LOCKS" })
        state.removeChild (state.getChildWithName (name), nullptr);

    juce::ValueTree uiTree ("UI");
    uiTree.setProperty ("advanced", ui.advanced.load(), nullptr);
    uiTree.setProperty ("band", ui.band.load(), nullptr);
    uiTree.setProperty ("tab", ui.tab.load(), nullptr);
    uiTree.setProperty ("modSource", ui.modSource.load(), nullptr);
    uiTree.setProperty ("scale", (double) ui.scale.load(), nullptr);
    uiTree.setProperty ("preset", presets.getCurrentName(), nullptr);
    state.appendChild (uiTree, nullptr);
    state.appendChild (modMatrix.toTree(), nullptr);
    state.appendChild (shaper.toTree(), nullptr);
    juce::ValueTree cap ("CAPTURE");
    cap.setProperty ("path", capture.getModelPath(), nullptr);
    state.appendChild (cap, nullptr);
    state.appendChild (session.snapshotsTree(), nullptr);
    juce::ValueTree locks ("LOCKS");
    locks.setProperty ("ids", session.getLocked().joinIntoString (","), nullptr);
    state.appendChild (locks, nullptr);
    state.setProperty ("klingeVersion", JucePlugin_VersionString, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void KlingeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr) return;
    auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.hasType (apvts.state.getType())) return;

    auto take = [&tree] (const char* name)
    {
        auto c = tree.getChildWithName (name);
        if (c.isValid()) tree.removeChild (c, nullptr);
        return c;
    };
    const auto uiTree = take ("UI");
    const auto mod = take ("MOD");
    const auto shp = take ("SHAPER");
    const auto cap = take ("CAPTURE");
    const auto snaps = take ("SNAPSHOTS");
    const auto locks = take ("LOCKS");

    if (uiTree.isValid())
    {
        ui.advanced.store ((bool) uiTree.getProperty ("advanced", false));
        ui.band.store (juce::jlimit (0, numBands - 1, (int) uiTree.getProperty ("band", 0)));
        ui.tab.store (juce::jlimit (0, 4, (int) uiTree.getProperty ("tab", 0)));
        ui.modSource.store (juce::jlimit (1, numModSources - 1, (int) uiTree.getProperty ("modSource", 1)));
        ui.scale.store ((float) juce::jlimit (0.6, 1.6, (double) uiTree.getProperty ("scale", 0.85)));
        presets.setCurrentName (uiTree.getProperty ("preset", "Init").toString());
    }

    apvts.replaceState (tree);
    // Push every stored value explicitly: replaceState skips values that equal the snapped current value.
    // Parameters missing from older sessions fall back to their defaults.
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto child = tree.getChildWithProperty ("id", rp->getParameterID());
            const float norm = child.isValid() && child.hasProperty ("value")
                                   ? rp->convertTo0to1 ((float) child.getProperty ("value"))
                                   : rp->getDefaultValue();
            rp->setValueNotifyingHost (norm);
        }

    modMatrix.fromTree (mod);
    if (shp.isValid()) shaper.fromTree (shp); else shaper.setPoints (ShaperCurve::preset (1));
    if (cap.isValid())
    {
        const juce::String path = cap.getProperty ("path").toString();
        if (path.isNotEmpty() && juce::File (path).existsAsFile()) capture.loadAsync (juce::File (path));
    }
    if (snaps.isValid()) session.restoreSnapshots (snaps);
    if (locks.isValid())
    {
        auto ids = juce::StringArray::fromTokens (locks.getProperty ("ids").toString(), ",", "");
        ids.removeEmptyStrings();
        session.setLockedList (ids);
    }
    snapNext = true;

   #if KLINGE_WITH_CLAP
    // CLAP hosts keep their own copy of the parameter values and need to be told to read them again.
    // (VST3 and AU hosts read the values after loading a state anyway.)
    if (is_clap) updateHostDisplay (ChangeDetails().withProgramChanged (true));
   #endif
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KlingeProcessor();
}
