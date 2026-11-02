// Importer for Sonarworks ".lvlsproj" project files. The project is a text header followed by compressed/encrypted
// parts, but the "eqb" part (the EQ curves) is stored in the clear:
//   "PEQb" ... then for each curve:  u32 type (0x0b = float32 {freq,gain}, 0x0f = float32 {freq,gain,aux}),
//   u32 pointCount, u32 7, pointCount * (8 or 12) bytes, then length-prefixed key/value strings
//   (ChannelName, ChannelIndex, ChannelDelayMs, Frequency = "LOG_355_20_22000", ...).
// Each channel has a measurement (type 0x0b) and its correction (type 0x0f) = -measurement.
#pragma once
#include "FlatqProfile.h"
#include <cstdint>
#include <cstring>

namespace flatq {

inline std::optional<FlatqProfile> parseLvlsProj(const std::vector<uint8_t>& d, std::string* err = nullptr) {
    auto fail = [&](const char* m) { if (err) *err = m; return std::optional<FlatqProfile>{}; };
    const std::string text(d.begin(), d.begin() + std::min<size_t>(d.size(), 4096));
    size_t eqbType = text.find("<Type>eqb</Type>");
    size_t sz = 0;
    if (eqbType != std::string::npos) { auto s = text.find("<Size>", eqbType); if (s != std::string::npos) sz = std::strtoull(text.c_str() + s + 6, nullptr, 10); }
    size_t start = std::string::npos;
    for (size_t i = 0; i + 4 < std::min<size_t>(d.size(), 8192); ++i) if (!std::memcmp(&d[i], "PEQb", 4)) { start = i; break; }
    if (start == std::string::npos) return fail("no PEQb block in project");
    if (sz == 0 || start + sz > d.size()) sz = d.size() - start;
    const uint8_t* b = &d[start];
    auto u32 = [&](size_t o) { uint32_t v; std::memcpy(&v, b + o, 4); return v; };
    auto f32 = [&](size_t o) { float v; std::memcpy(&v, b + o, 4); return double(v); };

    struct Rec { size_t hdr, data, stride, count, end; };
    std::vector<Rec> recs;
    for (size_t p = 8; p + 12 <= sz;) {
        uint32_t type = u32(p), cnt = u32(p + 4), id = u32(p + 8);
        if ((type == 0x0b || type == 0x0f) && cnt >= 2 && cnt <= 100000 && id == 7) {
            size_t stride = type == 0x0b ? 8 : 12;
            if (p + 12 + stride * cnt <= sz) { recs.push_back({ p, p + 12, stride, cnt, p + 12 + stride * cnt }); p += 12 + stride * cnt; continue; }
        }
        ++p;
    }
    if (recs.empty()) return fail("no EQ curves found in project");

    auto kv = [&](size_t from, size_t to, const char* key) -> std::string {   // length-prefixed value following 'key\0'
        size_t kl = std::strlen(key);
        for (size_t i = from; i + kl + 5 < to; ++i)
            if (!std::memcmp(b + i, key, kl) && b[i + kl] == 0) {
                uint32_t L = u32(i + kl + 1);
                if (L < 64 && i + kl + 5 + L <= to) return std::string((const char*) b + i + kl + 5, L);
            }
        return {};
    };

    FlatqProfile p; p.sameBoth = false; bool have[2] = { false, false };
    std::vector<double> meas[2], measF[2];
    for (size_t k = 0; k < recs.size(); ++k) {
        const Rec& r = recs[k];
        size_t segEnd = k + 1 < recs.size() ? recs[k + 1].hdr : sz;
        std::string nm = kv(r.end, segEnd, "ChannelName"), idx = kv(r.end, segEnd, "ChannelIndex");
        int c = (nm == "Right" || idx == "1") ? 1 : 0;
        std::vector<double> f(r.count), g(r.count), aux;
        for (size_t i = 0; i < r.count; ++i) {
            f[i] = f32(r.data + i * r.stride); g[i] = f32(r.data + i * r.stride + 4);
            if (r.stride == 12) aux.push_back(f32(r.data + i * r.stride + 8));
        }
        if (r.stride == 8) { measF[c] = f; meas[c] = g; continue; }          // measurement; correction is its negative
        Channel& ch = p.ch[c]; ch.freq = f; ch.gain = g; ch.aux = aux;
        std::string dl = kv(r.end, segEnd, "ChannelDelayMs");
        if (!dl.empty()) ch.delayMs = std::stod(dl);
        have[c] = true;
    }
    for (int c = 0; c < 2; ++c) {                                              // measurement-only channel: invert it
        if (!have[c] && !meas[c].empty()) { p.ch[c].freq = measF[c]; for (double v : meas[c]) p.ch[c].gain.push_back(-v); have[c] = true; }
    }
    if (!have[0] && !have[1]) return fail("no channel curves in project");
    if (!have[0]) p.ch[0] = p.ch[1]; if (!have[1]) p.ch[1] = p.ch[0];
    return p;
}

} // namespace flatq
