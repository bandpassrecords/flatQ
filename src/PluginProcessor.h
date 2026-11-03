#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "EqDesign.h"

// Data for the editor's response graph (computed on the design thread).
struct GraphData
{
    std::vector<float> freqs, ceiling;
    std::array<std::vector<float>, 2> target, actual;   // dB, preamp included: designed EQ curve / achieved filter response
    std::array<std::vector<float>, 2> measured;         // dB, no preamp: inverse of the raw profile = the response the profile measured
    bool hasCeiling = false, perChannel = false;
    std::array<std::array<float, 31>, 2> bandBase {};   // target at flatq::manualFreqs() per channel, before manual offsets and preamp
    double preampDb = 0, maxErrDb = 0;
    juce::String name, kind;   // kind: "headphone" or "speaker"
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
    static juce::String builtInProfileKind (int index);       // file extension in caps
    // AutoEq import: measurement CSV + one of the bundled (or any) target CSVs -> saved .flatq in importedProfilesDir(), loaded.
    static int numTargets();
    static juce::String targetName (int index);
    static juce::String targetCsvText (int index);
    static juce::File appDataDir();           // per-user settings folder (Application Support/FlatQ on macOS)
    // Measurement from the embedded AutoEq pack -> profile in the library (target: index into targetName(), -1 = Harman by form)
    bool importMeasurement (int measurementIndex, int targetIndex, juce::String& error);
    static int defaultTargetFor (int measurementIndex);
    static juce::File importedProfilesDir();  // same folder as the user library
    // --- user profile library (Application Support/FlatQ/Profiles): list, import, duplicate, rename, delete
    static juce::File userProfilesDir();
    static juce::String profileKind (const juce::File& file);   // "headphone" or "speaker"
    static juce::Array<juce::File> userProfileFiles();
    juce::String getProfilePath() const { juce::SpinLock::ScopedLockType sl (profileLock); return profilePath; }
    bool importProfileFile (const juce::File& file, juce::String& error);                      // converts to .flatq in the library and loads it
    bool addBuiltInToLibrary (int builtIn, juce::String& error, bool applyHints = true);      // copies an embedded profile into the library and loads it
    bool duplicateProfile (int builtIn, const juce::File& file, juce::String& error);          // builtIn >= 0 or a file
    bool saveEqualizationAs (const juce::String& name, juce::String& error);                   // the current equalization (limits, smoothing and manual changes baked in) as a new profile
    bool exportProfile (const juce::File& source, juce::File dest, juce::String& error);      // writes a library profile to a .flatq file anywhere
    bool renameProfile (const juce::File& file, const juce::String& newName, juce::String& error);
    bool deleteProfile (const juce::File& file, juce::String& error);
    static juce::File lastProfileFile();
    // Small persisted app settings (Application Support/FlatQ/FlatQ.settings), shared by all instances
    static bool getSetting (const juce::String& key, bool defaultValue);
    static void setSetting (const juce::String& key, bool value);      // remembers the last profile used, for new instances
    bool importAutoEq (const juce::File& measurement, const juce::String& targetCsv, const juce::String& targetLabel, juce::String& error);
    // Manual per-frequency trim (advanced): offsets in dB per flatq::manualFreqs() band, per channel, on top of the target.
    // While linked, an edit applies to both channels.
    static int numManualBands() { return (int) flatq::manualFreqs().size(); }
    float getManualBand (int ch, int i) const { return manualEq[(size_t) ch][(size_t) i].load(); }
    void setManualBand (int ch, int i, float db) { manualEq[(size_t) ch][(size_t) i] = db; if (manualLinked) manualEq[1 - (size_t) ch][(size_t) i] = db; requestRedesign(); }
    bool isManualLinked() const { return manualLinked.load(); }
    void setManualLinked (bool l) { manualLinked = l; if (l) for (size_t i = 0; i < 31; ++i) manualEq[1][i] = manualEq[0][i].load(); requestRedesign(); }
    void resetManual() { for (auto& c : manualEq) for (auto& b : c) b = 0.f; requestRedesign(); }
    int manualEditedCount() const { int n = 0; for (size_t i = 0; i < 31; ++i) n += std::abs (manualEq[0][i].load()) > 0.01f || std::abs (manualEq[1][i].load()) > 0.01f; return n; }
    int getBuiltInIndex() const { return builtInIndex.load(); }  // -1 when a custom file is loaded
    juce::String describeProfile() const;
    GraphData getGraph() const { juce::SpinLock::ScopedLockType sl (graphLock); return graph; }
    int getGraphVersion() const { return graphVersion.load(); }
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<bool> inClipped { false }, outClipped { false };   // sticky: set when a peak reaches 0 dBFS, cleared by the user (click on the clip light)
    std::array<std::atomic<float>, 2> inPeak { { 0.f, 0.f } }, outPeak { { 0.f, 0.f } };   // linear peak per channel, for the meters
    double getSampleRate_() const { return sampleRateAtomic.load(); }

private:
    enum Mode { LowLatency = 0, LinearPhase = 1, MinPhase = 2 };
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String&, float) override;
    float outputTrimDb() const;
    flatq::DesignOptions baseOptions() const;   // the design options from the parameters (manual offsets excluded)
    void applyMute (juce::AudioBuffer<float>&);
    juce::SmoothedValue<float> muteGain, mixGain;
    juce::AudioBuffer<float> dryCopy;

    class DesignThread;   // design runs on a background thread (the IIR fit is not cheap)
    std::unique_ptr<DesignThread> designThread;
    void requestRedesign();
    void redesign();

    std::shared_ptr<const flatq::Profile> profile;
    mutable juce::SpinLock profileLock;
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

    std::array<std::array<std::atomic<float>, 31>, 2> manualEq;   // [channel][band]; 31 == flatq::manualFreqs().size()
    std::atomic<bool> manualLinked { true };
    juce::SmoothedValue<float> outGain;
    std::atomic<double> sampleRateAtomic { 48000.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FlatQProcessor)
};
