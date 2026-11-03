// Designs both filter types for a profile at several sample rates and reports the worst-case error vs target.
// IIR limit is looser: wide HF bands near Nyquist are warped by the bilinear transform (inherent to biquads).
// Exit code 1 if an error limit is exceeded, so it doubles as a CI test.
#include "../src/EqDesign.h"
#include <cstdio>
using namespace flatq;
int main(int argc, char** argv) {
    int bad = 0;
    for (int a = 1; a < argc; ++a) {
        std::string err; auto p = Profile::load(argv[a], &err);
        if (!p) { std::fprintf(stderr, "%s: %s\n", argv[a], err.c_str()); return 1; }
        std::printf("%s (%d channel response%s)\n", p->name.c_str(), p->channels(), p->channels() > 1 ? "s" : "");
        DesignOptions o; double pre = preampDb(*p, o);
        std::printf("  preamp %.2f dB\n", pre);
        for (double fs : {44100., 48000., 96000.}) {
            for (int ch = 0; ch < p->channels(); ++ch) {
                auto fir = designLinearPhase(*p, ch, fs, 16384, pre, o);
                double firErr = 0;
                for (double f = 40; f < std::min(18000., .45 * fs); f *= 1.02) {
                    std::complex<double> s = 0;
                    for (size_t n = 0; n < fir.size(); ++n) s += double(fir[n]) * std::polar(1.0, -2 * M_PI * f / fs * n);
                    firErr = std::max(firErr, std::abs(20 * std::log10(std::abs(s)) - (targetDb(*p, ch, f, o) + pre)));
                }
                auto mp = designMinPhase(*p, ch, fs, 16384, pre, o);
                double mpErr = 0;
                for (double f = 40; f < std::min(18000., .45 * fs); f *= 1.02) {
                    std::complex<double> s = 0;
                    for (size_t n = 0; n < mp.size(); ++n) s += double(mp[n]) * std::polar(1.0, -2 * M_PI * f / fs * n);
                    mpErr = std::max(mpErr, std::abs(20 * std::log10(std::abs(s)) - (targetDb(*p, ch, f, o) + pre)));
                }
                std::printf("  fs=%6.0f ch%d  MinPhase(16384) maxErr=%.2f dB\n", fs, ch, mpErr);
                if (mpErr > 3.0) ++bad;
                auto iir = designLowLatency(*p, ch, fs, pre, o);
                std::printf("  fs=%6.0f ch%d  FIR(16384) maxErr(40Hz-18k)=%.2f dB | IIR sections=%zu maxErr=%.2f dB\n",
                            fs, ch, firErr, iir.sections.size(), iir.maxErrDb);
                if (firErr > 3.0 || iir.maxErrDb > 6.0) ++bad;
            }
        }
    }
    {   // slope limiter: a narrow 10 dB spike must be cut, a broad 6 dB shelf (about 1.5 dB/oct) must survive 6 dB/oct
        std::vector<double> f, spike, shelf;
        for (int i = 0; i < 400; ++i) { f.push_back(20 * std::pow(1000.0, i / 399.0)); spike.push_back(i == 200 ? 10 : 0); shelf.push_back(i > 200 ? 6 : 0); }
        auto s2 = spike, h2 = shelf; limitSlope(f, s2, 6); limitSlope(f, h2, 6);
        double sp = *std::max_element(s2.begin(), s2.end()), sh = *std::max_element(h2.begin(), h2.end());
        std::printf("slope limiter: spike 10 -> %.2f dB, step 6 -> %.2f dB\n", sp, sh);
        if (sp > 1.0) ++bad;
    }
    if (argc > 1) {   // manual per-band trim: +6 dB at 1 kHz must show up in the target
        auto p = Profile::load(argv[1]); DesignOptions a, b; b.manualDb[0].assign(manualFreqs().size(), 0.0);
        b.manualDb[0][17] = 6.0;   // 1 kHz
        double d = targetDb(*p, 0, 1000, b) - targetDb(*p, 0, 1000, a), far = std::abs(targetDb(*p, 0, 100, b) - targetDb(*p, 0, 100, a));
        std::printf("manual trim: +6 dB at 1 kHz -> %+.2f dB there, %.2f dB at 100 Hz\n", d, far);
        if (std::abs(d - 6.0) > 1e-6 || far > 1e-6) ++bad;
    }
    if (argc > 1) {   // bass limit: no boost below the limit, whatever the correction asks for
        auto p = Profile::load(argv[1]); DesignOptions o; o.bassLimitHz = 50;
        double worst = -1e9, rawMax = -1e9;
        for (double f = 20; f < 50; f *= 1.03) { worst = std::max(worst, targetDb(*p, 0, f, o)); rawMax = std::max(rawMax, p->correctionDb(0, f)); }
        std::printf("bass limit 50 Hz: max target below it %+.2f dB (raw correction up to %+.1f dB)\n", worst, rawMax);
        if (rawMax > 1 && worst > 1e-6) ++bad;
    }
    return bad ? 1 : 0;
}
