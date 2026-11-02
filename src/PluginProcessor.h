#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "EqDesign.h"

// Data for the editor's response graph (computed on the design thread).
struct GraphData
{
    std::vector<float> freqs, ceiling;
    std::array<std::vector<float>, 2> target, actual;   // dB, preamp included
    bool hasCeiling = false, perChannel = false;
    double preampDb = 0, maxErrDb = 0;
    juce::String name;
};

class FlatQProcessor : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener
{
public:
    FlatQProcessor();
    ~FlatQProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "FlatQ"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    bool loadProfile (const juce::File&, juce::String& error, bool applyHints = true);
    static int numBuiltInProfiles();
    static juce::String builtInProfileName (int index);       // display name (file name without extension)
    bool loadBuiltInProfile (int index, juce::String& error, bool applyHints = true);
    int getBuiltInIndex() const { return builtInIndex.load(); }  // -1 when a custom file is loaded
    juce::String describeProfile() const;
    GraphData getGraph() const { juce::SpinLock::ScopedLockType sl (graphLock); return graph; }
    int getGraphVersion() const { return graphVersion.load(); }
    juce::AudioProcessorValueTreeState apvts;

private:
    enum Mode { LowLatency = 0, LinearPhase = 1 };
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String&, float) override;

    class DesignThread;   // design runs on a background thread (the IIR fit is not cheap)
    std::unique_ptr<DesignThread> designThread;
    void requestRedesign();
    void redesign();

    std::shared_ptr<const flatq::Profile> profile;
    juce::SpinLock profileLock;
    juce::String profilePath;                 // custom file, empty for built-ins
    std::atomic<int> builtInIndex { -1 };
    bool setProfile (std::shared_ptr<const flatq::Profile>, const juce::String& path, int builtIn, bool applyHints);

    // IIR state (guarded by iirLock; audio thread only try-locks)
    juce::SpinLock iirLock;
    std::array<std::vector<flatq::Biquad>, 2> sections;
    std::array<std::vector<double>, 2> z1, z2;
    std::array<std::vector<float>, 2> delayBuf;
    std::array<int, 2> delayPos {};
    bool iirReady = false;

    juce::dsp::Convolution convolution;
    std::atomic<bool> firReady { false };

    mutable juce::SpinLock graphLock;
    GraphData graph;
    std::atomic<int> graphVersion { 0 };

    // Dry path used when bypassed (manual bypass or offline export): delayed by the reported latency so the
    // host's latency compensation stays correct.
    static constexpr int kMaxLatency = 8192;
    std::array<std::vector<float>, 2> dryBuf;
    std::array<int, 2> dryPos {};
    std::atomic<int> latencyNow { 0 };
    void processDry (juce::AudioBuffer<float>&);

    juce::SmoothedValue<float> outGain;
    std::atomic<double> sampleRateAtomic { 48000.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FlatQProcessor)
};
