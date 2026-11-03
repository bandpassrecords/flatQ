// Filter design from an LvlsProfile curve. Pure C++17 (no JUCE) so it is unit-testable.
//   designLinearPhase : symmetric FIR, latency = (taps-1)/2 samples, magnitude-exact, zero phase distortion
//   designLowLatency  : cascade of peaking biquads (min-phase IIR), ~0 latency, approximate magnitude
#pragma once
#include "Profile.h"
#include <algorithm>
#include <cmath>
#include <array>
#include <complex>

#ifndef M_PI   // MSVC does not define it by default
#define M_PI 3.14159265358979323846
#endif

namespace flatq {

// Slope limiter (idea from AutoEq's equalize()): y is a curve in dB sampled at ascending freqs. Boosts are limited so
// they never rise faster than maxSlopeDbOct, once walking left-to-right and once right-to-left; the pointwise minimum
// keeps both flanks of a peak gentle, so narrow spikes are cut down while broad corrections pass through.
// Cuts are left untouched (limiting them would widen dips instead of narrowing them).
inline void limitSlope(const std::vector<double>& freqs, std::vector<double>& y, double maxSlopeDbOct) {
    const size_t n = y.size();
    if (maxSlopeDbOct <= 0 || n < 2 || freqs.size() != n) return;
    std::vector<double> pos(n), ltr, rtl;
    for (size_t i = 0; i < n; ++i) pos[i] = std::max(0.0, y[i]);
    ltr = rtl = pos;
    for (size_t i = 1; i < n; ++i)
        ltr[i] = std::min(pos[i], ltr[i - 1] + maxSlopeDbOct * std::log2(freqs[i] / freqs[i - 1]));
    for (size_t i = n - 1; i-- > 0;)
        rtl[i] = std::min(pos[i], rtl[i + 1] + maxSlopeDbOct * std::log2(freqs[i + 1] / freqs[i]));
    for (size_t i = 0; i < n; ++i) y[i] = std::min(y[i], 0.0) + std::min(ltr[i], rtl[i]);
}

// Centres of the manual (advanced) per-frequency trim: ISO third-octave, 20 Hz .. 20 kHz.
inline const std::vector<double>& manualFreqs() {
    static const std::vector<double> f = { 20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800,
                                           1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000 };
    return f;
}

struct DesignOptions {
    bool   clampToMaxBoost = true;   // limit boosts: global maxBoostDb and the profile's own per-frequency ceiling
    double maxBoostDb      = 12.0;   // global boost limit (dB), applies to every profile
    double smoothingOct    = 0.0;    // fractional-octave smoothing width of the target (0 = off, 1/3 = third-octave)
    double trebleSmoothOct = 0.0;    // width used above ~6 kHz (blended in from ~1.5 kHz); 0 = same as smoothingOct
    double bassLimitHz     = 0.0;    // 0 = off. Boosts are cancelled below this frequency (cuts still apply); the allowed boost rises back to the max boost by 1.4x this frequency
    double maxSlopeDbOct   = 0.0;    // 0 = off. Boosts may not rise faster than this (dB/octave); see limitSlope()
    double trebleGain      = 1.0;    // scales the correction above ~8 kHz (blended in from ~4 kHz); 1 = untouched
    bool   autoPreamp      = true;   // lower overall gain so the EQ never boosts above 0 dB
    double extraGainDb     = 0.0;    // user trim
    std::array<std::vector<double>, 2> manualDb;   // manual per-band offsets (dB) per channel at manualFreqs(); empty = none. Applied last, after the limits

    // Slope-limited curves, sampled on a log grid per channel. Filled lazily by targetDb(); valid for ONE profile,
    // so make a fresh DesignOptions (or call resetCache) whenever the profile changes.
    mutable std::vector<std::vector<double>> slopeCache;
    void resetCache() { slopeCache.clear(); }
};

// Smooth 0..1 blend (raised cosine) in log-frequency between f0 and f1.
inline double logBlend(double f, double f0, double f1) {
    double t = std::clamp(std::log2(f / f0) / std::log2(f1 / f0), 0.0, 1.0);
    return 0.5 - 0.5 * std::cos(M_PI * t);
}

// Correction before smoothing/limits: profile response with the bass fade-out and treble gain applied.
inline double rawTargetDb(const Profile& p, int ch, double f, const DesignOptions& o) {
    double g = p.correctionDb(ch, f);
    if (o.bassLimitHz > 0 && g > 0 && f < o.bassLimitHz * 1.4) {
        // A speaker that rolls off cannot be boosted back (the correction asks for +40 dB at 20 Hz): below the limit no boost is
        // allowed at all; between the limit and 1.4x it rises smoothly to the maximum boost, so the curve has no corner.
        const double t = std::clamp(std::log2(f / o.bassLimitHz) / std::log2(1.4), 0.0, 1.0);
        const double allowed = (o.clampToMaxBoost ? o.maxBoostDb : 12.0) * (0.5 - 0.5 * std::cos(M_PI * t));
        g = std::min(g, allowed);
    }
    if (o.trebleGain != 1.0) g *= 1.0 + (o.trebleGain - 1.0) * logBlend(f, 4000.0, 8000.0);
    return g;
}

// Smoothing width in octaves at f (frequency dependent: normal width in the mids, treble width above ~6 kHz).
inline double smoothWidthOct(double f, const DesignOptions& o) {
    const double tw = o.trebleSmoothOct > 0 ? o.trebleSmoothOct : o.smoothingOct;
    return o.smoothingOct + (tw - o.smoothingOct) * logBlend(f, 1500.0, 6000.0);
}

inline double smoothedDb(const Profile& p, int ch, double f, const DesignOptions& o) {
    const double w = smoothWidthOct(f, o);
    if (w <= 0) return rawTargetDb(p, ch, f, o);
    const double sigma = w / 2.355;                // Gaussian smoothing in log-frequency, sigma = width / 2.355 octaves
    double sum = 0, wsum = 0;
    for (int k = -9; k <= 9; ++k) {
        double x = k / 3.0 * sigma, wt = std::exp(-0.5 * (x / sigma) * (x / sigma));
        sum += wt * rawTargetDb(p, ch, f * std::pow(2.0, x), o); wsum += wt;
    }
    return sum / wsum;
}

// Target response in dB at f for channel ch, after bass fade, smoothing, slope limit and boost limits. (preamp not included)
inline double targetDb(const Profile& p, int ch, double f, const DesignOptions& o) {
    double g;
    if (o.maxSlopeDbOct > 0) {
        constexpr double kLo = 1.0, kHi = 100000.0; constexpr int kPerOct = 48;
        const int n = int(std::ceil(std::log2(kHi / kLo) * kPerOct)) + 1;
        if (o.slopeCache.size() <= size_t(ch)) o.slopeCache.resize(size_t(ch) + 1);
        auto& c = o.slopeCache[size_t(ch)];
        if (c.empty()) {
            std::vector<double> fr(n); c.resize(n);
            for (int i = 0; i < n; ++i) { fr[i] = kLo * std::pow(2.0, double(i) / kPerOct); c[i] = smoothedDb(p, ch, fr[i], o); }
            limitSlope(fr, c, o.maxSlopeDbOct);
        }
        const double pos = std::clamp(std::log2(std::max(f, kLo) / kLo) * kPerOct, 0.0, double(n - 1));
        const int i = std::min(int(pos), n - 2); const double t = pos - i;
        g = c[i] * (1 - t) + c[i + 1] * t;
    } else g = smoothedDb(p, ch, f, o);
    if (o.clampToMaxBoost) g = std::min({ g, o.maxBoostDb, p.ceilingDb(f) });
    const auto& m = o.manualDb[size_t(std::min(std::max(ch, 0), 1))];
    if (m.size() == manualFreqs().size()) g += interpDb(manualFreqs(), m, f);
    return g;
}

// One preamp for all channels (so L/R balance is preserved): minus the highest boost anywhere.
inline double preampDb(const Profile& p, const DesignOptions& o) {
    double peak = 0;
    for (int ch = 0; ch < 2; ++ch)            // both channels: manual offsets can differ even on a one-response profile
        for (int i = 0; i < 600; ++i)
            peak = std::max(peak, targetDb(p, ch, 20.0 * std::pow(1100.0, i / 599.0), o));
    return (o.autoPreamp ? -peak : 0.0) + o.extraGainDb;
}

// ---------------------------------------------------------------- linear phase FIR
inline void fftInPlace(std::vector<std::complex<double>>& a, bool inverse) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        double ang = 2 * M_PI / double(len) * (inverse ? 1 : -1);
        std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1);
            for (size_t k = 0; k < len / 2; ++k) {
                auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl;
            }
        }
    }
    if (inverse) for (auto& x : a) x /= double(n);
}

// taps must be a power of two. Returns taps coefficients; latency = taps/2 samples.
inline std::vector<float> designLinearPhase(const Profile& p, int ch, double sampleRate, int taps, double pre, const DesignOptions& o = {}) {
    const int shift = std::clamp(int(std::lround(p.delayMs(ch) * sampleRate / 1000.0)), 0, taps / 4);
    std::vector<std::complex<double>> H(taps);
    for (int k = 0; k <= taps / 2; ++k) {
        double f = std::max(1.0, k * sampleRate / taps);
        double mag = std::pow(10.0, (targetDb(p, ch, f, o) + pre) / 20.0);
        H[k] = mag;
        if (k > 0 && k < taps / 2) H[taps - k] = mag;
    }
    fftInPlace(H, true);                      // zero-phase impulse, centred at index 0 (wraps)
    std::vector<float> h(taps);
    for (int n = 0; n < taps; ++n) {
        int src = ((n - taps / 2 - shift) % taps + taps) % taps;   // rotate so peak sits at taps/2
        double w = 0.5 - 0.5 * std::cos(2 * M_PI * n / double(taps));   // Hann
        h[n] = float(H[src].real() * w);
    }
    return h;
}

// Minimum-phase FIR: same magnitude as designLinearPhase but no pre-ringing and ~0 latency (cepstral method).
// taps must be a power of two. The response is designed on a 4x longer FFT and truncated with a short fade-out.
inline std::vector<float> designMinPhase(const Profile& p, int ch, double sampleRate, int taps, double pre, const DesignOptions& o = {}) {
    const size_t N = size_t(taps) * 4;
    std::vector<std::complex<double>> c(N);
    for (size_t k = 0; k <= N / 2; ++k) {
        double f = std::max(1.0, double(k) * sampleRate / double(N));
        double lg = std::log(std::pow(10.0, (targetDb(p, ch, f, o) + pre) / 20.0));
        c[k] = lg;
        if (k > 0 && k < N / 2) c[N - k] = lg;
    }
    fftInPlace(c, true);                                   // real cepstrum
    for (size_t n = 1; n < N / 2; ++n) c[n] *= 2.0;        // fold onto the causal part
    for (size_t n = N / 2 + 1; n < N; ++n) c[n] = 0;
    fftInPlace(c, false);
    for (auto& x : c) x = std::exp(x);                     // exp(log-mag + j*phase)
    fftInPlace(c, true);
    const int shift = std::clamp(int(std::lround(p.delayMs(ch) * sampleRate / 1000.0)), 0, taps / 4);
    std::vector<float> h(size_t(taps), 0.0f);
    const int fade = taps / 8;
    for (int n = shift; n < taps; ++n) {
        double w = n < taps - fade ? 1.0 : 0.5 + 0.5 * std::cos(M_PI * double(n - (taps - fade)) / double(fade));
        h[size_t(n)] = float(c[size_t(n - shift)].real() * w);
    }
    return h;
}

// ---------------------------------------------------------------- low-latency IIR
struct Biquad { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };   // a0 normalised to 1

inline Biquad peaking(double fs, double f0, double gainDb, double q) {
    double A = std::pow(10.0, gainDb / 40.0), w = 2 * M_PI * f0 / fs, al = std::sin(w) / (2 * q), c = std::cos(w);
    double a0 = 1 + al / A;
    return { (1 + al * A) / a0, -2 * c / a0, (1 - al * A) / a0, -2 * c / a0, (1 - al / A) / a0 };
}
inline double biquadDb(const Biquad& b, double fs, double f) {
    double w = 2 * M_PI * f / fs;
    std::complex<double> z1 = std::polar(1.0, -w), z2 = z1 * z1;
    return 20 * std::log10(std::abs((b.b0 + b.b1 * z1 + b.b2 * z2) / (1.0 + b.a1 * z1 + b.a2 * z2)));
}

struct IirDesign {
    std::vector<double> freqs, gains;     // per band
    std::vector<Biquad> sections;
    double preampDb = 0, maxErrDb = 0;    // max |target - fit| over 20 Hz..min(20k, 0.45 fs)
    double q = 3.0;
};

inline IirDesign designLowLatency(const Profile& p, int ch, double fs, double pre, const DesignOptions& o = {}, int bandsPerOctave = 6) {
    IirDesign d; d.preampDb = pre;
    const double fHi = std::min(20000.0, 0.45 * fs), fLo = 20.0;
    std::vector<double> grid, tgt;
    for (int i = 0; i < 600; ++i) { double f = fLo * std::pow(fHi / fLo, i / 599.0); grid.push_back(f); tgt.push_back(targetDb(p, ch, f, o) + pre); }
    auto measure = [&] {
        for (size_t i = 0; i < grid.size(); ++i) {
            double r = 0; for (auto& b : d.sections) r += biquadDb(b, fs, grid[i]);
            d.maxErrDb = std::max(d.maxErrDb, std::abs(tgt[i] - r));
        }
    };

    bool unmodified = p.isParametric(ch) && o.smoothingOct <= 0 && o.bassLimitHz <= 0;
    for (size_t i = 0; unmodified && i < grid.size(); ++i)             // boost limits would change the response too
        if (std::abs(p.correctionDb(ch, grid[i]) - targetDb(p, ch, grid[i], o)) > 1e-9) unmodified = false;
    if (unmodified) {   // parametric profile: bands map exactly onto biquads
        for (auto& b : p.bands(ch))
            if (b.gainDb != 0.0 && b.freq < 0.499 * fs) {
                d.freqs.push_back(b.freq); d.gains.push_back(b.gainDb); d.q = b.q;
                d.sections.push_back(peaking(fs, b.freq, b.gainDb, b.q));
            }
        double g = std::pow(10.0, (pre + p.channelGainDb(ch)) / 20.0);
        d.sections.push_back({ g, 0, 0, 0, 0 });
        measure();
        return d;
    }

    // curve profile: fit a bank of peaking filters (the response in dB of a cascade is the sum of the bands)
    // extra bands above the fit range keep the top end from rolling off (bilinear warping near Nyquist)
    for (double f = fLo; f <= std::min(0.47 * fs, fHi * 1.6); f *= std::pow(2.0, 1.0 / bandsPerOctave)) d.freqs.push_back(f);
    d.q = 1.0 / (2.0 * std::sinh(std::log(2.0) / 2.0 * (1.0 / bandsPerOctave) * 2.0));
    const int nb = int(d.freqs.size());
    d.gains.assign(nb, 0.0);
    std::vector<Biquad> sec(nb);
    auto resp = [&](double f) { double s = 0; for (int b = 0; b < nb; ++b) s += biquadDb(sec[b], fs, f); return s; };
    for (int it = 0; it < 200; ++it) {
        for (int b = 0; b < nb; ++b) sec[b] = peaking(fs, d.freqs[b], d.gains[b], d.q);
        std::vector<double> err(grid.size());
        for (size_t i = 0; i < grid.size(); ++i) err[i] = tgt[i] - resp(grid[i]);
        for (int b = 0; b < nb; ++b) {          // triangular-weighted mean error around band centre
            double sw = 0, se = 0;
            for (size_t i = 0; i < grid.size(); ++i) {
                double oct = std::abs(std::log2(grid[i] / d.freqs[b])) * bandsPerOctave;
                double w = std::max(0.0, 1.0 - oct); sw += w; se += w * err[i];
            }
            if (sw > 0) d.gains[b] += 0.6 * se / sw;
        }
    }
    for (int b = 0; b < nb; ++b) sec[b] = peaking(fs, d.freqs[b], d.gains[b], d.q);
    d.sections = sec;
    measure();
    return d;
}

} // namespace flatq
