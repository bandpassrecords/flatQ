// Parser for RME TotalMix "Room EQ" presets (.tmreq). The file looks like XML but is not valid XML
// (tag names contain spaces), so it is parsed line by line.
//   <Room EQ L> / <Room EQ R>  -> channel sections
//   <val e="REQ Band3 Freq" v="173,"/>   (Freq Hz, Q, Gain dB, BandNType), "REQ Delay" (ms), "Chan Gain" (dB)
// Assumptions: Q follows the RBJ peaking convention; type 0 = peak. Other types are flagged in warnings()
// and treated as peaks.
#pragma once
#include "FlatqProfile.h"
#include <array>
#include <cmath>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace flatq {

struct TmReqProfile {
    static constexpr int kBands = 9;
    std::array<std::array<ParamBand, kBands>, 2> bands {};
    std::array<double, 2> delayMs {}, chanGainDb {};
    std::vector<std::string> warnings;

    double responseDb(int ch, double f) const {
        double s = chanGainDb[ch];
        for (auto& b : bands[ch]) s += flatq::bandDb(b, f);
        return s;
    }

    FlatqProfile toFlatq(const std::string& name) const {
        FlatqProfile p; p.name = name; p.sameBoth = false; p.warnings = warnings;
        for (int c = 0; c < 2; ++c) {
            p.ch[c].delayMs = delayMs[c]; p.ch[c].gainDb = chanGainDb[c];
            for (auto& b : bands[c]) if (b.gainDb != 0.0) p.ch[c].bands.push_back(b);
        }
        return p;
    }

    static std::optional<TmReqProfile> load(const std::string& path, std::string* err = nullptr) {
        std::ifstream f(path);
        if (!f) { if (err) *err = "cannot open file"; return std::nullopt; }
        std::stringstream ss; ss << f.rdbuf();
        return parse(ss.str(), err);
    }

    static std::optional<TmReqProfile> parse(const std::string& text, std::string* err = nullptr) {
        TmReqProfile p;
        static const std::regex secRe(R"re(<Room EQ ([LR])>)re");
        static const std::regex valRe(R"re(e="([^"]*)"\s+v="([^"]*)")re");
        static const std::regex bandRe(R"re(REQ Band(\d+) ?(Freq|Q|Gain|Type))re");
        std::istringstream in(text); std::string line; int ch = -1; bool any = false;
        while (std::getline(in, line)) {
            std::smatch m;
            if (std::regex_search(line, m, secRe)) { ch = m[1] == "L" ? 0 : 1; continue; }
            if (ch < 0 || !std::regex_search(line, m, valRe)) continue;
            std::string key = m[1], val = m[2]; double v = 0;
            try { v = std::stod(val); } catch (...) { continue; }
            if (key == "REQ Delay") p.delayMs[ch] = v;
            else if (key == "Chan Gain") p.chanGainDb[ch] = v;
            else {
                std::smatch bm;
                if (!std::regex_match(key, bm, bandRe)) continue;
                int idx = std::stoi(bm[1]) - 1; if (idx < 0 || idx >= kBands) continue;
                auto& b = p.bands[ch][idx]; any = true;
                if (bm[2] == "Freq") b.freq = v; else if (bm[2] == "Q") b.q = v;
                else if (bm[2] == "Gain") b.gainDb = v; else b.type = int(std::lround(v));
            }
        }
        if (!any) { if (err) *err = "no Room EQ bands found"; return std::nullopt; }
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < kBands; ++i)
                if (p.bands[c][i].type != 0 && p.bands[c][i].gainDb != 0)
                    p.warnings.push_back("band " + std::to_string(i + 1) + (c ? " R" : " L") + ": type " +
                                         std::to_string(p.bands[c][i].type) + " treated as peak");
        return p;
    }
};

} // namespace flatq
