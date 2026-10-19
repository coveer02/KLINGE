#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class KlingeProcessor;

namespace klinge
{
// ---------------------------------------------------------------------------
// Everything around the sound that lives on the message thread:
// undo / redo, A/B/C/D snapshots, parameter locks and randomisation.
// ---------------------------------------------------------------------------
class Session : private juce::Timer,
                private juce::AudioProcessorListener
{
public:
    explicit Session (KlingeProcessor& p);
    ~Session() override;

    // history is tracked while the editor is open; only user gestures create undo steps
    void setEditorOpen (bool open);
    void markUserEdit() noexcept { userEdit = true; }

    // undo / redo (automatic: a state that stays unchanged for a moment becomes a step)
    bool canUndo() const { return ! undoStack.isEmpty(); }
    bool canRedo() const { return ! redoStack.isEmpty(); }
    void undo();
    void redo();

    // snapshots
    static constexpr int numSnapshots = 4;
    int getActiveSnapshot() const { return active.load(); }
    void selectSnapshot (int index);
    void copySnapshot (int from, int to);
    bool snapshotUsed (int index) const;
    juce::ValueTree snapshotsTree() const;                 // thread-safe
    void restoreSnapshots (const juce::ValueTree& t);      // thread-safe

    // locks (kept when presets load or when randomising)
    bool isLocked (const juce::String& paramId) const;
    void setLocked (const juce::String& paramId, bool locked);
    juce::StringArray getLocked() const;
    void setLockedList (const juce::StringArray& ids);

    // randomise everything that is not locked, within musical ranges
    void randomise();

    // called after the sound changed programmatically, so it does not become an undo step of its own
    void resync();

    std::function<void()> onChange;                        // UI refresh hook

private:
    void timerCallback() override;
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int) override { userEdit = true; }
    std::atomic<bool> userEdit { false };
    juce::String currentSound() const;
    void restore (const juce::String& xml);

    KlingeProcessor& proc;
    juce::StringArray undoStack, redoStack;
    juce::String committed, lastSeen;
    juce::uint32 lastChangeMs = 0;

    mutable juce::SpinLock lock;                           // guards snapshots and locks
    std::array<juce::String, numSnapshots> snapshots;
    std::atomic<int> active { 0 };
    juce::StringArray locked;
    juce::Random rng;
};

} // namespace klinge
