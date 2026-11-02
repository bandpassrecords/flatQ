// Parser for Sonarworks ".lvls" EQ profile files ("PEQb" container).
//
// Layout (little endian), reverse-engineered:
//   0x00  "PEQb" magic, 4 bytes
//   ...   header (version bytes, 16-byte id, "Sonarworks", uint32 curveCount = 7)
//   then curveCount blocks, each:
//       uint32 curveId (1,2,5,6,6,7,7) + 16 bytes (extra params, kept raw)
//       uint32 pointCount (355)
//       pointCount x { double freqHz, double gainDb, double phaseOrAux }   (24 bytes)
//   trailer: uint32 pairCount, 1 byte, then pairCount x (key,value) strings,
//            each string = uint32 length + bytes + NUL.
//
// Curve meaning (inferred from data):
//   [0],[1]  correction curve (identical; likely L/R)  -> the EQ to apply
//   [2]      max boost allowed per frequency (dB), 0..15 with taper at the extremes
//   [3],[4]  correction -/+ FixedErrorBandRange (3 dB) (lower / upper bound), id 6
//   [5],[6]  same again, id 7
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace flatq {

struct CurvePoint { double freqHz, gainDb, aux; };

struct Curve {
    uint32_t id = 0;
    uint8_t  extra[16] {};
    std::vector<CurvePoint> points;

    // Log-frequency linear interpolation of gain in dB. Clamps outside range.
    double gainAt(double f) const {
        if (points.empty()) return 0.0;
        if (f <= points.front().freqHz) return points.front().gainDb;
        if (f >= points.back().freqHz)  return points.back().gainDb;
        size_t lo = 0, hi = points.size() - 1;
        while (hi - lo > 1) { size_t m = (lo + hi) / 2; (points[m].freqHz <= f ? lo : hi) = m; }
        double t = std::log(f / points[lo].freqHz) / std::log(points[hi].freqHz / points[lo].freqHz);
        return points[lo].gainDb + t * (points[hi].gainDb - points[lo].gainDb);
    }
};

struct LvlsProfile {
    std::vector<Curve> curves;
    std::map<std::string, std::string> meta;

    const Curve& correction()    const { return curves.at(0); }
    const Curve& maxBoost()      const { return curves.at(2); }
    std::string manufacturer()   const { return get("META_Manufacturer"); }
    std::string model()          const { return get("META_Model"); }
    std::string mode()           const { return get("META_Mode"); }
    double errorBandDb()         const { auto s = get("META_FixedErrorBandRange"); return s.empty() ? 0.0 : std::stod(s); }
    std::string get(const std::string& k) const { auto i = meta.find(k); return i == meta.end() ? "" : i->second; }

    // Largest boost the correction asks for, and the largest boost the profile allows there.
    double peakBoostDb() const { double m = 0; for (auto& p : correction().points) m = std::fmax(m, p.gainDb); return m; }
    // Preamp (negative dB) so that correction never exceeds 0 dBFS gain-wise: -max(0, peak boost).
    double safePreampDb() const { return -peakBoostDb(); }

    static std::optional<LvlsProfile> load(const std::string& path, std::string* err = nullptr) {
        std::ifstream f(path, std::ios::binary);
        if (!f) { if (err) *err = "cannot open file"; return std::nullopt; }
        std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
        return parse(d, err);
    }

    static std::optional<LvlsProfile> parse(const std::vector<uint8_t>& d, std::string* err = nullptr) {
        auto fail = [&](const char* m) { if (err) *err = m; return std::optional<LvlsProfile>{}; };
        if (d.size() < 64 || std::memcmp(d.data(), "PEQb", 4) != 0) return fail("not a PEQb file");
        // Find "Sonarworks" marker, curve count follows it.
        const char tag[] = "Sonarworks";
        size_t p = 0; bool found = false;
        for (size_t i = 0; i + sizeof(tag) < d.size() && i < 128; ++i)
            if (!std::memcmp(&d[i], tag, sizeof(tag) - 1)) { p = i + sizeof(tag) - 1; found = true; break; }
        if (!found) return fail("missing Sonarworks tag");
        auto u32 = [&](size_t o) { uint32_t v; std::memcpy(&v, &d[o], 4); return v; };
        uint32_t n = u32(p); p += 4;
        if (n == 0 || n > 64) return fail("bad curve count");
        LvlsProfile out;
        for (uint32_t c = 0; c < n; ++c) {
            if (p + 24 > d.size()) return fail("truncated curve header");
            Curve cv; cv.id = u32(p); std::memcpy(cv.extra, &d[p + 4], 16);
            uint32_t cnt = u32(p + 20); p += 24;
            if (cnt > 100000 || p + size_t(cnt) * 24 > d.size()) return fail("bad point count");
            cv.points.resize(cnt);
            std::memcpy(cv.points.data(), &d[p], size_t(cnt) * 24);
            p += size_t(cnt) * 24;
            out.curves.push_back(std::move(cv));
        }
        // Trailer
        if (p + 5 <= d.size()) {
            uint32_t pairs = u32(p); p += 5;
            auto str = [&](std::string& s) {
                if (p + 4 > d.size()) return false;
                uint32_t L = u32(p); p += 4;
                if (p + L > d.size()) return false;
                s.assign(reinterpret_cast<const char*>(&d[p]), L); p += L;
                if (p < d.size() && d[p] == 0) ++p;
                return true;
            };
            for (uint32_t i = 0; i < pairs; ++i) {
                std::string k, v; if (!str(k) || !str(v)) break; out.meta[k] = v;
            }
        }
        return out;
    }
};

} // namespace flatq
