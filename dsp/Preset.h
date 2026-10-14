// Preset files: small JSON documents shared by the plugin, the render tool
// and the analysis scripts.
//
// {
//   "name": "White-throated Sparrow",
//   "beakType": "conical",
//   "params": { "octave": -2, "purity": 0.79, "scoopDir": "Auto", ... }
// }
//
// Parameters left out keep their Init defaults. Choice parameters accept
// either the index or the label.
#pragma once

#include "Params.h"

#include <string>

namespace pbv
{

struct Preset
{
    std::string name = "Init";
    std::string beakType = "conical";
    ParamValues values;
};

// Parses preset JSON. On failure returns false and describes the problem in `error`.
bool parsePreset (const std::string& json, Preset& out, std::string& error);

// Sets one parameter from text ("0.5", "Auto", "true"). Returns false if the id or value is unknown.
bool setParamFromText (ParamValues& values, const std::string& id, const std::string& text);

std::string presetToJson (const Preset& preset);

} // namespace pbv
