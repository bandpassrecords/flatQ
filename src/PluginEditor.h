#pragma once
#include "PluginProcessor.h"

// Log-frequency response plot: target (thick, translucent), actual filter response (thin), boost ceiling (dashed).
class ResponseGraph : public juce::Component
{
public:
    void setData (GraphData d) { data = std::move (d); repaint(); }
    void paint (juce::Graphics&) override;
private:
    GraphData data;
};

class FlatQEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit FlatQEditor (FlatQProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override;
    FlatQProcessor& proc;
    ResponseGraph graph;
    int lastVersion = -1;
    juce::TextButton loadButton { "Load file..." };
    juce::Label info, stats;
    juce::ComboBox presets, mode, taps, smooth;
    juce::ToggleButton clamp { "Respect max boost" }, autoPre { "Auto preamp" }, bypass { "Bypass" }, bypassExport { "Skip on export" };
    juce::Slider trim { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight }, maxBoost { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight }, bassLimit { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label modeL { {}, "Mode" }, tapsL { {}, "FIR length" }, trimL { {}, "Output trim (dB)" }, maxBoostL { {}, "Max boost (dB)" }, smoothL { {}, "Smoothing" }, bassLimitL { {}, "Bass limit (Hz)" };
    std::unique_ptr<juce::FileChooser> chooser;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<CA> modeA, tapsA, smoothA; std::unique_ptr<BA> clampA, preA, bypassA, bypassExportA; std::unique_ptr<SA> trimA, maxBoostA, bassLimitA;
};
