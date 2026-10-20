#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <memory>
#include <vector>

// MicVST's built-in effects (EQ, cleaning, colour, level, time/channel tools, meters). Each one is a
// separate chain element like a VST3 plug-in: it is listed in the picker under "Dahili - ...",
// can be moved, bypassed and removed, and keeps its settings in config.xml. None of them is put
// into the chain by itself. (No noise gate and no compressor - by the user's choice.)
//
// Common rules (BuiltInEffect below):
//  * Parameters are JUCE parameters (float / choice / bool) with fixed ASCII ids and Turkish names;
//    the settings window (double-click on the row) edits them. Their values reach the plug-in
//    thread through atomics; the effects glide every change (filters in small coefficient steps,
//    gains and mixes sample by sample, switches as short cross-fades), so no change clicks.
//  * State: <MicVSTEffect id="builtin:..." version="1"><param id="..." value="..."/>...</MicVSTEffect>.
//    Unknown parameters are ignored, missing ones keep their value - damaged or older data never
//    breaks the chain.
//  * Channels: most effects are channel-neutral (they take the width of the element before them,
//    like "Kazanç"); the stereo effects (reverb, echo, chorus/widener, channel tool) always have two
//    channels - a mono part before them feeds both inputs. PluginChain tells every effect how many
//    channels it really gets (setActiveChannels), so a mono chain costs half and the meters measure
//    what is really there.
//  * Broken input (NaN, infinite, beyond +24 dBFS - the engine silences such blocks) is passed on
//    untouched and never enters an effect's filters or delay lines; an effect whose output were
//    ever invalid resets itself.
//  * Latency (limiter look-ahead, sync delay, oversampling) is reported with setLatencySamples; the
//    engine adds it to the shown latency and to the silence around chain edits.
//  * Realtime: no allocation, no locks on the plug-in thread; buffers are sized in prepareToPlay.
class BuiltInEffect : public juce::AudioProcessor,
                      private juce::AudioProcessorParameter::Listener
{
public:
    struct Info
    {
        const char* id;            // "builtin:lowcut" (stored in config.xml - never change)
        const char* name;          // Turkish display name (UTF-8)
        const char* group;         // picker group, e.g. "Dahili - Ton ve EQ" (UTF-8)
        const char* description;   // one or two sentences for the settings window (UTF-8)
        bool stereo;               // true: always two channels (see above)
        std::unique_ptr<BuiltInEffect> (*create) (const Info&);
    };

    explicit BuiltInEffect (const Info& info);
    ~BuiltInEffect() override;

    const Info& getInfo() const noexcept { return info; }
    const juce::String getName() const override;

    // PluginChain, after wiring: how many channels this element really receives (1 or 2).
    void setActiveChannels (int n) noexcept { activeChannels.store (juce::jlimit (1, 2, n), std::memory_order_relaxed); }
    int  getActiveChannels() const noexcept { return activeChannels.load (std::memory_order_relaxed); }

    void prepareToPlay (double sampleRate, int maximumBlockSize) final;
    void releaseResources() override {}
    void reset() final;   // PluginChain calls it while the node is suspended
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) final;
    using juce::AudioProcessor::processBlock;

    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    // Extra live display shown above the controls (the meters' readings); nullptr = none.
    virtual std::unique_ptr<juce::Component> createDisplay() { return nullptr; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    std::unique_ptr<juce::XmlElement> createStateXml() const;
    bool applyStateXml (const juce::XmlElement&);   // false = not this effect's data (nothing changed)
    void resetToDefaults();

    // Parameter access by id (tests, presets). Values are real values (Hz, dB, %, index, 0/1).
    juce::RangedAudioParameter* findParameter (const juce::String& paramId) const;
    float getParam (const juce::String& paramId) const;
    void  setParam (const juce::String& paramId, float value);   // message thread (notifies the editor)
    const juce::Array<juce::RangedAudioParameter*>& getEffectParameters() const noexcept { return params; }

    // Blocks with invalid input passed through untouched / invalid outputs repaired (diagnostics, tests).
    uint64_t getRejectedInputBlocks() const noexcept  { return rejectedInput.load (std::memory_order_relaxed); }
    uint64_t getRepairedOutputBlocks() const noexcept { return repairedOutput.load (std::memory_order_relaxed); }

    enum class Unit { plain, count, hertz, decibel, dbAmount, percent, milliseconds, seconds, dbPerOctave };

protected:
    // --- for the effects -------------------------------------------------------------------------
    juce::AudioParameterFloat*  addFloat (const char* paramId, const char* name, float min, float max, float def,
                                          Unit unit, float centre = 0.0f, float step = 0.0f);
    juce::AudioParameterChoice* addChoice (const char* paramId, const char* name, const juce::StringArray& choices, int def);
    juce::AudioParameterBool*   addBool (const char* paramId, const char* name, bool def);

    double getRate() const noexcept { return rate; }
    int    getMaxBlock() const noexcept { return maxBlock; }

    // Called with the plug-in thread stopped (prepareToPlay): size buffers, set glide times, snap to
    // the current parameter values.
    virtual void prepareEffect() = 0;
    // Clear filter states / delay lines (never allocates; may run on the plug-in thread).
    virtual void resetEffect() = 0;
    // Process numChannels (1 or 2) channels in place.
    virtual void processEffect (float* const* channels, int numChannels, int numSamples) noexcept = 0;
    // Latency in samples for the current parameters at the current rate.
    virtual int computeLatency() const { return 0; }

    // Recomputes and reports the latency (message thread; also called after parameter changes).
    void updateLatency();

private:
    void parameterValueChanged (int, float) override;
    void parameterGestureChanged (int, bool) override {}

    const Info& info;
    juce::Array<juce::RangedAudioParameter*> params;
    double rate = 48000.0;
    int    maxBlock = 512;
    bool   prepared = false;
    std::atomic<int> activeChannels { 2 };
    int    lastActive = -1;                         // plug-in thread
    std::atomic<uint64_t> rejectedInput { 0 }, repairedOutput { 0 };
    bool   pendingReset = false;                    // plug-in thread

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BuiltInEffect)
};

namespace builtin
{
    // Every built-in effect, in the order of the list in docs (Ton ve EQ, Temizlik, Renk ve ortam,
    // Seviye ve koruma, Zaman ve kanal, Ölçüm).
    const std::vector<BuiltInEffect::Info>& effects();
    const BuiltInEffect::Info* find (const juce::String& id);
    bool isEffectId (const juce::String& id);
    std::unique_ptr<BuiltInEffect> create (const juce::String& id);   // nullptr = unknown id

    // Turkish number texts used by the parameters ("75 Hz", "4,0 kHz", "+2,5 dB", "%30", "250 ms").
    juce::String formatValue (float value, BuiltInEffect::Unit unit);
    float parseValue (const juce::String& text, BuiltInEffect::Unit unit);
}
