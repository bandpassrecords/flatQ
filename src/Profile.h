// Unified profile used by the engine: a FlatqProfile, loadable from .flatq or imported from
// Sonarworks .lvls / .lvlsproj and RME TotalMix .tmreq files.
#pragma once
#include "FlatqProfile.h"
#include "LvlsProfile.h"
#include "LvlsProjImport.h"
#include "TmReqProfile.h"
#include <fstream>
#include <memory>

namespace flatq {

inline FlatqProfile lvlsToFlatq(const LvlsProfile& l, const std::string& name) {
    FlatqProfile p; p.name = name; p.sameBoth = true;
    for (auto& pt : l.correction().points) { p.ch[0].freq.push_back(pt.freqHz); p.ch[0].gain.push_back(pt.gainDb); p.ch[0].aux.push_back(pt.aux); }
    for (auto& pt : l.maxBoost().points) { p.ceilFreq.push_back(pt.freqHz); p.ceilGain.push_back(pt.gainDb); }
    p.ch[1] = p.ch[0];
    return p;
}

struct Profile {
    std::string name;
    FlatqProfile d;

    int channels() const { return d.channels(); }                       // distinct responses (1 = same on both)
    const Channel& chan(int ch) const { return d.ch[d.sameBoth ? 0 : ch]; }
    double correctionDb(int ch, double f) const { return chan(ch).responseDb(f); }   // includes channel gain
    bool hasCeiling() const { return !d.ceilFreq.empty(); }
    double ceilingDb(double f) const { return hasCeiling() ? interpDb(d.ceilFreq, d.ceilGain, f) : 1e9; }
    double delayMs(int ch) const { return chan(ch).delayMs; }
    double channelGainDb(int ch) const { return chan(ch).gainDb; }
    bool isParametric(int ch) const { return chan(ch).freq.empty() && !chan(ch).bands.empty(); }   // exact biquads possible
    const std::vector<ParamBand>& bands(int ch) const { return chan(ch).bands; }

    static std::shared_ptr<const Profile> loadFromMemory(const void* data, size_t size, const std::string& filename,
                                                          std::string* err = nullptr) {
        auto dot = filename.find_last_of('.'); std::string ext = dot == std::string::npos ? "" : filename.substr(dot);
        for (auto& c : ext) c = char(std::tolower((unsigned char) c));
        auto slash = filename.find_last_of("/\\");
        std::string base = filename.substr(slash == std::string::npos ? 0 : slash + 1);
        if (!ext.empty() && base.size() > ext.size()) base.resize(base.size() - ext.size());
        auto p = std::make_shared<Profile>(); p->name = base;
        const std::vector<uint8_t> bytes((const uint8_t*) data, (const uint8_t*) data + size);
        if (ext == ".flatq") {
            auto f = FlatqProfile::parse(std::string((const char*) data, size), err); if (!f) return nullptr;
            p->d = std::move(*f);
        } else if (ext == ".lvls") {
            auto l = LvlsProfile::parse(bytes, err); if (!l) return nullptr;
            std::string nm = l->manufacturer().empty() ? base : l->manufacturer() + " " + l->model() + " - " + l->mode();
            p->d = lvlsToFlatq(*l, nm);
        } else if (ext == ".lvlsproj") {
            auto f = parseLvlsProj(bytes, err); if (!f) return nullptr;
            p->d = std::move(*f);
        } else if (ext == ".tmreq") {
            auto t = TmReqProfile::parse(std::string((const char*) data, size), err); if (!t) return nullptr;
            p->d = t->toFlatq(base);
        } else { if (err) *err = "unsupported file type (use .flatq, .lvls, .lvlsproj or .tmreq)"; return nullptr; }
        if (p->d.name.empty()) p->d.name = base;
        p->name = p->d.name;
        return p;
    }

    static std::shared_ptr<const Profile> load(const std::string& path, std::string* err = nullptr) {
        std::ifstream f(path, std::ios::binary);
        if (!f) { if (err) *err = "cannot open file"; return nullptr; }
        std::vector<char> d((std::istreambuf_iterator<char>(f)), {});
        return loadFromMemory(d.data(), d.size(), path, err);
    }
};

} // namespace flatq
