// Filter design from an LvlsProfile curve. Pure C++17 (no JUCE) so it is unit-testable.
//   designLinearPhase : symmetric FIR, latency = (taps-1)/2 samples, magnitude-exact, zero phase distortion
//   designLowLatency  : cascade of peaking biquads (min-phase IIR), ~0 latency, approximate magnitude
#pragma once
#include "Profile.h"
#include <algorithm>
#include <cmath>
#include <complex>

#ifndef M_PI   // MSVC does not define it by default
#define M_PI 3.14159265358979323846
#endif

namespace flatq {

struct DesignOptions {
    bool   clampToMaxBoost = true;   // limit boosts: global maxBoostDb and the profile's own per-frequency ceiling
    double maxBoostDb      = 12.0;   // global boost limit (dB), applies to every profile
    double smoothingOct    = 0.0;    // fractional-octave smoothing width of the target (0 = off, 1/3 = third-octave)
    double bassLimitHz     = 0.0;    // 0 = off. Correction fades out between bassLimitHz/2 and bassLimitHz and is 0 dB below
    bool   autoPreamp      = true;   // lower overall gain so the EQ never boosts above 0 dB
    double extraGainDb     = 0.0;    // user trim
};

// Correction before smoothing/limits: profile response with the bass fade-out applied.
inline double rawTargetDb(const Profile& p, int ch, double f, const DesignOptions& o) {
    double g = p.correctionDb(ch, f);
    if (o.bassLimitHz > 0 && f < o.bassLimitHz) {
        double t = std::clamp(std::log2(f / (o.bassLimitHz * 0.5)), 0.0, 1.0);     // 0 at limit/2 ... 1 at limit
        g *= 0.5 - 0.5 * std::cos(M_PI * t);                                         // raised-cosine fade
    }
    return g;
}

// Target response in dB at f for channel ch, after bass fade, smoothing and boost limits. (preamp not included)
inline double targetDb(const Profile& p, int ch, double f, const DesignOptions& o) {
    double g;
    if (o.smoothingOct > 0) {                       // Gaussian smoothing in log-frequency, sigma = width / 2.355 octaves
        const double sigma = o.smoothingOct / 2.355;
        double sum = 0, wsum = 0;
        for (int k = -9; k <= 9; ++k) {
            double x = k / 3.0 * sigma, w = std::exp(-0.5 * (x / sigma) * (x / sigma));
            sum += w * rawTargetDb(p, ch, f * std::pow(2.0, x), o); wsum += w;
        }
        g = sum / wsum;
    } else g = rawTargetDb(p, ch, f, o);
    if (o.clampToMaxBoost) g = std::min({ g, o.maxBoostDb, p.ceilingDb(f) });
    return g;
}

// One preamp for all channels (so L/R balance is preserved): minus the highest boost anywhere.
inline double preampDb(const Profile& p, const DesignOptions& o) {
    double peak = 0;
    for (int ch = 0; ch < p.channels(); ++ch)
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
