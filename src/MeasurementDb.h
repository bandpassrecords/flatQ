// The AutoEq measurement pack embedded in the plugin (see tools/build_measurement_db.py): an index of every headphone /
// IEM measurement plus its curve on a 480-point log grid. Built only when data/measurements/ exists (personal use: the
// measurements keep their own licences), otherwise available() is false and the plugin hides the search.
#pragma once
#include "AutoEqImport.h"
#include <juce_core/juce_core.h>
#include <map>
#if FLATQ_HAS_MEASUREMENTS
 #include <MeasurementData.h>
#endif

class MeasurementDb
{
public:
    struct Entry { juce::String source, form, rig, brand, name, model, haystack; bool preEq = false; };   // preEq: an AutoEq result, not a raw measurement

    static MeasurementDb& get() { static MeasurementDb db; return db; }
    bool available() const { return ! entries.empty(); }
    int size() const { return (int) entries.size(); }
    const Entry& entry (int i) const { return entries[(size_t) i]; }

    // Entries matching every word of `query` (name, brand, source or rig), optionally of one form (over-ear / in-ear / earbud).
    // Sorted by name, then by how much the source is trusted. At most maxResults.
    struct Brand { juce::String name; int count = 0; };
    const std::vector<Brand>& brands() const { return brandList; }   // sorted by name

    std::vector<int> search (const juce::String& query, const juce::String& form, const juce::String& brand, int maxResults) const
    {
        const auto words = juce::StringArray::fromTokens (query.toLowerCase().trim(), " ", "");
        std::vector<int> hits;
        for (int i = 0; i < size(); ++i)
        {
            const auto& e = entries[(size_t) i];
            if (form.isNotEmpty() && e.form != form) continue;
            if (brand.isNotEmpty() && e.brand != brand) continue;
            bool all = true;
            for (auto& w : words) if (! e.haystack.contains (w)) { all = false; break; }
            if (all) hits.push_back (i);
        }
        std::stable_sort (hits.begin(), hits.end(), [this] (int a, int b)
        {
            const auto &x = entries[(size_t) a], &y = entries[(size_t) b];
            if (const int c = x.name.compareIgnoreCase (y.name)) return c < 0;
            return rank (x.source) < rank (y.source);
        });
        if ((int) hits.size() > maxResults) hits.resize ((size_t) maxResults);
        return hits;
    }

    // The measurement as a curve (dB) on the pack's log grid. Decompresses the pack on first use.
    bool curve (int i, flatq::CsvCurve& out)
    {
        loadCurves();
        if (i < 0 || i >= size() || curves.size() < (size_t) (i + 1) * kPoints) return false;
        out.freq.resize (kPoints); out.db.resize (kPoints);
        for (int k = 0; k < kPoints; ++k)
        {
            out.freq[(size_t) k] = 20.0 * std::pow (1000.0, (double) k / (kPoints - 1));
            out.db[(size_t) k] = curves[(size_t) i * kPoints + (size_t) k] / 100.0;
        }
        return true;
    }

private:
    static constexpr int kPoints = 480;
    std::vector<Entry> entries;
    std::vector<Brand> brandList;
    std::vector<int16_t> curves;
    bool curvesLoaded = false;

    static int rank (const juce::String& source)   // lower = more trusted when several sources measured the same model
    {
        static const char* order[] = { "oratory1990", "Innerfidelity", "Rtings", "Headphone.com Legacy", "Super Review", "HypetheSonics" };
        for (int i = 0; i < 6; ++i) if (source == order[i]) return i;
        return 6;
    }

    MeasurementDb()
    {
       #if FLATQ_HAS_MEASUREMENTS
        const auto text = juce::String::fromUTF8 (FlatQMeasurementData::measurements_tsv, FlatQMeasurementData::measurements_tsvSize);
        for (auto& line : juce::StringArray::fromLines (text))
        {
            auto f = juce::StringArray::fromTokens (line, "\t", "");
            if (f.size() < 6) continue;
            Entry e { f[0], f[1], f[2], f[3], f[4], f[5], {}, f.size() > 6 && f[6] == "eq" };
            e.haystack = (e.name + " " + e.brand + " " + e.source + " " + e.rig).toLowerCase();
            entries.push_back (std::move (e));
        }
        std::map<juce::String, int> counts;
        for (auto& e : entries) if (e.brand.isNotEmpty()) ++counts[e.brand];
        for (auto& kv : counts) brandList.push_back ({ kv.first, kv.second });
        std::sort (brandList.begin(), brandList.end(), [] (const Brand& a, const Brand& b) { return a.name.compareIgnoreCase (b.name) < 0; });
       #endif
    }

    void loadCurves()
    {
        if (curvesLoaded) return;
        curvesLoaded = true;
       #if FLATQ_HAS_MEASUREMENTS
        juce::MemoryInputStream in (FlatQMeasurementData::measurements_bin_gz, (size_t) FlatQMeasurementData::measurements_bin_gzSize, false);
        juce::GZIPDecompressorInputStream gz (&in, false, juce::GZIPDecompressorInputStream::gzipFormat);
        juce::MemoryBlock raw;
        gz.readIntoMemoryBlock (raw);
        curves.resize (raw.getSize() / sizeof (int16_t));
        std::memcpy (curves.data(), raw.getData(), curves.size() * sizeof (int16_t));   // little-endian pack, little-endian host
       #endif
    }
};
