// pbv_render — plays a score through the bird voice engine into a WAV file.
//
// Usage:
//   pbv_render --preset presets/white_throated_sparrow.json --score song.txt --out song.wav
//              [--set id=value ...] [--sr 48000] [--tail 1.5] [--seed 1] [--mono] [--dump-preset]
//
// Score format: one event per line, times in seconds, '#' starts a comment.
//   0.000 on 79 100      key 79, velocity 100
//   0.600 off 79
//   0.000 cc 2 90        controller 2 (breath) = 90
//   0.000 bend 8192      pitch bend, 0..16383
//   0.000 at 64          channel aftertouch
//
// The engine code is the same code the plugin runs, so a render here sounds
// like the plugin in Cubase with the same settings.

#include "BirdEngine.h"
#include "Preset.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    struct Event
    {
        double time;
        int order; // keeps file order for events at the same time
        std::string type;
        int a = 0, b = 0;
    };

    bool readFile (const std::string& path, std::string& out)
    {
        std::ifstream f (path, std::ios::binary);
        if (! f) return false;
        std::stringstream ss;
        ss << f.rdbuf();
        out = ss.str();
        return true;
    }

    bool parseScore (const std::string& text, std::vector<Event>& events, std::string& error)
    {
        std::istringstream in (text);
        std::string line;
        int lineNo = 0;
        while (std::getline (in, line))
        {
            ++lineNo;
            if (auto hash = line.find ('#'); hash != std::string::npos) line.erase (hash);
            std::istringstream ls (line);
            Event e {};
            if (! (ls >> e.time)) continue; // blank line
            if (! (ls >> e.type)) { error = "line " + std::to_string (lineNo) + ": missing event type"; return false; }
            const int need = (e.type == "on" || e.type == "cc") ? 2 : 1;
            if (! (ls >> e.a) || (need == 2 && ! (ls >> e.b)))
            {
                error = "line " + std::to_string (lineNo) + ": missing values for '" + e.type + "'";
                return false;
            }
            if (e.type != "on" && e.type != "off" && e.type != "cc" && e.type != "bend" && e.type != "at")
            {
                error = "line " + std::to_string (lineNo) + ": unknown event '" + e.type + "'";
                return false;
            }
            e.order = (int) events.size();
            events.push_back (e);
        }
        std::stable_sort (events.begin(), events.end(), [] (const Event& x, const Event& y) { return x.time < y.time; });
        return true;
    }

    void put32 (std::ofstream& f, uint32_t v) { f.write (reinterpret_cast<const char*> (&v), 4); }
    void put16 (std::ofstream& f, uint16_t v) { f.write (reinterpret_cast<const char*> (&v), 2); }

    bool writeWavFloat (const std::string& path, const std::vector<float>& interleaved, int channels, int sampleRate)
    {
        std::ofstream f (path, std::ios::binary);
        if (! f) return false;
        const uint32_t dataBytes = (uint32_t) (interleaved.size() * sizeof (float));
        f.write ("RIFF", 4); put32 (f, 36 + dataBytes); f.write ("WAVE", 4);
        f.write ("fmt ", 4); put32 (f, 16); put16 (f, 3 /* IEEE float */); put16 (f, (uint16_t) channels);
        put32 (f, (uint32_t) sampleRate); put32 (f, (uint32_t) (sampleRate * channels * 4));
        put16 (f, (uint16_t) (channels * 4)); put16 (f, 32);
        f.write ("data", 4); put32 (f, dataBytes);
        f.write (reinterpret_cast<const char*> (interleaved.data()), dataBytes);
        return (bool) f;
    }

    void usage()
    {
        std::cerr << "usage: pbv_render --score FILE --out FILE.wav [--preset FILE.json] [--set id=value]...\n"
                     "                  [--sr 48000] [--tail 1.5] [--seed 1] [--mono]\n"
                     "       pbv_render --preset FILE.json --dump-preset\n";
    }
}

int main (int argc, char** argv)
{
    std::string presetPath, scorePath, outPath;
    std::vector<std::string> sets;
    int sampleRate = 48000;
    double tail = 1.5;
    uint32_t seed = 1;
    bool mono = false, dump = false;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        auto next = [&] () -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (arg == "--preset") presetPath = next();
        else if (arg == "--score") scorePath = next();
        else if (arg == "--out") outPath = next();
        else if (arg == "--set") sets.push_back (next());
        else if (arg == "--sr") sampleRate = std::atoi (next().c_str());
        else if (arg == "--tail") tail = std::atof (next().c_str());
        else if (arg == "--seed") seed = (uint32_t) std::strtoul (next().c_str(), nullptr, 10);
        else if (arg == "--mono") mono = true;
        else if (arg == "--dump-preset") dump = true;
        else { usage(); return 2; }
    }

    pbv::Preset preset;
    if (! presetPath.empty())
    {
        std::string json, error;
        if (! readFile (presetPath, json)) { std::cerr << "cannot read preset " << presetPath << "\n"; return 1; }
        if (! pbv::parsePreset (json, preset, error)) { std::cerr << presetPath << ": " << error << "\n"; return 1; }
    }
    for (const auto& s : sets)
    {
        const auto eq = s.find ('=');
        if (eq == std::string::npos || ! pbv::setParamFromText (preset.values, s.substr (0, eq), s.substr (eq + 1)))
        {
            std::cerr << "bad --set " << s << "\n";
            return 1;
        }
    }

    if (dump) { std::cout << pbv::presetToJson (preset); return 0; }
    if (scorePath.empty() || outPath.empty()) { usage(); return 2; }

    std::string scoreText, error;
    std::vector<Event> events;
    if (! readFile (scorePath, scoreText)) { std::cerr << "cannot read score " << scorePath << "\n"; return 1; }
    if (! parseScore (scoreText, events, error)) { std::cerr << scorePath << ": " << error << "\n"; return 1; }

    pbv::BirdEngine engine;
    constexpr int kBlock = 64;
    engine.setParams (preset.values);
    engine.prepare (sampleRate, kBlock);
    engine.setSeed (seed);

    const double end = (events.empty() ? 0.0 : events.back().time) + tail;
    const int64_t total = (int64_t) (end * sampleRate);
    std::vector<float> L ((size_t) total), R ((size_t) total);

    size_t next = 0;
    int64_t pos = 0;
    while (pos < total)
    {
        // Apply all events due at or before this sample, then render up to the next one.
        while (next < events.size() && (int64_t) (events[next].time * sampleRate) <= pos)
        {
            const Event& e = events[next++];
            if (e.type == "on") engine.noteOn (e.a, e.b);
            else if (e.type == "off") engine.noteOff (e.a);
            else if (e.type == "cc") engine.controller (e.a, e.b);
            else if (e.type == "bend") engine.pitchBend (e.a);
            else if (e.type == "at") engine.channelPressure (e.a);
        }
        int64_t until = std::min<int64_t> (pos + kBlock, total);
        if (next < events.size()) until = std::min<int64_t> (until, std::max<int64_t> (pos + 1, (int64_t) (events[next].time * sampleRate)));
        engine.process (L.data() + pos, R.data() + pos, (int) (until - pos));
        pos = until;
    }

    std::vector<float> out;
    if (mono)
    {
        out.resize ((size_t) total);
        for (int64_t i = 0; i < total; ++i) out[(size_t) i] = 0.5f * (L[(size_t) i] + R[(size_t) i]);
    }
    else
    {
        out.resize ((size_t) total * 2);
        for (int64_t i = 0; i < total; ++i) { out[(size_t) (2 * i)] = L[(size_t) i]; out[(size_t) (2 * i + 1)] = R[(size_t) i]; }
    }

    if (! writeWavFloat (outPath, out, mono ? 1 : 2, sampleRate)) { std::cerr << "cannot write " << outPath << "\n"; return 1; }
    return 0;
}
