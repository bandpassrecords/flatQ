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
                auto iir = designLowLatency(*p, ch, fs, pre, o);
                std::printf("  fs=%6.0f ch%d  FIR(16384) maxErr(40Hz-18k)=%.2f dB | IIR sections=%zu maxErr=%.2f dB\n",
                            fs, ch, firErr, iir.sections.size(), iir.maxErrDb);
                if (firErr > 3.0 || iir.maxErrDb > 6.0) ++bad;
            }
        }
    }
    return bad ? 1 : 0;
}
