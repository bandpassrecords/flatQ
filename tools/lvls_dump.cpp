#include "../src/LvlsProfile.h"
#include <cstdio>
int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string err; auto p = flatq::LvlsProfile::load(argv[i], &err);
        if (!p) { std::fprintf(stderr, "%s: %s\n", argv[i], err.c_str()); return 1; }
        std::printf("%s\n  %s | %s | %s\n  curves=%zu points=%zu errorBand=%.1f dB\n  peak boost=%.2f dB  safe preamp=%.2f dB\n",
            argv[i], p->manufacturer().c_str(), p->model().c_str(), p->mode().c_str(),
            p->curves.size(), p->correction().points.size(), p->errorBandDb(), p->peakBoostDb(), p->safePreampDb());
        for (double f : {20., 100., 1000., 3000., 10000., 20000.})
            std::printf("  %6.0f Hz: corr %+6.2f dB  maxBoost %5.2f dB\n", f, p->correction().gainAt(f), p->maxBoost().gainAt(f));
    }
}
