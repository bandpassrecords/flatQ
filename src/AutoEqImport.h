// Import of AutoEq data (https://github.com/jaakkopasanen/AutoEq): a headphone/speaker measurement CSV plus a target CSV
// ("frequency,raw" columns, dB) become a flatQ profile. As in AutoEq, both curves are centred at 1 kHz and the
// correction is target - measurement. AutoEq's own defaults (1/12 oct smoothing, 2 oct in the treble, 18 dB/oct slope
// limit, 6 dB max gain) are stored as hints so the plugin reproduces what AutoEq would design.
#pragma once
#include "FlatqProfile.h"
#include <cstdlib>

namespace flatq {

struct CsvCurve { std::vector<double> freq, db; };

// First column = frequency, data column = the one headed "raw" (else the second column). Header optional.
inline bool parseAutoEqCsv(const std::string& text, CsvCurve& out, std::string* err = nullptr) {
    std::istringstream in(text); std::string line; int dataCol = 1; bool first = true;
    auto split = [](const std::string& l) { std::vector<std::string> v; std::string c; std::istringstream s(l);
        while (std::getline(s, c, ',')) { while (!c.empty() && (c.back() == '\r' || c.back() == ' ')) c.pop_back(); v.push_back(c); } return v; };
    while (std::getline(in, line)) {
        auto cols = split(line); if (cols.size() < 2) continue;
        char* e1 = nullptr; double f = std::strtod(cols[0].c_str(), &e1);
        if (e1 == cols[0].c_str()) {                       // header row
            if (first) for (size_t i = 1; i < cols.size(); ++i) { std::string c = cols[i]; for (auto& ch : c) ch = char(std::tolower((unsigned char) ch)); if (c == "raw") dataCol = int(i); }
            first = false; continue;
        }
        first = false;
        if (size_t(dataCol) >= cols.size()) continue;
        char* e2 = nullptr; double v = std::strtod(cols[size_t(dataCol)].c_str(), &e2);
        if (e2 == cols[size_t(dataCol)].c_str() || f <= 0) continue;
        if (!out.freq.empty() && f <= out.freq.back()) continue;   // keep strictly ascending
        out.freq.push_back(f); out.db.push_back(v);
    }
    if (out.freq.size() < 8) { if (err) *err = "no frequency/dB data found (expected CSV with 'frequency,raw' columns)"; return false; }
    return true;
}

// Correction = target - measurement, both centred at 1 kHz, on a 960-point log grid.
inline FlatqProfile buildAutoEqProfile(const CsvCurve& m, const CsvCurve& t, const std::string& name, double maxBoostDb = 6.0) {
    const double m1k = interpDb(m.freq, m.db, 1000.0), t1k = interpDb(t.freq, t.db, 1000.0);
    FlatqProfile p; p.name = name; p.type = "headphone"; p.sameBoth = true;
    const int n = 960;   // ~96 points/octave: finer than the AutoEq data itself (~70/oct)
    Channel& c = p.ch[0];
    c.freq = logGrid(n, 20.0, 20000.0);
    for (double f : c.freq) c.gain.push_back((interpDb(t.freq, t.db, f) - t1k) - (interpDb(m.freq, m.db, f) - m1k));
    p.ch[1] = p.ch[0];
    // the profile's own suggested maximum (AutoEq's max gain): a per-frequency ceiling, shown as the dashed line and always
    // enforced while "Respect max boost" is on, plus the matching slider hint
    p.ceilFreq = c.freq; p.ceilGain.assign(c.freq.size(), maxBoostDb);
    p.hintMaxBoostDb = maxBoostDb; p.hintSmoothOct = 1.0 / 12; p.hintTrebleSmoothOct = 2.0; p.hintSlopeDbOct = 18;
    return p;
}

// A finished AutoEq equalization curve (e.g. its results for crinacle's measurements): used as the correction as it is.
inline FlatqProfile buildPreEqProfile(const CsvCurve& eq, const std::string& name) {
    FlatqProfile p; p.name = name; p.type = "headphone"; p.sameBoth = true;
    p.ch[0].freq = eq.freq; p.ch[0].gain = eq.db;
    p.ch[1] = p.ch[0];
    return p;   // no hints: AutoEq already smoothed and limited it
}

inline std::optional<FlatqProfile> buildAutoEqProfile(const std::string& measurementCsv, const std::string& targetCsv,
                                                      const std::string& name, std::string* err = nullptr, double maxBoostDb = 6.0) {
    CsvCurve m, t;
    std::string e;
    if (!parseAutoEqCsv(measurementCsv, m, &e)) { if (err) *err = "measurement: " + e; return std::nullopt; }
    if (!parseAutoEqCsv(targetCsv, t, &e)) { if (err) *err = "target: " + e; return std::nullopt; }
    return buildAutoEqProfile(m, t, name, maxBoostDb);
}

} // namespace flatq
