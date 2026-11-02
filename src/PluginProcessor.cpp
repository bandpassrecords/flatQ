#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <BinaryData.h>

class FreeQProcessor::DesignThread : public juce::Thread
{
public:
    explicit DesignThread (FreeQProcessor& o) : juce::Thread ("FreeQ design"), owner (o) {}
    void run() override
    {
        while (! threadShouldExit())
        {
            if (dirty.exchange (false)) owner.redesign();
            else wait (500);
        }
    }
    void request() { dirty = true; notify(); }
private:
    FreeQProcessor& owner;
    std::atomic<bool> dirty { false };
};

juce::AudioProcessorValueTreeState::ParameterLayout FreeQProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode",
           StringArray { "Low latency (IIR)", "Linear phase (FIR)" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "taps", 1 }, "FIR length",
           StringArray { "4096", "8192", "16384" }, 2));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "clamp", 1 }, "Respect max boost", true));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "autopreamp", 1 }, "Auto preamp", true));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "bypass", 1 }, "Bypass", false));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "trim", 1 }, "Output trim",
           NormalisableRange<float> (-24.f, 12.f, 0.1f), 0.f));
    return l;
}

FreeQProcessor::FreeQProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    for (auto* id : { "mode", "taps", "clamp", "autopreamp" }) apvts.addParameterListener (id, this);
    designThread = std::make_unique<DesignThread> (*this);
    designThread->startThread();
}

FreeQProcessor::~FreeQProcessor()
{
    for (auto* id : { "mode", "taps", "clamp", "autopreamp" }) apvts.removeParameterListener (id, this);
    designThread->stopThread (5000);
}

bool FreeQProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == l.getMainInputChannelSet()
        && (l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
         || l.getMainOutputChannelSet() == juce::AudioChannelSet::mono());
}

void FreeQProcessor::parameterChanged (const juce::String&, float) { requestRedesign(); }
void FreeQProcessor::requestRedesign() { if (designThread) designThread->request(); }

void FreeQProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampleRateAtomic = sampleRate;
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };
    convolution.prepare (spec);
    outGain.reset (sampleRate, 0.02);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("trim")->load()));
    firReady = false;
    requestRedesign();
}

bool FreeQProcessor::setProfile (std::shared_ptr<const freeq::Profile> p, const juce::String& path, int builtIn)
{
    {
        juce::SpinLock::ScopedLockType sl (profileLock);
        profile = std::move (p);
        profilePath = path;
        builtInIndex = builtIn;
    }
    requestRedesign();
    return true;
}

bool FreeQProcessor::loadProfile (const juce::File& file, juce::String& error)
{
    std::string err;
    auto p = freeq::Profile::load (file.getFullPathName().toStdString(), &err);
    if (! p) { error = err; return false; }
    return setProfile (p, file.getFullPathName(), -1);
}

int FreeQProcessor::numBuiltInProfiles() { return BinaryData::namedResourceListSize; }

juce::String FreeQProcessor::builtInProfileName (int i)
{
    if (i < 0 || i >= numBuiltInProfiles()) return {};
    return juce::File (BinaryData::getNamedResourceOriginalFilename (BinaryData::namedResourceList[i])).getFileNameWithoutExtension();
}

bool FreeQProcessor::loadBuiltInProfile (int i, juce::String& error)
{
    if (i < 0 || i >= numBuiltInProfiles()) { error = "no such built-in profile"; return false; }
    int size = 0;
    const char* res = BinaryData::namedResourceList[i];
    const char* data = BinaryData::getNamedResource (res, size);
    std::string err;
    auto p = freeq::Profile::loadFromMemory (data, (size_t) size, BinaryData::getNamedResourceOriginalFilename (res), &err);
    if (! p) { error = err; return false; }
    return setProfile (p, {}, i);
}

juce::String FreeQProcessor::describeProfile() const
{
    juce::SpinLock::ScopedLockType sl (graphLock);
    return graph.name.isEmpty() ? juce::String ("No profile loaded") : graph.name;
}

void FreeQProcessor::redesign()
{
    std::shared_ptr<const freeq::Profile> p;
    { juce::SpinLock::ScopedLockType sl (profileLock); p = profile; }
    if (! p) return;

    freeq::DesignOptions opt;
    opt.clampToMaxBoost = apvts.getRawParameterValue ("clamp")->load() > 0.5f;
    opt.autoPreamp      = apvts.getRawParameterValue ("autopreamp")->load() > 0.5f;
    const double fs = sampleRateAtomic.load();
    const int mode = (int) apvts.getRawParameterValue ("mode")->load();
    const double pre = freeq::preampDb (*p, opt);
    const int nResp = p->channels();                 // 1 = same response on both channels
    auto src = [nResp] (int c) { return juce::jmin (c, nResp - 1); };

    GraphData g;
    g.name = juce::String (p->name) + (nResp > 1 ? "  (independent L/R)" : "");
    g.hasCeiling = p->hasCeiling() && opt.clampToMaxBoost;
    g.perChannel = nResp > 1;
    g.preampDb = pre;
    for (int i = 0; i < 256; ++i)
    {
        const double f = 20.0 * std::pow (1000.0, i / 255.0);
        g.freqs.push_back ((float) f);
        if (g.hasCeiling) g.ceiling.push_back ((float) (p->ceilingDb (f) + pre));
        for (int c = 0; c < 2; ++c) g.target[(size_t) c].push_back ((float) (freeq::targetDb (*p, src (c), f, opt) + pre + p->channelGainDb (src (c))));
    }

    if (mode == LowLatency)
    {
        freeq::IirDesign d[2];
        for (int c = 0; c < 2; ++c) d[c] = (c > 0 && nResp == 1) ? d[0] : freeq::designLowLatency (*p, src (c), fs, pre, opt);
        for (int c = 0; c < 2; ++c)
            for (float f : g.freqs)
            {
                double r = 0; for (auto& b : d[c].sections) r += freeq::biquadDb (b, fs, f);
                g.actual[(size_t) c].push_back ((float) r);
            }
        g.maxErrDb = juce::jmax (d[0].maxErrDb, d[1].maxErrDb);
        {
            juce::SpinLock::ScopedLockType sl (iirLock);
            for (size_t c = 0; c < 2; ++c)
            {
                sections[c] = std::move (d[c].sections);
                z1[c].assign (sections[c].size(), 0.0);
                z2[c].assign (sections[c].size(), 0.0);
                const int dly = juce::jlimit (0, 4096, (int) std::lround (p->delayMs ((int) src ((int) c)) * fs / 1000.0));
                delayBuf[c].assign ((size_t) dly, 0.0f);
                delayPos[c] = 0;
            }
            iirReady = true;
        }
        juce::MessageManager::callAsync ([this] { setLatencySamples (0); });
    }
    else
    {
        static const int tapOptions[] = { 4096, 8192, 16384 };
        const int taps = tapOptions[juce::jlimit (0, 2, (int) apvts.getRawParameterValue ("taps")->load())];
        juce::AudioBuffer<float> ir (2, taps);
        for (int c = 0; c < 2; ++c)
        {
            auto h = freeq::designLinearPhase (*p, src (c), fs, taps, pre + p->channelGainDb (src (c)), opt);
            ir.copyFrom (c, 0, h.data(), taps);
            for (float f : g.freqs)       // actual response = DFT of the taps
            {
                std::complex<double> acc = 0;
                const double w = 2.0 * juce::MathConstants<double>::pi * f / fs;
                for (int n = 0; n < taps; ++n) acc += (double) h[(size_t) n] * std::polar (1.0, -w * n);
                g.actual[(size_t) c].push_back ((float) (20.0 * std::log10 (std::abs (acc) + 1e-12)));
            }
        }
        convolution.loadImpulseResponse (std::move (ir), fs, juce::dsp::Convolution::Stereo::yes,
                                         juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
        firReady = true;
        juce::MessageManager::callAsync ([this, taps] { setLatencySamples (taps / 2); });
    }
    { juce::SpinLock::ScopedLockType sl (graphLock); graph = std::move (g); }
    ++graphVersion;
}

void FreeQProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int nch = juce::jmin (buffer.getNumChannels(), 2);

    if (apvts.getRawParameterValue ("bypass")->load() > 0.5f) return;
    const int mode = (int) apvts.getRawParameterValue ("mode")->load();

    if (mode == LowLatency)
    {
        juce::SpinLock::ScopedTryLockType lock (iirLock);
        if (! lock.isLocked() || ! iirReady) return;
        for (int c = 0; c < nch; ++c)
        {
            const size_t ci = (size_t) c, ns = sections[ci].size();
            float* x = buffer.getWritePointer (c);
            double* s1 = z1[ci].data();
            double* s2 = z2[ci].data();
            const auto* bq = sections[ci].data();
            auto& dl = delayBuf[ci];
            for (int i = 0; i < n; ++i)
            {
                double v = x[i];
                for (size_t k = 0; k < ns; ++k)             // transposed direct form II
                {
                    const auto& b = bq[k];
                    double y = b.b0 * v + s1[k];
                    s1[k] = b.b1 * v - b.a1 * y + s2[k];
                    s2[k] = b.b2 * v - b.a2 * y;
                    v = y;
                }
                float out = (float) v;
                if (! dl.empty())                           // per-channel delay from the profile (samples)
                {
                    std::swap (out, dl[(size_t) delayPos[ci]]);
                    if (++delayPos[ci] >= (int) dl.size()) delayPos[ci] = 0;
                }
                x[i] = out;
            }
        }
    }
    else
    {
        if (! firReady) return;
        juce::dsp::AudioBlock<float> block (buffer);
        auto sub = block.getSubsetChannelBlock (0, (size_t) nch);
        juce::dsp::ProcessContextReplacing<float> ctx (sub);
        convolution.process (ctx);
    }

    outGain.setTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("trim")->load()));
    if (outGain.isSmoothing())
        for (int i = 0; i < n; ++i) { const float g = outGain.getNextValue(); for (int c = 0; c < nch; ++c) buffer.getWritePointer (c)[i] *= g; }
    else
        buffer.applyGain (0, n, outGain.getTargetValue());
}

void FreeQProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("profilePath", profilePath, nullptr);
    state.setProperty ("builtIn", builtInIndex.load() >= 0 ? builtInProfileName (builtInIndex.load()) : juce::String(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void FreeQProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        if (! tree.isValid()) return;
        apvts.replaceState (tree);
        juce::String err;
        const auto builtIn = tree.getProperty ("builtIn").toString();   // looked up by name so reordering is safe
        if (builtIn.isNotEmpty())
        {
            for (int i = 0; i < numBuiltInProfiles(); ++i)
                if (builtInProfileName (i) == builtIn) { loadBuiltInProfile (i, err); break; }
        }
        else
        {
            juce::File f (tree.getProperty ("profilePath").toString());
            if (f.existsAsFile()) loadProfile (f, err);
        }
    }
}

juce::AudioProcessorEditor* FreeQProcessor::createEditor() { return new FreeQEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FreeQProcessor(); }
