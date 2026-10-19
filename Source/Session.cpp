#include "Session.h"
#include "PluginProcessor.h"

namespace klinge
{
Session::Session (KlingeProcessor& p) : proc (p)
{
    proc.addListener (this);
}

Session::~Session()
{
    stopTimer();
    proc.removeListener (this);
}

void Session::setEditorOpen (bool open)
{
    if (open) { resync(); startTimer (250); }
    else stopTimer();
}

juce::String Session::currentSound() const
{
    return proc.captureSound().toXmlString (juce::XmlElement::TextFormat().singleLine());
}

void Session::restore (const juce::String& xml)
{
    if (auto x = juce::XmlDocument::parse (xml))
        proc.restoreSound (juce::ValueTree::fromXml (*x), false);
}

void Session::resync()
{
    userEdit = false;
    committed = lastSeen = currentSound();
    lastChangeMs = juce::Time::getMillisecondCounter();
}

void Session::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounter();
    const auto s = currentSound();
    if (committed.isEmpty()) { committed = lastSeen = s; return; }
    if (s != lastSeen)
    {
        lastSeen = s;
        lastChangeMs = now;
        return;
    }
    if (s != committed && now - lastChangeMs > 400)
    {
        if (userEdit.exchange (false))
        {
            undoStack.add (committed);
            if (undoStack.size() > 64) undoStack.remove (0);
            redoStack.clear();
            if (onChange) onChange();
        }
        committed = s;          // host automation only moves the baseline, it never becomes an undo step
    }
}

void Session::undo()
{
    if (undoStack.isEmpty()) return;
    const auto s = currentSound();
    redoStack.add (s != committed ? s : committed);
    const auto target = undoStack[undoStack.size() - 1];
    undoStack.remove (undoStack.size() - 1);
    restore (target);
    resync();
    if (onChange) onChange();
}

void Session::redo()
{
    if (redoStack.isEmpty()) return;
    undoStack.add (currentSound());
    const auto target = redoStack[redoStack.size() - 1];
    redoStack.remove (redoStack.size() - 1);
    restore (target);
    resync();
    if (onChange) onChange();
}

// ---------------------------------------------------------------------------
void Session::selectSnapshot (int index)
{
    if (index < 0 || index >= numSnapshots || index == active.load()) return;
    const auto current = currentSound();
    juce::String target;
    {
        const juce::SpinLock::ScopedLockType l (lock);
        snapshots[(size_t) active.load()] = current;
        if (snapshots[(size_t) index].isEmpty()) snapshots[(size_t) index] = current;
        target = snapshots[(size_t) index];
    }
    active = index;
    restore (target);
    if (onChange) onChange();
}

void Session::copySnapshot (int from, int to)
{
    if (from < 0 || to < 0 || from >= numSnapshots || to >= numSnapshots || from == to) return;
    const auto current = currentSound();
    juce::String src;
    {
        const juce::SpinLock::ScopedLockType l (lock);
        snapshots[(size_t) active.load()] = current;
        src = snapshots[(size_t) from];
        snapshots[(size_t) to] = src;
    }
    if (to == active.load()) restore (src);
    if (onChange) onChange();
}

bool Session::snapshotUsed (int index) const
{
    const juce::SpinLock::ScopedLockType l (lock);
    return index == active.load() || snapshots[(size_t) index].isNotEmpty();
}

juce::ValueTree Session::snapshotsTree() const
{
    juce::ValueTree t ("SNAPSHOTS");
    t.setProperty ("active", active.load(), nullptr);
    const juce::SpinLock::ScopedLockType l (lock);
    for (int i = 0; i < numSnapshots; ++i)
        if (snapshots[(size_t) i].isNotEmpty() && i != active.load())
        {
            juce::ValueTree s ("SNAP");
            s.setProperty ("i", i, nullptr);
            s.setProperty ("xml", snapshots[(size_t) i], nullptr);
            t.appendChild (s, nullptr);
        }
    return t;
}

void Session::restoreSnapshots (const juce::ValueTree& t)
{
    const juce::SpinLock::ScopedLockType l (lock);
    for (auto& s : snapshots) s.clear();
    active = juce::jlimit (0, numSnapshots - 1, (int) t.getProperty ("active", 0));
    for (auto c : t)
    {
        const int i = c.getProperty ("i", -1);
        if (i >= 0 && i < numSnapshots) snapshots[(size_t) i] = c.getProperty ("xml").toString();
    }
}

// ---------------------------------------------------------------------------
bool Session::isLocked (const juce::String& id) const
{
    const juce::SpinLock::ScopedLockType l (lock);
    return locked.contains (id);
}

void Session::setLocked (const juce::String& id, bool lk)
{
    {
        const juce::SpinLock::ScopedLockType l (lock);
        if (lk) locked.addIfNotAlreadyThere (id); else locked.removeString (id);
    }
    if (onChange) onChange();
}

juce::StringArray Session::getLocked() const
{
    const juce::SpinLock::ScopedLockType l (lock);
    return locked;
}

void Session::setLockedList (const juce::StringArray& ids)
{
    const juce::SpinLock::ScopedLockType l (lock);
    locked = ids;
}

// ---------------------------------------------------------------------------
void Session::randomise()
{
    // never randomised: level, quality and switching parameters
    static const juce::StringArray fixedIds { "in_gain", "out_gain", "mix", "oversampling", "os_quality", "render_os",
                                              "auto_gain", "delta", "clip_out", "cap_on", "cap_pos", "cap_in",
                                              "cap_out", "cap_mix", "cap_stereo", "shp_retrig", "lfo1_retrig", "lfo2_retrig",
                                              "env1_src", "env2_src", "fb_amount", "num_bands", "routing" };
    auto setReal = [this] (const juce::String& id, float v)
    {
        if (isLocked (id)) return;
        if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
    };
    auto rnd = [this] (float lo, float hi) { return lo + (hi - lo) * rng.nextFloat(); };

    for (auto* p : proc.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (rp == nullptr) continue;
        const auto id = rp->getParameterID();
        if (fixedIds.contains (id) || isLocked (id)) continue;
        if (id.endsWith ("_solo") || id.endsWith ("_mute") || id.endsWith ("_on")) continue;
        if (id.startsWith ("lfo") || id.startsWith ("env") || id.startsWith ("shp") || id.startsWith ("xover")) continue;
        if (id.endsWith ("_type"))      setReal (id, (float) rng.nextInt (16));
        else if (id == "drive")         setReal (id, rnd (6.0f, 36.0f));
        else if (id.endsWith ("_drive")) setReal (id, rnd (-6.0f, 8.0f));
        else if (id.endsWith ("_shape")) setReal (id, rnd (0.0f, 100.0f));
        else if (id.endsWith ("_bias"))  setReal (id, rng.nextFloat() < 0.6f ? 0.0f : rnd (-50.0f, 50.0f));
        else if (id.endsWith ("_sag"))   setReal (id, rng.nextFloat() < 0.6f ? 0.0f : rnd (0.0f, 60.0f));
        else if (id.endsWith ("_dyn"))   setReal (id, rng.nextFloat() < 0.7f ? 0.0f : rnd (-60.0f, 60.0f));
        else if (id.endsWith ("_mix"))   setReal (id, rnd (50.0f, 100.0f));
        else if (id.endsWith ("_level")) setReal (id, rnd (-4.0f, 2.0f));
        else if (id.endsWith ("_harm"))  setReal (id, rnd (0.0f, 100.0f));
        else if (id.endsWith ("_stereo") || id.endsWith ("_thresh") || id.endsWith ("_dyn_pos")) continue;
        else if (id.startsWith ("h") && id.length() == 2) setReal (id, rng.nextFloat() < 0.6f ? 0.0f : rnd (-20.0f, 30.0f));
        else if (id == "focus")         setReal (id, rnd (-6.0f, 6.0f));
        else if (id == "dyn_drive")     setReal (id, rnd (-60.0f, 60.0f));
        else if (id == "punch")         setReal (id, rnd (0.0f, 100.0f));
        else if (id == "tone_lock")     setReal (id, rnd (0.0f, 60.0f));
        else if (id == "analog")        setReal (id, rng.nextFloat() < 0.5f ? 0.0f : rnd (0.0f, 60.0f));
        else if (id == "pre_hp")        setReal (id, rng.nextFloat() < 0.5f ? 20.0f : rnd (40.0f, 250.0f));
        else if (id == "post_lp")       setReal (id, rng.nextFloat() < 0.4f ? 20000.0f : rnd (6000.0f, 16000.0f));
    }
}

} // namespace klinge
