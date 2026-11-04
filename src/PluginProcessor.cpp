#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <BinaryData.h>
#include <TargetData.h>
#include "AutoEqImport.h"
#include "MeasurementDb.h"

class FlatQProcessor::DesignThread : public juce::Thread
{
public:
    explicit DesignThread (FlatQProcessor& o) : juce::Thread ("flatQ design"), owner (o) {}
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
    FlatQProcessor& owner;
    std::atomic<bool> dirty { false };
};

juce::AudioProcessorValueTreeState::ParameterLayout FlatQProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode",
           StringArray { "Low latency (IIR)", "Linear phase (FIR)", "Minimum phase (FIR)" }, 2));   // exact and ~0 latency
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "taps", 1 }, "FIR length",
           StringArray { "4096", "8192", "16384", "32768", "65536" }, 2));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "iirres", 1 }, "IIR resolution",
           StringArray { "Normal (6 bands/oct)", "High (48 bands/oct)" }, 0));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "clamp", 1 }, "Respect max boost", true));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "maxboost", 1 }, "Max boost",
           NormalisableRange<float> (0.f, 30.f, 0.5f), 12.f));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "smooth", 1 }, "Smoothing",
           StringArray { "Off", "1/24 oct", "1/12 oct", "1/6 oct", "1/3 oct", "1/2 oct", "1 oct" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "treblesmooth", 1 }, "Treble smoothing",
           StringArray { "Same", "1/6 oct", "1/3 oct", "1/2 oct", "1 oct", "2 oct" }, 0));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "maxslope", 1 }, "Max slope (dB/oct)",
           NormalisableRange<float> (1.f, 36.f, 1.f), 36.f,   // starts at the maximum (= no limit); lowering it tightens the limit
           AudioParameterFloatAttributes()
               .withStringFromValueFunction ([] (float v, int) { return v >= 35.5f ? String ("Off") : String ((int) v) + " dB/oct"; })
               .withValueFromStringFunction ([] (const String& t) { return t.trim().startsWithIgnoreCase ("off") ? 36.f : t.getFloatValue(); })));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "treblegain", 1 }, "Treble gain (%)",
           NormalisableRange<float> (0.f, 100.f, 1.f), 100.f));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "basslimit", 1 }, "Bass limit",
           NormalisableRange<float> (20.f, 200.f, 1.f), 20.f,
           AudioParameterFloatAttributes()
               .withStringFromValueFunction ([] (float v, int) { return v <= 20.5f ? String ("Off") : String ((int) v) + " Hz"; })     // 20 Hz (the bottom of the range) = off
               .withValueFromStringFunction ([] (const String& t) { return t.trim().startsWithIgnoreCase ("off") ? 20.f : t.getFloatValue(); })));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "autopreamp", 1 }, "Auto preamp", true));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "bypass", 1 }, "Bypass", false));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "bypassexport", 1 }, "Skip on export (offline render)", true));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "drywet", 1 }, "Dry/Wet",
           NormalisableRange<float> (0.f, 100.f, 1.f), 100.f,
           AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String ((int) v) + " %"; })));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "mono", 1 }, "Mono", false));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "mute", 1 }, "Mute output", false));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "swap", 1 }, "Swap L/R", false));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "trim", 1 }, "Output trim",
           NormalisableRange<float> (-72.f, 6.f, 0.1f), 0.f));
    return l;
}

FlatQProcessor::FlatQProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    for (auto& b : dryBuf) b.assign (kMaxLatency, 0.0f);
    for (auto& c : manualEq) for (auto& b : c) b = 0.f;
    for (auto* id : { "mode", "taps", "iirres", "clamp", "maxboost", "smooth", "treblesmooth", "maxslope", "treblegain", "basslimit", "autopreamp" }) apvts.addParameterListener (id, this);
    designThread = std::make_unique<DesignThread> (*this);
    designThread->startThread();

    // a fresh instance starts from the last profile used (a host restoring saved state will override this afterwards)
    juce::String last = getSetting ("loadLastProfile", true) ? lastProfileFile().loadFileAsString().trim() : juce::String(), err;
    if (last.startsWith ("file:"))
    {
        juce::File f (last.fromFirstOccurrenceOf ("file:", false, false));
        if (f.existsAsFile()) loadProfile (f, err);
    }

    bool haveProfile;
    { juce::SpinLock::ScopedLockType sl (profileLock); haveProfile = (profile != nullptr); }
    if (! haveProfile)   // nothing used before (or its file is gone): start with the first profile in the list
        for (auto& f : userProfileFiles()) if (loadProfile (f, err)) break;
}

static juce::PropertiesFile::Options settingsOptions()
{
    juce::PropertiesFile::Options o;
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    o.millisecondsBeforeSaving = 0;
    return o;
}

bool FlatQProcessor::getSetting (const juce::String& key, bool defaultValue)
{
    juce::PropertiesFile p (appDataDir().getChildFile ("flatQ.settings"), settingsOptions());
    return p.getBoolValue (key, defaultValue);
}

void FlatQProcessor::setSetting (const juce::String& key, bool value)
{
    appDataDir().createDirectory();
    juce::PropertiesFile p (appDataDir().getChildFile ("flatQ.settings"), settingsOptions());
    p.setValue (key, value);
    p.saveIfNeeded();
}

juce::File FlatQProcessor::lastProfileFile()
{
    return appDataDir().getChildFile ("last_profile.txt");
}

FlatQProcessor::~FlatQProcessor()
{
    for (auto* id : { "mode", "taps", "iirres", "clamp", "maxboost", "smooth", "treblesmooth", "maxslope", "treblegain", "basslimit", "autopreamp" }) apvts.removeParameterListener (id, this);
    designThread->stopThread (5000);
}

bool FlatQProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == l.getMainInputChannelSet()
        && (l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
         || l.getMainOutputChannelSet() == juce::AudioChannelSet::mono());
}

// Output trim in dB. With auto preamp on (safe headroom) the trim can't go above 0 dB: that is the limit of the profile.
float FlatQProcessor::outputTrimDb() const
{
    const float t = apvts.getRawParameterValue ("trim")->load();
    return apvts.getRawParameterValue ("autopreamp")->load() > 0.5f ? juce::jmin (t, 0.f) : t;
}

void FlatQProcessor::parameterChanged (const juce::String&, float) { requestRedesign(); }
void FlatQProcessor::requestRedesign() { if (designThread) designThread->request(); }

void FlatQProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampleRateAtomic = sampleRate;
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };
    convolution.prepare (spec);
    outGain.reset (sampleRate, 0.02);
    muteGain.reset (sampleRate, 0.02);
    mixGain.reset (sampleRate, 0.02);
    mixGain.setCurrentAndTargetValue (apvts.getRawParameterValue ("drywet")->load() / 100.f);
    dryCopy.setSize (2, samplesPerBlock, false, false, true);
    muteGain.setCurrentAndTargetValue (apvts.getRawParameterValue ("mute")->load() > 0.5f ? 0.f : 1.f);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputTrimDb()));
    firReady = false;
    requestRedesign();
}

bool FlatQProcessor::setProfile (std::shared_ptr<const flatq::Profile> p, const juce::String& path, int builtIn, bool applyHints)
{
    if (applyHints)   // profile's recommended settings, or the defaults when it has none
    {
        auto setParam = [this] (const char* id, double v) {
            if (auto* prm = apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 ((float) v)); };
        setParam ("maxboost", p->d.hintMaxBoostDb >= 0 ? p->d.hintMaxBoostDb : 12.0);
        setParam ("basslimit", p->d.hintBassLimitHz >= 0 ? p->d.hintBassLimitHz : 20.0);   // 20 = off
        auto nearest = [] (const std::vector<double>& widths, double w) {   // index of the closest smoothing width
            int best = 0; for (int i = 1; i < (int) widths.size(); ++i) if (std::abs (std::log (w / widths[(size_t) i])) < std::abs (std::log (w / widths[(size_t) best]))) best = i;
            return best; };
        setParam ("smooth", p->d.hintSmoothOct > 0 ? nearest ({ 0, 1.0 / 24, 1.0 / 12, 1.0 / 6, 1.0 / 3, 0.5, 1.0 }, p->d.hintSmoothOct) : 0);
        setParam ("treblesmooth", p->d.hintTrebleSmoothOct > 0 ? nearest ({ 0, 1.0 / 6, 1.0 / 3, 0.5, 1.0, 2.0 }, p->d.hintTrebleSmoothOct) : 0);
        setParam ("maxslope", p->d.hintSlopeDbOct >= 0 ? p->d.hintSlopeDbOct : 36.0);   // 36 = off
    }
    if (applyHints) for (auto& c : manualEq) for (auto& b : c) b = 0.f;   // a new profile starts from its own equalization
    {
        juce::SpinLock::ScopedLockType sl (profileLock);
        profile = std::move (p);
        profilePath = path;
        builtInIndex = builtIn;
    }
    {   // remember it for the next new instance
        auto f = lastProfileFile();
        if (f.getParentDirectory().createDirectory().wasOk())
            f.replaceWithText (builtIn >= 0 ? "builtin:" + builtInProfileName (builtIn) : "file:" + path);
    }
    requestRedesign();
    return true;
}

bool FlatQProcessor::loadProfile (const juce::File& file, juce::String& error, bool applyHints)
{
    std::string err;
    auto p = flatq::Profile::load (file.getFullPathName().toStdString(), &err);
    if (! p) { error = err; return false; }
    return setProfile (p, file.getFullPathName(), -1, applyHints);
}

int FlatQProcessor::numBuiltInProfiles() { return BinaryData::namedResourceListSize; }

juce::String FlatQProcessor::builtInProfileName (int i)
{
    if (i < 0 || i >= numBuiltInProfiles()) return {};
    return juce::File (BinaryData::getNamedResourceOriginalFilename (BinaryData::namedResourceList[i])).getFileNameWithoutExtension();
}

int FlatQProcessor::numTargets() { return FlatQTargetData::namedResourceListSize; }

juce::String FlatQProcessor::targetName (int i)
{
    if (i < 0 || i >= numTargets()) return {};
    return juce::File (FlatQTargetData::getNamedResourceOriginalFilename (FlatQTargetData::namedResourceList[i])).getFileNameWithoutExtension();
}

juce::File FlatQProcessor::appDataDir()
{
    auto d = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);   // ~/Library on macOS
   #if JUCE_MAC
    d = d.getChildFile ("Application Support");
   #endif
    const auto dir = d.getChildFile ("flatQ");
    const auto old = d.getChildFile ("FlatQ");   // the folder used before the rename (same folder on a case-insensitive disk)
    if (! dir.exists() && old.isDirectory()) old.moveFileTo (dir);
    return dir;
}

juce::String FlatQProcessor::profileKind (const juce::File& file)
{
    auto p = flatq::Profile::load (file.getFullPathName().toStdString());
    return p ? juce::String (p->d.kind()) : juce::String ("headphone");
}

juce::File FlatQProcessor::userProfilesDir() { return appDataDir().getChildFile ("Profiles"); }
juce::File FlatQProcessor::importedProfilesDir() { return userProfilesDir(); }

juce::Array<juce::File> FlatQProcessor::userProfileFiles()
{
    auto files = userProfilesDir().findChildFiles (juce::File::findFiles, false, "*.flatq");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareIgnoreCase (b.getFileName()) < 0; });
    return files;
}

namespace
{
    // Writes `d` into the user library as "<name>.flatq" (name made unique unless `overwrite`).
    bool saveToLibrary (flatq::FlatqProfile d, juce::String name, bool overwrite, juce::File& out, juce::String& error)
    {
        auto dir = FlatQProcessor::userProfilesDir();
        if (! dir.createDirectory().wasOk()) { error = "cannot create " + dir.getFullPathName(); return false; }
        name = name.trim().isEmpty() ? juce::String ("Profile") : name.trim();
        juce::String unique = name;
        for (int n = 2; ! overwrite && dir.getChildFile (juce::File::createLegalFileName (unique) + ".flatq").existsAsFile(); ++n) unique = name + " " + juce::String (n);
        d.name = unique.toStdString();
        out = dir.getChildFile (juce::File::createLegalFileName (unique) + ".flatq");
        if (! out.replaceWithText (juce::String (d.serialize()))) { error = "cannot write " + out.getFullPathName(); return false; }
        return true;
    }
}

bool FlatQProcessor::importProfileFile (const juce::File& file, juce::String& error)
{
    std::string err;
    auto p = flatq::Profile::load (file.getFullPathName().toStdString(), &err);
    if (! p) { error = err; return false; }
    juce::File out;
    if (! saveToLibrary (p->d, juce::String (p->name), false, out, error)) return false;
    return loadProfile (out, error);
}

bool FlatQProcessor::addBuiltInToLibrary (int builtIn, juce::String& error, bool applyHints)
{
    if (builtIn < 0 || builtIn >= numBuiltInProfiles()) { error = "no such built-in profile"; return false; }
    int size = 0; const char* res = BinaryData::namedResourceList[builtIn];
    const char* data = BinaryData::getNamedResource (res, size);
    std::string err;
    auto p = flatq::Profile::loadFromMemory (data, (size_t) size, BinaryData::getNamedResourceOriginalFilename (res), &err);
    if (! p) { error = err; return false; }
    juce::File out;
    if (! saveToLibrary (p->d, juce::String (p->name), true, out, error)) return false;
    return loadProfile (out, error, applyHints);
}

bool FlatQProcessor::saveEqualizationAs (const juce::String& name, juce::String& error)
{
    std::shared_ptr<const flatq::Profile> p;
    { juce::SpinLock::ScopedLockType sl (profileLock); p = profile; }
    if (! p) { error = "no profile is loaded"; return false; }

    auto opt = baseOptions();
    bool differs = false;
    for (size_t i = 0; i < 31; ++i) differs |= std::abs (manualEq[0][i].load() - manualEq[1][i].load()) > 1e-4f;
    if (manualEditedCount() > 0)
        for (size_t c = 0; c < 2; ++c)
            for (auto& b : manualEq[c]) opt.manualDb[c].push_back (b.load());

    const int nResp = p->channels();
    const bool twoChannels = nResp > 1 || differs;
    flatq::FlatqProfile out;
    out.name = name.toStdString(); out.type = p->d.kind(); out.sameBoth = ! twoChannels;
    const auto grid = flatq::logGrid (960, 20.0, 20000.0);
    for (int c = 0; c < (twoChannels ? 2 : 1); ++c)
    {
        const int sc = juce::jmin (c, nResp - 1);
        auto& ch = out.ch[(size_t) c];
        ch.freq = grid;
        for (double f : grid) ch.gain.push_back (flatq::targetDb (*p, sc, f, opt));
        ch.delayMs = p->delayMs (sc);
    }
    if (! twoChannels) out.ch[1] = out.ch[0];
    out.hintMaxBoostDb = 30.0;   // the boosts are already part of the curve (limits and manual changes included): do not clamp it again

    juce::File file;
    if (! saveToLibrary (out, name, false, file, error)) return false;
    return loadProfile (file, error);
}

bool FlatQProcessor::exportProfile (const juce::File& source, juce::File dest, juce::String& error)
{
    std::string err;
    auto p = flatq::Profile::load (source.getFullPathName().toStdString(), &err);
    if (! p) { error = err; return false; }
    if (! dest.hasFileExtension ("flatq")) dest = dest.withFileExtension ("flatq");
    if (! dest.replaceWithText (juce::String (p->d.serialize()))) { error = "cannot write " + dest.getFullPathName(); return false; }
    return true;
}

bool FlatQProcessor::duplicateProfile (int builtIn, const juce::File& file, juce::String& error)
{
    std::shared_ptr<const flatq::Profile> p;
    std::string err;
    if (builtIn >= 0)
    {
        int size = 0; const char* res = BinaryData::namedResourceList[builtIn];
        const char* data = BinaryData::getNamedResource (res, size);
        p = flatq::Profile::loadFromMemory (data, (size_t) size, BinaryData::getNamedResourceOriginalFilename (res), &err);
    }
    else p = flatq::Profile::load (file.getFullPathName().toStdString(), &err);
    if (! p) { error = err; return false; }
    juce::File out;
    return saveToLibrary (p->d, juce::String (p->name) + " copy", false, out, error);
}

bool FlatQProcessor::renameProfile (const juce::File& file, const juce::String& newName, juce::String& error)
{
    std::string err;
    auto p = flatq::Profile::load (file.getFullPathName().toStdString(), &err);
    if (! p) { error = err; return false; }
    const auto target = userProfilesDir().getChildFile (juce::File::createLegalFileName (newName.trim()) + ".flatq");
    if (newName.trim().isEmpty()) { error = "the name is empty"; return false; }
    if (target != file && target.existsAsFile()) { error = "a profile called \"" + newName.trim() + "\" already exists"; return false; }
    juce::File out;
    if (! saveToLibrary (p->d, newName, true, out, error)) return false;
    const bool wasCurrent = getProfilePath() == file.getFullPathName();
    if (out != file) file.deleteFile();
    if (wasCurrent) return loadProfile (out, error, false);
    return true;
}

bool FlatQProcessor::deleteProfile (const juce::File& file, juce::String& error)
{
    const bool wasCurrent = getProfilePath() == file.getFullPathName();
    if (! file.moveToTrash() && ! file.deleteFile()) { error = "cannot delete " + file.getFullPathName(); return false; }
    if (wasCurrent)   // don't keep a profile loaded whose file is gone: fall back to the next one in the library, if any
        for (auto& f : userProfileFiles()) if (loadProfile (f, error)) return true;
    return true;
}

bool FlatQProcessor::importAutoEq (const juce::File& measurement, const juce::String& targetCsv, const juce::String& targetLabel, juce::String& error)
{
    if (! measurement.existsAsFile()) { error = "measurement file not found"; return false; }
    std::string err;
    const auto name = measurement.getFileNameWithoutExtension() + " - " + targetLabel;
    auto prof = flatq::buildAutoEqProfile (measurement.loadFileAsString().toStdString(), targetCsv.toStdString(), name.toStdString(), &err);
    if (! prof) { error = err; return false; }
    juce::File out;
    if (! saveToLibrary (*prof, name, true, out, error)) return false;
    return loadProfile (out, error);
}

int FlatQProcessor::defaultTargetFor (int measurementIndex)
{
    const auto& e = MeasurementDb::get().entry (measurementIndex);
    const juce::String want = e.form == "over-ear" ? "Harman over-ear 2018" : "Harman in-ear 2019";
    for (int i = 0; i < numTargets(); ++i) if (targetName (i) == want) return i;
    return 0;
}

bool FlatQProcessor::importMeasurement (int measurementIndex, int targetIndex, juce::String& error)
{
    auto& db = MeasurementDb::get();
    flatq::CsvCurve m, t;
    if (db.available() && measurementIndex >= 0 && measurementIndex < db.size() && db.entry (measurementIndex).builtIn >= 0)
        return addBuiltInToLibrary (db.entry (measurementIndex).builtIn, error);
    if (! db.available() || ! db.curve (measurementIndex, m)) { error = "measurement data not available"; return false; }
    const auto& e = db.entry (measurementIndex);
    if (e.builtIn >= 0) return addBuiltInToLibrary (e.builtIn, error);   // an embedded profile offered in the search
    if (e.preEq)   // AutoEq's finished equalization (no raw measurement exists): the curve is the correction
    {
        const auto label = e.name + " (" + e.source + " - AutoEq EQ)";
        auto prof = flatq::buildPreEqProfile (m, label.toStdString());
        juce::File out;
        if (! saveToLibrary (prof, label, true, out, error)) return false;
        return loadProfile (out, error);
    }
    if (targetIndex < 0) targetIndex = defaultTargetFor (measurementIndex);
    std::string err;
    if (! flatq::parseAutoEqCsv (targetCsvText (targetIndex).toStdString(), t, &err)) { error = "target: " + juce::String (err); return false; }
    const auto name = e.name + " (" + e.source + ") - " + targetName (targetIndex);
    auto prof = flatq::buildAutoEqProfile (m, t, name.toStdString());
    juce::File out;
    if (! saveToLibrary (prof, name, true, out, error)) return false;
    return loadProfile (out, error);
}

juce::String FlatQProcessor::targetCsvText (int i)
{
    if (i < 0 || i >= numTargets()) return {};
    int size = 0;
    const char* data = FlatQTargetData::getNamedResource (FlatQTargetData::namedResourceList[i], size);
    return juce::String::fromUTF8 (data, size);
}

juce::String FlatQProcessor::builtInProfileKind (int i)
{
    if (i < 0 || i >= numBuiltInProfiles()) return {};
    return juce::File (BinaryData::getNamedResourceOriginalFilename (BinaryData::namedResourceList[i])).getFileExtension().trimCharactersAtStart (".").toUpperCase();
}

bool FlatQProcessor::loadBuiltInProfile (int i, juce::String& error, bool applyHints)
{
    if (i < 0 || i >= numBuiltInProfiles()) { error = "no such built-in profile"; return false; }
    int size = 0;
    const char* res = BinaryData::namedResourceList[i];
    const char* data = BinaryData::getNamedResource (res, size);
    std::string err;
    auto p = flatq::Profile::loadFromMemory (data, (size_t) size, BinaryData::getNamedResourceOriginalFilename (res), &err);
    if (! p) { error = err; return false; }
    return setProfile (p, {}, i, applyHints);
}

juce::String FlatQProcessor::describeProfile() const
{
    juce::SpinLock::ScopedLockType sl (graphLock);
    return graph.name.isEmpty() ? juce::String ("No profile loaded") : graph.name;
}

flatq::DesignOptions FlatQProcessor::baseOptions() const
{
    flatq::DesignOptions opt;
    opt.clampToMaxBoost = apvts.getRawParameterValue ("clamp")->load() > 0.5f;
    opt.maxBoostDb      = apvts.getRawParameterValue ("maxboost")->load();
    {
        static const double widths[] = { 0.0, 1.0 / 24, 1.0 / 12, 1.0 / 6, 1.0 / 3, 0.5, 1.0 };
        opt.smoothingOct = widths[juce::jlimit (0, 6, (int) apvts.getRawParameterValue ("smooth")->load())];
        static const double tw[] = { 0.0, 1.0 / 6, 1.0 / 3, 0.5, 1.0, 2.0 };
        opt.trebleSmoothOct = tw[juce::jlimit (0, 5, (int) apvts.getRawParameterValue ("treblesmooth")->load())];
        { const double bl = apvts.getRawParameterValue ("basslimit")->load(); opt.bassLimitHz = bl > 20.5 ? bl : 0.0; }   // bottom of the range = off
        { const double sl = apvts.getRawParameterValue ("maxslope")->load(); opt.maxSlopeDbOct = sl < 35.5 ? sl : 0.0; }   // the top of the range = off
        opt.trebleGain   = apvts.getRawParameterValue ("treblegain")->load() / 100.0;
    }
    opt.autoPreamp      = apvts.getRawParameterValue ("autopreamp")->load() > 0.5f;
    return opt;
}

void FlatQProcessor::redesign()
{
    std::shared_ptr<const flatq::Profile> p;
    { juce::SpinLock::ScopedLockType sl (profileLock); p = profile; }
    if (! p) return;

    flatq::DesignOptions opt = baseOptions();
    const double fs = sampleRateAtomic.load();
    const int mode = (int) apvts.getRawParameterValue ("mode")->load();
    const int nResp = p->channels();                 // 1 = same response on both channels

    GraphData g;
    for (int c = 0; c < 2; ++c)                      // the equalization as designed, before manual offsets (what the manual EQ shows)
        for (size_t i = 0; i < 31; ++i) g.bandBase[(size_t) c][i] = (float) flatq::targetDb (*p, juce::jmin (c, nResp - 1), flatq::manualFreqs()[i], opt);

    bool manualDiffers = false;
    for (size_t i = 0; i < 31; ++i) manualDiffers |= std::abs (manualEq[0][i].load() - manualEq[1][i].load()) > 1e-4f;
    if (manualEditedCount() > 0)
        for (size_t c = 0; c < 2; ++c)
            for (auto& b : manualEq[c]) opt.manualDb[c].push_back (b.load());
    const int nEff = (nResp > 1 || manualDiffers) ? 2 : 1;   // distinct channel designs
    const double pre = flatq::preampDb (*p, opt);
    auto src = [nEff] (int c) { return juce::jmin (c, nEff - 1); };

    g.name = juce::String (p->name);
    g.kind = juce::String (p->d.kind());
    g.hasCeiling = opt.clampToMaxBoost;
    g.perChannel = nEff > 1;
    g.preampDb = pre;
    for (int i = 0; i < 640; ++i)
    {
        const double f = 20.0 * std::pow (1000.0, i / 639.0);
        g.freqs.push_back ((float) f);
        if (g.hasCeiling) g.ceiling.push_back ((float) (std::min (p->ceilingDb (f), opt.maxBoostDb) + pre));
        for (int c = 0; c < 2; ++c) g.measured[(size_t) c].push_back ((float) -p->correctionDb (src (c), f));
        for (int c = 0; c < 2; ++c) g.target[(size_t) c].push_back ((float) (flatq::targetDb (*p, src (c), f, opt) + pre));
    }

    if (mode == LowLatency)
    {
        const int iirBands = apvts.getRawParameterValue ("iirres")->load() > 0.5f ? 48 : 6;
        flatq::IirDesign d[2];
        for (int c = 0; c < 2; ++c) d[c] = (c > 0 && nEff == 1) ? d[0] : flatq::designLowLatency (*p, src (c), fs, pre, opt, iirBands);
        for (int c = 0; c < 2; ++c)
            for (float f : g.freqs)
            {
                double r = 0; for (auto& b : d[c].sections) r += flatq::biquadDb (b, fs, f);
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
        latencyNow = 0;
        juce::MessageManager::callAsync ([this] { setLatencySamples (0); });
    }
    else
    {
        static const int tapOptions[] = { 4096, 8192, 16384, 32768, 65536 };
        const int taps = tapOptions[juce::jlimit (0, 4, (int) apvts.getRawParameterValue ("taps")->load())];
        juce::AudioBuffer<float> ir (2, taps);
        for (int c = 0; c < 2; ++c)
        {
            auto h = mode == MinPhase ? flatq::designMinPhase (*p, src (c), fs, taps, pre, opt)
                                      : flatq::designLinearPhase (*p, src (c), fs, taps, pre, opt);
            ir.copyFrom (c, 0, h.data(), taps);
            {   // achieved response: zero-padded FFT of the taps, interpolated at the graph frequencies
                const size_t N = (size_t) taps * 4;
                std::vector<std::complex<double>> a (N);
                for (int n = 0; n < taps; ++n) a[(size_t) n] = (double) h[(size_t) n];
                flatq::fftInPlace (a, false);
                for (float f : g.freqs)
                {
                    const double pos = (double) f * (double) N / fs;
                    const size_t i0 = juce::jmin ((size_t) pos, N / 2 - 1);
                    const double t = pos - (double) i0;
                    const double m = std::abs (a[i0]) * (1.0 - t) + std::abs (a[i0 + 1]) * t;
                    g.actual[(size_t) c].push_back ((float) (20.0 * std::log10 (m + 1e-12)));
                }
            }
        }
        convolution.loadImpulseResponse (std::move (ir), fs, juce::dsp::Convolution::Stereo::yes,
                                         juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
        firReady = true;
        const int lat = mode == MinPhase ? 0 : taps / 2;
        latencyNow = lat;
        juce::MessageManager::callAsync ([this, lat] { setLatencySamples (lat); });
    }
    { juce::SpinLock::ScopedLockType sl (graphLock); graph = std::move (g); }
    ++graphVersion;
}

void FlatQProcessor::applyMute (juce::AudioBuffer<float>& buffer)
{
    muteGain.setTargetValue (apvts.getRawParameterValue ("mute")->load() > 0.5f ? 0.f : 1.f);
    const int n = buffer.getNumSamples(), nch = juce::jmin (buffer.getNumChannels(), 2);
    if (muteGain.isSmoothing())
        for (int i = 0; i < n; ++i) { const float g = muteGain.getNextValue(); for (int c = 0; c < nch; ++c) buffer.getWritePointer (c)[i] *= g; }
    else if (muteGain.getTargetValue() < 0.5f)
        buffer.clear();
}

void FlatQProcessor::processDry (juce::AudioBuffer<float>& buffer)
{
    const int len = juce::jmin (latencyNow.load(), kMaxLatency);
    if (len <= 0) return;
    const int n = buffer.getNumSamples();
    for (int c = 0; c < juce::jmin (buffer.getNumChannels(), 2); ++c)
    {
        float* x = buffer.getWritePointer (c);
        auto& d = dryBuf[(size_t) c]; int& pos = dryPos[(size_t) c];
        if (pos >= len) pos = 0;
        for (int i = 0; i < n; ++i)
        {
            std::swap (x[i], d[(size_t) pos]);
            if (++pos >= len) pos = 0;
        }
    }
}

void FlatQProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int nch = juce::jmin (buffer.getNumChannels(), 2);

    const bool manualBypass = apvts.getRawParameterValue ("bypass")->load() > 0.5f;
    const bool skipForExport = apvts.getRawParameterValue ("bypassexport")->load() > 0.5f && isNonRealtime();
    if (nch == 2 && ! skipForExport && apvts.getRawParameterValue ("swap")->load() > 0.5f)   // input L/R swap
        std::swap_ranges (buffer.getWritePointer (0), buffer.getWritePointer (0) + n, buffer.getWritePointer (1));
    auto peakOf = [&] (std::array<std::atomic<float>, 2>& m, std::atomic<bool>& clipFlag)
    {
        for (int c = 0; c < nch; ++c)
        {
            const float pk = buffer.getMagnitude (c, 0, n);
            if (pk >= 0.9999f) clipFlag = true;
            const float old = m[(size_t) c].load();
            m[(size_t) c] = pk > old ? pk : old * 0.92f;   // instant attack, smooth release (per block)
        }
    };
    peakOf (inPeak, inClipped);
    struct OutMeter { std::function<void()> f; ~OutMeter() { f(); } } outMeter { [&] { applyMute (buffer); peakOf (outPeak, outClipped); } };
    if (manualBypass || skipForExport) { processDry (buffer); return; }
    const int mode = (int) apvts.getRawParameterValue ("mode")->load();

    // Dry/Wet: keep a copy of the input; it is delayed by the reported latency so it lines up with the processed signal
    mixGain.setTargetValue (apvts.getRawParameterValue ("drywet")->load() / 100.f);
    const bool mixing = nch > 0 && (mixGain.isSmoothing() || mixGain.getTargetValue() < 0.999f);
    if (mixing)
    {
        dryCopy.setSize (2, n, false, false, true);   // no reallocation unless the host sends a bigger block than announced
        for (int c = 0; c < nch; ++c) dryCopy.copyFrom (c, 0, buffer, c, 0, n);
    }
    auto doMix = [&]
    {
        if (! mixing) return;
        processDry (dryCopy);
        for (int i = 0; i < n; ++i)
        {
            const float w = mixGain.getNextValue();
            for (int c = 0; c < nch; ++c) { float* o = buffer.getWritePointer (c); o[i] = w * o[i] + (1.f - w) * dryCopy.getReadPointer (c)[i]; }
        }
    };

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

    doMix();   // dry / wet right after the EQ, before the output trim
    outGain.setTargetValue (juce::Decibels::decibelsToGain (outputTrimDb()));
    if (outGain.isSmoothing())
        for (int i = 0; i < n; ++i) { const float g = outGain.getNextValue(); for (int c = 0; c < nch; ++c) buffer.getWritePointer (c)[i] *= g; }
    else
        buffer.applyGain (0, n, outGain.getTargetValue());

    if (nch == 2 && apvts.getRawParameterValue ("mono")->load() > 0.5f)    // mono check: (L + R) / 2 on both outputs
    {
        float* l = buffer.getWritePointer (0); float* r = buffer.getWritePointer (1);
        for (int i = 0; i < n; ++i) l[i] = r[i] = 0.5f * (l[i] + r[i]);
    }
}

void FlatQProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("profilePath", profilePath, nullptr);
    state.setProperty ("builtIn", builtInIndex.load() >= 0 ? builtInProfileName (builtInIndex.load()) : juce::String(), nullptr);
    for (int c = 0; c < 2; ++c) { juce::StringArray v; for (auto& b : manualEq[(size_t) c]) v.add (juce::String (b.load(), 2)); state.setProperty (c ? "manualEqR" : "manualEqL", v.joinIntoString (","), nullptr); }
    state.setProperty ("manualLinked", manualLinked.load(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void FlatQProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        if (! tree.isValid()) return;
        apvts.replaceState (tree);
        juce::String err;
        const auto builtIn = tree.getProperty ("builtIn").toString();   // looked up by name so reordering is safe
        if (builtIn.isNotEmpty())   // sessions from before the library: the embedded profile goes into the library; a profile that is no longer embedded is looked up there
        {
            bool done = false;
            for (int i = 0; i < numBuiltInProfiles() && ! done; ++i)
                if (builtInProfileName (i) == builtIn) done = addBuiltInToLibrary (i, err, false);
            auto lib = userProfilesDir().getChildFile (juce::File::createLegalFileName (builtIn) + ".flatq");
            if (! done && lib.existsAsFile()) loadProfile (lib, err, false);
        }
        else
        {
            juce::File f (tree.getProperty ("profilePath").toString());
            if (f.existsAsFile()) loadProfile (f, err, false);
        }
        manualLinked = tree.hasProperty ("manualLinked") ? (bool) tree.getProperty ("manualLinked") : true;
        for (int c = 0; c < 2; ++c)   // after the profile load, which would otherwise be a reason to reset
        {
            auto v = juce::StringArray::fromTokens (tree.getProperty (c ? "manualEqR" : "manualEqL").toString(), ",", "");
            for (int i = 0; i < numManualBands(); ++i) manualEq[(size_t) c][(size_t) i] = i < v.size() ? v[i].getFloatValue() : 0.f;
        }
        requestRedesign();
    }
}

juce::AudioProcessorEditor* FlatQProcessor::createEditor() { return new FlatQEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FlatQProcessor(); }
