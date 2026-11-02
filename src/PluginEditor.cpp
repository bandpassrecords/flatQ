#include "PluginEditor.h"

void ResponseGraph::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff14161a)); g.fillRoundedRectangle (r, 6.f);
    auto plot = r.reduced (8.f).withTrimmedLeft (28.f).withTrimmedBottom (16.f);

    // y range: multiples of 6 dB around the data
    float peak = 6.f;
    for (auto& v : data.target) for (float x : v) peak = juce::jmax (peak, std::abs (x));
    for (float x : data.ceiling) peak = juce::jmax (peak, std::min (std::abs (x), 24.f));
    const float range = std::ceil (peak / 6.f) * 6.f;
    auto yOf = [&] (float db) { return plot.getBottom() - (db + range) / (2 * range) * plot.getHeight(); };
    auto xOf = [&] (float f) { return plot.getX() + std::log10 (f / 20.f) / 3.f * plot.getWidth(); };

    g.setFont (11.f);
    for (float db = -range; db <= range + 0.1f; db += 6.f)
    {
        g.setColour (db == 0 ? juce::Colour (0xff4a505a) : juce::Colour (0xff262a31));
        g.drawHorizontalLine ((int) yOf (db), plot.getX(), plot.getRight());
        g.setColour (juce::Colours::grey);
        g.drawText (juce::String ((int) db), (int) r.getX(), (int) yOf (db) - 7, 26, 14, juce::Justification::centredRight);
    }
    for (float f : { 20.f, 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 20000.f })
    {
        g.setColour (juce::Colour (0xff262a31)); g.drawVerticalLine ((int) xOf (f), plot.getY(), plot.getBottom());
        g.setColour (juce::Colours::grey);
        g.drawText (f >= 1000 ? juce::String (f / 1000.f, 0) + "k" : juce::String ((int) f),
                    (int) xOf (f) - 20, (int) plot.getBottom() + 1, 40, 14, juce::Justification::centred);
    }
    if (data.freqs.empty()) { g.setColour (juce::Colours::grey); g.drawText ("Load a profile", getLocalBounds(), juce::Justification::centred); return; }

    g.saveState(); g.reduceClipRegion (plot.toNearestInt().expanded (0, 2));
    auto path = [&] (const std::vector<float>& v) {
        juce::Path p;
        for (size_t i = 0; i < v.size(); ++i)
        {
            const float x = xOf (data.freqs[i]), y = juce::jlimit (plot.getY() - 4, plot.getBottom() + 4, yOf (v[i]));
            i == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
        }
        return p;
    };
    if (data.hasCeiling)
    {
        juce::Path p = path (data.ceiling); juce::Path dashed;
        const float dash[] = { 5.f, 4.f };
        juce::PathStrokeType (1.f).createDashedStroke (dashed, p, dash, 2);
        g.setColour (juce::Colour (0xff8a8f99)); g.fillPath (dashed);
    }
    const juce::Colour cols[2] = { juce::Colour (0xff4cc9f0), juce::Colour (0xfff4a259) };
    const int nDraw = data.perChannel ? 2 : 1;
    for (int c = 0; c < nDraw; ++c)
    {
        auto col = data.perChannel ? cols[c] : juce::Colour (0xffb8e986);
        g.setColour (col.withAlpha (0.28f)); g.strokePath (path (data.target[(size_t) c]), juce::PathStrokeType (5.f));
        if (data.actual[(size_t) c].size() == data.freqs.size())
        { g.setColour (col); g.strokePath (path (data.actual[(size_t) c]), juce::PathStrokeType (1.5f)); }
    }
    g.restoreState();
    if (data.perChannel)
    {
        g.setColour (cols[0]); g.drawText ("L", (int) plot.getRight() - 40, (int) plot.getY() + 2, 18, 14, juce::Justification::centred);
        g.setColour (cols[1]); g.drawText ("R", (int) plot.getRight() - 20, (int) plot.getY() + 2, 18, 14, juce::Justification::centred);
    }
}

FreeQEditor::FreeQEditor (FreeQProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    mode.addItemList ({ "Low latency (IIR)", "Linear phase (FIR)" }, 1);
    taps.addItemList ({ "4096", "8192", "16384" }, 1);
    stats.setColour (juce::Label::textColourId, juce::Colours::grey);
    for (auto* c : std::initializer_list<juce::Component*> { &graph, &presets, &loadButton, &info, &stats, &mode, &taps, &clamp, &autoPre, &bypass, &bypassExport, &trim, &modeL, &tapsL, &trimL })
        addAndMakeVisible (c);
    presets.setTextWhenNothingSelected ("Select profile...");
    for (int i = 0; i < FreeQProcessor::numBuiltInProfiles(); ++i) presets.addItem (FreeQProcessor::builtInProfileName (i), i + 1);
    presets.onChange = [this]
    {
        juce::String err;
        const int idx = presets.getSelectedId() - 1;
        if (idx >= 0 && ! proc.loadBuiltInProfile (idx, err))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not load profile", err);
    };
    modeA = std::make_unique<CA> (p.apvts, "mode", mode);
    tapsA = std::make_unique<CA> (p.apvts, "taps", taps);
    clampA = std::make_unique<BA> (p.apvts, "clamp", clamp);
    preA = std::make_unique<BA> (p.apvts, "autopreamp", autoPre);
    bypassA = std::make_unique<BA> (p.apvts, "bypass", bypass);
    bypassExportA = std::make_unique<BA> (p.apvts, "bypassexport", bypassExport);
    bypassExport.setTooltip ("Pass audio through untouched while the host renders offline (e.g. Cubase Export Audio Mixdown)");
    trimA = std::make_unique<SA> (p.apvts, "trim", trim);

    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Select a profile", juce::File(), "*.lvls;*.tmreq");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (! f.existsAsFile()) return;
                juce::String err;
                if (! proc.loadProfile (f, err))
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not load profile", err);
            });
    };
    setSize (640, 480);
    startTimerHz (8);
    timerCallback();
}

void FreeQEditor::timerCallback()
{
    const int v = proc.getGraphVersion();
    if (v == lastVersion) return;
    lastVersion = v;
    auto d = proc.getGraph();
    const int bi = proc.getBuiltInIndex();
    if (bi >= 0) presets.setSelectedId (bi + 1, juce::dontSendNotification);
    else { presets.setSelectedId (0, juce::dontSendNotification); presets.setTextWhenNothingSelected (d.name.isEmpty() ? "Select profile..." : "Custom: " + d.name); }
    info.setText (d.name.isEmpty() ? "" : d.name, juce::dontSendNotification);
    stats.setText (d.freqs.empty() ? "" : juce::String::formatted ("Preamp %.1f dB   |   max fit error %.1f dB%s",
                   d.preampDb, d.maxErrDb, d.hasCeiling ? "   |   dashed = max boost" : ""), juce::dontSendNotification);
    graph.setData (std::move (d));
}

void FreeQEditor::paint (juce::Graphics& g) { g.fillAll (juce::Colour (0xff1c1f24)); }

void FreeQEditor::resized()
{
    auto r = getLocalBounds().reduced (14);
    auto top = r.removeFromTop (30); presets.setBounds (top.removeFromLeft (360)); top.removeFromLeft (8); loadButton.setBounds (top.removeFromLeft (100));
    info.setBounds (r.removeFromTop (22).withTrimmedTop (2));
    r.removeFromTop (6);
    graph.setBounds (r.removeFromTop (250)); r.removeFromTop (4);
    stats.setBounds (r.removeFromTop (20)); r.removeFromTop (6);
    auto row = r.removeFromTop (26); modeL.setBounds (row.removeFromLeft (110)); mode.setBounds (row.removeFromLeft (220));
    tapsL.setBounds (row.removeFromLeft (110).withTrimmedLeft (16)); taps.setBounds (row.removeFromLeft (100)); r.removeFromTop (6);
    row = r.removeFromTop (26); trimL.setBounds (row.removeFromLeft (110)); trim.setBounds (row); r.removeFromTop (8);
    row = r.removeFromTop (26);
    clamp.setBounds (row.removeFromLeft (160)); autoPre.setBounds (row.removeFromLeft (125)); bypass.setBounds (row.removeFromLeft (80)); bypassExport.setBounds (row);
}
