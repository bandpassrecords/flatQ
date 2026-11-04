// Native flatQ profile (".flatq"): a small, human-readable text format.
//
//   flatq 1
//   name Focal Speakers - Flat
//   type headphone                 # optional: headphone | speaker (shown as an icon); inferred when missing
//   channel L                      # L, R, or LR (same response on both channels)
//     delay_ms 0.068               # optional
//     gain_db 0.1                  # optional channel gain
//     curve log 355 20 22000       # sampled correction in dB on a log grid: N points from f0 to f1 Hz ...
//       -2.17 -2.16 ...            #   ... numbers follow, until "end"
//     end
//     curve points 3               # ... or explicit frequency/gain pairs
//       20 4.5  1000 0  20000 -2
//     end
//     aux log 355 20 22000 ... end # optional extra column kept from imports (unused by the engine)
//     band 1000 2.0 -3.5           # parametric peaking band: Hz, Q, dB (any number, stacked on the curve)
//   hint max_boost_db 9            # optional recommended plugin settings, applied when the profile is loaded
//   hint bass_limit_hz 50
//   hint smooth_oct 0.0833         # also: treble_smooth_oct, slope_db_oct
//   ceiling log 355 20 22000       # optional max-boost limit (dB) per frequency
//     ...
//   end
// '#' starts a comment. The response of a channel = interpolated curve + all bands + gain_db.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace flatq {

struct ParamBand { double freq = 1000, q = 1, gainDb = 0; int type = 0; };

// Analog-prototype magnitude of a peaking band at f.
inline double bandDb(const ParamBand& b, double f) {
    if (b.gainDb == 0.0) return 0.0;
    double A = std::pow(10.0, b.gainDb / 40.0), x = f / b.freq, d = 1 - x * x;
    double num = d * d + (x * A / b.q) * (x * A / b.q);
    double den = d * d + (x / (A * b.q)) * (x / (A * b.q));
    return 10.0 * std::log10(num / den);
}

inline std::vector<double> logGrid(int n, double f0, double f1) {
    std::vector<double> g(n);
    for (int i = 0; i < n; ++i) g[i] = f0 * std::pow(f1 / f0, n > 1 ? double(i) / (n - 1) : 0.0);
    return g;
}

// Log-frequency linear interpolation, clamped at the ends.
inline double interpDb(const std::vector<double>& f, const std::vector<double>& g, double x) {
    if (f.empty()) return 0.0;
    if (x <= f.front()) return g.front();
    if (x >= f.back()) return g.back();
    size_t lo = 0, hi = f.size() - 1;
    while (hi - lo > 1) { size_t m = (lo + hi) / 2; (f[m] <= x ? lo : hi) = m; }
    double t = std::log(x / f[lo]) / std::log(f[hi] / f[lo]);
    return g[lo] + t * (g[hi] - g[lo]);
}

struct Channel {
    std::vector<double> freq, gain, aux;     // sampled correction (empty = none)
    std::vector<ParamBand> bands;
    double delayMs = 0, gainDb = 0;

    double responseDb(double f) const {
        double s = gainDb + interpDb(freq, gain, f);
        for (auto& b : bands) s += bandDb(b, f);
        return s;
    }
    bool sameAs(const Channel& o) const {
        if (freq != o.freq || gain != o.gain || aux != o.aux || delayMs != o.delayMs || gainDb != o.gainDb || bands.size() != o.bands.size()) return false;
        for (size_t i = 0; i < bands.size(); ++i)
            if (bands[i].freq != o.bands[i].freq || bands[i].q != o.bands[i].q || bands[i].gainDb != o.bands[i].gainDb) return false;
        return true;
    }
};

struct FlatqProfile {
    std::string name;
    std::string type;                        // "headphone" or "speaker"; empty = not stored (see kind())
    std::array<Channel, 2> ch;
    bool sameBoth = true;                    // true: ch[0] applies to both channels
    std::vector<double> ceilFreq, ceilGain;  // optional boost ceiling
    double hintMaxBoostDb = -1, hintBassLimitHz = -1, hintSmoothOct = -1, hintTrebleSmoothOct = -1, hintSlopeDbOct = -1;   // recommended settings (< 0 = none)
    std::vector<std::string> warnings;

    int channels() const { return sameBoth ? 1 : 2; }

    // What the profile is for. Stored when known; otherwise guessed: a profile with a different response or delay per
    // channel is a speaker (room / driver alignment), a single shared response is a headphone.
    std::string kind() const {
        if (type == "headphone" || type == "speaker") return type;
        if (!sameBoth || ch[0].delayMs != 0 || ch[1].delayMs != 0) return "speaker";
        return "headphone";
    }

    // Suggests settings from the curve itself (used for imports that carry no recommendation). If the correction asks for
    // a big boost at the bottom of the spectrum, the device is rolling off there and boosting cannot restore the output:
    // it only costs headroom (the preamp follows the peak boost) and risks the drivers. The bass limit (below it no boost is
    // applied) goes where the correction first exceeds +6 dB (coming down from 200 Hz), rounded up to 5 Hz, and the max boost
    // is 6 dB.
    // Nothing is suggested when the correction at 20 Hz stays within +12 dB.
    void suggestHints() {
        double knee = 0, at20 = -1e9;
        for (int c = 0; c < channels(); ++c) {
            const Channel& k = ch[size_t(c)];
            at20 = std::max(at20, k.responseDb(20.0));
            for (double f = 200.0; f >= 20.0; f *= std::pow(2.0, -1.0 / 48.0))
                if (k.responseDb(f) > 6.0) { knee = std::max(knee, f); break; }
        }
        if (at20 <= 12.0 || knee <= 0) return;
        hintBassLimitHz = std::ceil(knee / 5.0) * 5.0;
        hintMaxBoostDb = 6.0;
    }

    // ------------------------------------------------------------------ text format
    static std::optional<FlatqProfile> parse(const std::string& text, std::string* err = nullptr) {
        auto fail = [&](const std::string& m, int line) { if (err) *err = "line " + std::to_string(line) + ": " + m; return std::optional<FlatqProfile>{}; };
        FlatqProfile p; std::istringstream in(text); std::string line;
        int ln = 0; bool header = false, sawL = false, sawR = false; Channel* cur = &p.ch[0];

        auto readBlock = [&](std::vector<double>& out) -> bool {           // numbers until "end"
            while (std::getline(in, line)) {
                ++ln; auto h = line.find('#'); if (h != std::string::npos) line.resize(h);
                std::istringstream ls(line); std::string tok;
                while (ls >> tok) { if (tok == "end") return true; try { out.push_back(std::stod(tok)); } catch (...) { return false; } }
            }
            return false;
        };
        auto readGrid = [&](std::istringstream& ls, std::vector<double>& f, std::vector<double>& g, bool wantPairs, int& n) -> bool {
            std::string mode; ls >> mode;
            std::vector<double> nums;
            if (mode == "log") {
                double f0, f1; if (!(ls >> n >> f0 >> f1) || n < 2 || f0 <= 0 || f1 <= f0) return false;
                f = logGrid(n, f0, f1); if (!readBlock(g)) return false; return int(g.size()) == n;
            }
            if (mode == "points") {
                if (!(ls >> n) || n < 2 || !readBlock(nums) || int(nums.size()) != 2 * n) return false;
                f.clear(); g.clear(); for (int i = 0; i < n; ++i) { f.push_back(nums[2 * i]); g.push_back(nums[2 * i + 1]); }
                return true;
            }
            (void) wantPairs; return false;
        };

        while (std::getline(in, line)) {
            ++ln; auto h = line.find('#'); if (h != std::string::npos) line.resize(h);
            std::istringstream ls(line); std::string kw; if (!(ls >> kw)) continue;
            if (kw == "flatq") { int v = 0; ls >> v; if (v != 1) return fail("unsupported flatq version", ln); header = true; }
            else if (!header) return fail("missing 'flatq 1' header", ln);
            else if (kw == "name") { std::getline(ls, p.name); p.name.erase(0, p.name.find_first_not_of(" \t")); }
            else if (kw == "type") { ls >> p.type; }
            else if (kw == "channel") {
                std::string c; ls >> c;
                if (c == "LR") { cur = &p.ch[0]; p.sameBoth = true; }
                else if (c == "L") { cur = &p.ch[0]; sawL = true; p.sameBoth = false; }
                else if (c == "R") { cur = &p.ch[1]; sawR = true; p.sameBoth = false; }
                else return fail("channel must be L, R or LR", ln);
            }
            else if (kw == "hint") { std::string k; double v = 0; ls >> k >> v; if (k == "max_boost_db") p.hintMaxBoostDb = v; else if (k == "bass_limit_hz") p.hintBassLimitHz = v;
                else if (k == "smooth_oct") p.hintSmoothOct = v; else if (k == "treble_smooth_oct") p.hintTrebleSmoothOct = v; else if (k == "slope_db_oct") p.hintSlopeDbOct = v; }
            else if (kw == "delay_ms") ls >> cur->delayMs;
            else if (kw == "gain_db") ls >> cur->gainDb;
            else if (kw == "band") { ParamBand b; if (!(ls >> b.freq >> b.q >> b.gainDb) || b.freq <= 0 || b.q <= 0) return fail("band needs: freq Q gain", ln); cur->bands.push_back(b); }
            else if (kw == "curve") { int n; if (!readGrid(ls, cur->freq, cur->gain, true, n)) return fail("bad curve block", ln); }
            else if (kw == "aux") { std::vector<double> f, g; int n; if (!readGrid(ls, f, g, true, n)) return fail("bad aux block", ln); cur->aux = g; }
            else if (kw == "ceiling") { int n; if (!readGrid(ls, p.ceilFreq, p.ceilGain, true, n)) return fail("bad ceiling block", ln); }
            else return fail("unknown keyword '" + kw + "'", ln);
        }
        if (!header) { if (err) *err = "not a flatq file"; return std::nullopt; }
        if (!p.sameBoth && sawL && !sawR) p.ch[1] = p.ch[0];
        if (!p.sameBoth && sawR && !sawL) p.ch[0] = p.ch[1];
        return p;
    }

    std::string serialize() const {
        std::string o = "# flatQ profile\nflatq 1\nname " + name + "\n";
        if (!type.empty()) o += "type " + type + "\n";
        if (hintMaxBoostDb >= 0) o += "hint max_boost_db " + std::to_string(hintMaxBoostDb) + "\n";
        if (hintBassLimitHz >= 0) o += "hint bass_limit_hz " + std::to_string(hintBassLimitHz) + "\n";
        if (hintSmoothOct >= 0) o += "hint smooth_oct " + std::to_string(hintSmoothOct) + "\n";
        if (hintTrebleSmoothOct >= 0) o += "hint treble_smooth_oct " + std::to_string(hintTrebleSmoothOct) + "\n";
        if (hintSlopeDbOct >= 0) o += "hint slope_db_oct " + std::to_string(hintSlopeDbOct) + "\n";
        auto num = [](double v) { char b[40]; std::snprintf(b, sizeof b, "%.6f", v);
            std::string s = b; if (s.find('.') != std::string::npos) { while (s.back() == '0') s.pop_back(); if (s.back() == '.') s.pop_back(); }
            return s == "-0" ? std::string("0") : s; };
        auto isLog = [](const std::vector<double>& f) {
            if (f.size() < 2) return false;
            auto g = logGrid(int(f.size()), f.front(), f.back());
            for (size_t i = 0; i < f.size(); ++i) if (std::abs(g[i] / f[i] - 1) > 1e-5) return false;
            return true; };
        auto block = [&](const char* kw, const std::vector<double>& f, const std::vector<double>& g, const char* ind) {
            if (f.empty()) return;
            const bool lg = isLog(f);
            o += std::string(ind) + kw + (lg ? " log " + std::to_string(f.size()) + " " + num(f.front()) + " " + num(f.back())
                                              : " points " + std::to_string(f.size())) + "\n";
            std::string row; int k = 0;
            auto flush = [&] { if (!row.empty()) { o += std::string(ind) + "  " + row + "\n"; row.clear(); k = 0; } };
            for (size_t i = 0; i < g.size(); ++i) {
                if (!lg) row += num(f[i]) + " ";
                row += num(g[i]) + " "; if (++k >= 8) flush();
            }
            flush(); o += std::string(ind) + "end\n";
        };
        auto chan = [&](const char* id, const Channel& c) {
            o += std::string("channel ") + id + "\n";
            if (c.delayMs != 0) o += "  delay_ms " + num(c.delayMs) + "\n";
            if (c.gainDb != 0) o += "  gain_db " + num(c.gainDb) + "\n";
            block("curve", c.freq, c.gain, "  ");
            if (!c.aux.empty() && c.aux.size() == c.freq.size()) block("aux", c.freq, c.aux, "  ");
            for (auto& b : c.bands) o += "  band " + num(b.freq) + " " + num(b.q) + " " + num(b.gainDb) + "\n";
        };
        if (sameBoth || ch[0].sameAs(ch[1])) chan("LR", ch[0]); else { chan("L", ch[0]); chan("R", ch[1]); }
        block("ceiling", ceilFreq, ceilGain, "");
        return o;
    }
};

} // namespace flatq
