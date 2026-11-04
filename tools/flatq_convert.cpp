// flatq_convert <input.(lvls|lvlsproj|tmreq|flatq)> <output.flatq> [--name "Display name"]
// Converts a profile to the native flatQ format and verifies the result reproduces the source response.
#include "../src/Profile.h"
#include <cstdio>
#include <fstream>
using namespace flatq;
int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: %s input output.flatq [--name NAME]\n", argv[0]); return 2; }
    std::string err, name;
    for (int i = 3; i + 1 < argc; ++i) if (std::string(argv[i]) == "--name") name = argv[i + 1];
    auto src = Profile::load(argv[1], &err);
    if (!src) { std::fprintf(stderr, "%s: %s\n", argv[1], err.c_str()); return 1; }
    FlatqProfile out = src->d; if (!name.empty()) out.name = name;
    const std::string text = out.serialize();
    { std::ofstream f(argv[2], std::ios::binary); f << text; if (!f) { std::fprintf(stderr, "cannot write %s\n", argv[2]); return 1; } }

    // verify: reload and compare responses, delays, gains, ceiling
    auto back = Profile::load(argv[2], &err);
    if (!back) { std::fprintf(stderr, "re-read failed: %s\n", err.c_str()); return 1; }
    double maxErr = 0, ceilErr = 0;
    if (back->channels() != src->channels() && !(src->channels() == 2 && back->channels() == 1)) { std::fprintf(stderr, "channel layout changed\n"); return 1; }
    for (int ch = 0; ch < 2; ++ch) {
        const int sc = std::min(ch, src->channels() - 1), bc = std::min(ch, back->channels() - 1);
        for (int i = 0; i < 2000; ++i) {
            double f = 20.0 * std::pow(1100.0, i / 1999.0);
            maxErr = std::max(maxErr, std::abs(src->correctionDb(sc, f) - back->correctionDb(bc, f)));
            if (src->hasCeiling()) ceilErr = std::max(ceilErr, std::abs(src->ceilingDb(f) - back->ceilingDb(f)));
        }
        if (std::abs(src->delayMs(sc) - back->delayMs(bc)) > 1e-6) { std::fprintf(stderr, "delay mismatch\n"); return 1; }
    }
    const bool ok = maxErr < 1e-4 && ceilErr < 1e-4 && src->hasCeiling() == back->hasCeiling();
    std::printf("%s -> %s\n  %zu bytes, %d channel response%s, ceiling %s\n  max response error %.2e dB, ceiling error %.2e dB  %s\n",
                argv[1], argv[2], text.size(), back->channels(), back->channels() > 1 ? "s" : "", back->hasCeiling() ? "yes" : "no",
                maxErr, ceilErr, ok ? "OK" : "MISMATCH");
    for (auto& w : src->d.warnings) std::printf("  warning: %s\n", w.c_str());
    return ok ? 0 : 1;
}
