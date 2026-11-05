# flatQ

A calibration EQ plugin (VST3 and AU) that applies headphone or speaker correction profiles to your audio output. Put it on the master bus of your DAW.

- **Three filter types:** Linear Phase (no phase shift, most latency), Mixed (about 10 ms) and Zero Latency, all FIR with a selectable resolution (4096 to 65536 taps).
- **Per-channel profiles:** independent left/right responses, delays and gains.
- **Boost limits:** a global max-boost setting plus a profile's own per-frequency ceiling, with optional auto preamp.
- **Response graph:** target, actual filter response and boost limit.
- **Skip on export:** passes audio through untouched during offline renders (e.g. Cubase *Export Audio Mixdown*).
- **Profiles:** built-in presets, plus load your own `.flatq`, Sonarworks `.lvls` / `.lvlsproj`, or RME TotalMix Room EQ `.tmreq` files.

## Build

Requires Xcode command line tools, CMake and Ninja (JUCE is fetched automatically).

```sh
make setup      # install CMake + Ninja via Homebrew
make build      # builds VST3 and AU, then runs tests
make install    # ad-hoc signs and installs to ~/Library/Audio/Plug-Ins
make run        # open the standalone app
make validate   # run Apple's auval on the AU
```

Output goes to `build/FlatQ_artefacts/Release/`. Use `make universal` for an arm64 + x86_64 build.

CI (`.github/workflows/`) builds macOS, Windows and Linux on every push; pushing a `v*` tag creates a release with the VST3 and AU zips.

## Using it

1. Insert flatQ as the last effect on your master / stereo out.
2. Pick a profile from the dropdown, or use **Load file...**.
3. Choose **Zero Latency** for recording and monitoring, **Mixed** for mixing, or **Linear Phase** for critical listening and mastering. Linear Phase at 16384 taps reports about 170 ms of latency at 48 kHz, which the host compensates for.

Controls: max boost (dB), respect max boost, auto preamp, output trim, bypass, skip on export.

## Profile format (`.flatq`)

A small text file; `#` starts a comment.

```
flatq 1
name My Headphones
channel LR                    # L, R, or LR (same on both channels)
  delay_ms 0.07               # optional
  gain_db 0.1                 # optional
  curve log 355 20 22000      # N gain values (dB) on a log grid from 20 to 22000 Hz ...
    1.5 1.4 1.2 ...
  end                         # ... or "curve points N" followed by freq/gain pairs
  band 1000 2.0 -3.5          # parametric peak: Hz, Q, dB (any number)
ceiling log 355 20 22000      # optional max-boost limit (dB) per frequency
  ...
end
```

Convert other formats:

```sh
build/flatq_convert input.lvlsproj output.flatq --name "My Speakers"
```

The converter re-reads its output and reports the maximum response error.

Built-in presets are every `.flatq` under `profiles/`. Personal profiles go in `profiles/local/` and original source files in `sources/`; both are git-ignored.

## Layout

| Path | Contents |
| --- | --- |
| `src/` | plugin (JUCE) and DSP/profile code; `FlatqProfile.h`, `EqDesign.h` and the importers have no JUCE dependency |
| `tools/` | `flatq_convert`, `design_test`, `lvls_dump` |
| `profiles/` | built-in `.flatq` profiles |
| `scripts/` | build, install, run and validate helpers |
