// Unified view over every supported profile format: per-channel target response in dB.
#pragma once
#include "LvlsProfile.h"
#include "TmReqProfile.h"
#include <memory>

namespace freeq {

struct Profile {
    std::string name;
    std::shared_ptr<const LvlsProfile> lvls;   // Sonarworks (.lvls): one curve for both channels + boost ceiling
    std::shared_ptr<const TmReqProfile> tm;    // RME TotalMix Room EQ (.tmreq): parametric, independent L/R

    int channels() const { return tm ? 2 : 1; }                        // distinct responses
    double correctionDb(int ch, double f) const { return tm ? tm->responseDb(ch, f) : lvls->correction().gainAt(f); }
    bool hasCeiling() const { return lvls != nullptr; }
    double ceilingDb(double f) const { return lvls ? lvls->maxBoost().gainAt(f) : 1e9; }
    double delayMs(int ch) const { return tm ? tm->delayMs[ch] : 0.0; }
    double channelGainDb(int ch) const { return tm ? tm->chanGainDb[ch] : 0.0; }

    static std::shared_ptr<const Profile> loadFromMemory(const void* data, size_t size, const std::string& filename,
                                                          std::string* err = nullptr) {
        auto p = std::make_shared<Profile>();
        auto dot = filename.find_last_of('.'); std::string ext = dot == std::string::npos ? "" : filename.substr(dot);
        for (auto& c : ext) c = char(std::tolower((unsigned char) c));
        auto slash = filename.find_last_of("/\\"); p->name = filename.substr(slash == std::string::npos ? 0 : slash + 1);
        if (dot != std::string::npos && dot > (slash == std::string::npos ? 0 : slash)) p->name = p->name.substr(0, p->name.size() - ext.size());
        if (ext == ".lvls") {
            auto l = LvlsProfile::parse(std::vector<uint8_t>((const uint8_t*) data, (const uint8_t*) data + size), err);
            if (!l) return nullptr;
            if (!l->manufacturer().empty()) p->name = l->manufacturer() + " " + l->model() + " - " + l->mode();
            p->lvls = std::make_shared<const LvlsProfile>(std::move(*l));
        } else if (ext == ".tmreq") {
            auto t = TmReqProfile::parse(std::string((const char*) data, size), err);
            if (!t) return nullptr;
            p->tm = std::make_shared<const TmReqProfile>(std::move(*t));
        } else { if (err) *err = "unsupported file type (use .lvls or .tmreq)"; return nullptr; }
        return p;
    }

    static std::shared_ptr<const Profile> load(const std::string& path, std::string* err = nullptr) {
        std::ifstream f(path, std::ios::binary);
        if (!f) { if (err) *err = "cannot open file"; return nullptr; }
        std::vector<char> d((std::istreambuf_iterator<char>(f)), {});
        return loadFromMemory(d.data(), d.size(), path, err);
    }
};

} // namespace freeq
