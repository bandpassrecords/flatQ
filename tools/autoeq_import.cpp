// autoeq_import <measurement.csv> <target.csv> <output.flatq> [--name NAME]
// Builds a FlatQ profile from an AutoEq measurement and target (correction = target - measurement, both centred at 1 kHz).
#include "../src/AutoEqImport.h"
#include <cstdio>
#include <fstream>
static std::string slurp(const char* p) { std::ifstream f(p, std::ios::binary); return std::string((std::istreambuf_iterator<char>(f)), {}); }
int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: %s measurement.csv target.csv output.flatq [--name NAME]\n", argv[0]); return 2; }
    std::string name = "AutoEq import", err;
    for (int i = 4; i + 1 < argc; ++i) if (std::string(argv[i]) == "--name") name = argv[i + 1];
    auto p = flatq::buildAutoEqProfile(slurp(argv[1]), slurp(argv[2]), name, &err);
    if (!p) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    std::ofstream(argv[3], std::ios::binary) << p->serialize();
    std::printf("wrote %s (%zu points)\n", argv[3], p->ch[0].freq.size());
    return 0;
}
