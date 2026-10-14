#include "Preset.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace pbv
{

int findParam (const char* id)
{
    for (int i = 0; i < kNumParams; ++i)
        if (std::strcmp (paramDef (i).id, id) == 0)
            return i;
    return -1;
}

namespace
{
    int choiceIndex (const ParamDef& def, const std::string& label)
    {
        std::string all = def.choices;
        int index = 0;
        size_t start = 0;
        while (start <= all.size())
        {
            const size_t end = std::min (all.find ('|', start), all.size());
            if (all.compare (start, end - start, label) == 0) return index;
            start = end + 1;
            ++index;
        }
        return -1;
    }

    std::string choiceLabel (const ParamDef& def, int index)
    {
        std::string all = def.choices;
        size_t start = 0;
        for (int i = 0; i < index; ++i)
        {
            start = all.find ('|', start);
            if (start == std::string::npos) return {};
            ++start;
        }
        return all.substr (start, all.find ('|', start) - start);
    }

    // A deliberately small JSON reader: objects, strings, numbers, booleans.
    struct Reader
    {
        const std::string& s;
        size_t i = 0;
        std::string error;

        void skip() { while (i < s.size() && std::isspace ((unsigned char) s[i])) ++i; }
        bool expect (char c)
        {
            skip();
            if (i < s.size() && s[i] == c) { ++i; return true; }
            error = std::string ("expected '") + c + "' at offset " + std::to_string (i);
            return false;
        }
        bool peek (char c) { skip(); return i < s.size() && s[i] == c; }

        // Skips any value, including nested objects and arrays.
        bool skipValue()
        {
            skip();
            if (peek ('{') || peek ('['))
            {
                int depth = 0;
                bool inString = false;
                for (; i < s.size(); ++i)
                {
                    const char c = s[i];
                    if (inString) { if (c == '\\') ++i; else if (c == '"') inString = false; continue; }
                    if (c == '"') inString = true;
                    else if (c == '{' || c == '[') ++depth;
                    else if ((c == '}' || c == ']') && --depth == 0) { ++i; return true; }
                }
                error = "unterminated object or array";
                return false;
            }
            std::string ignored;
            return readScalar (ignored);
        }

        bool readString (std::string& out)
        {
            if (! expect ('"')) return false;
            out.clear();
            while (i < s.size() && s[i] != '"')
            {
                if (s[i] == '\\' && i + 1 < s.size()) ++i;
                out += s[i++];
            }
            return expect ('"');
        }

        // Reads a scalar value as text; strings come back unquoted.
        bool readScalar (std::string& out)
        {
            skip();
            if (peek ('"')) return readString (out);
            const size_t start = i;
            while (i < s.size() && (std::isalnum ((unsigned char) s[i]) || std::strchr ("+-.", s[i]) != nullptr)) ++i;
            if (i == start) { error = "expected a value at offset " + std::to_string (i); return false; }
            out = s.substr (start, i - start);
            return true;
        }
    };
}

bool setParamFromText (ParamValues& values, const std::string& id, const std::string& text)
{
    const int index = findParam (id.c_str());
    if (index < 0) return false;
    const ParamDef& def = paramDef (index);

    float v = 0.0f;
    if (text == "true") v = 1.0f;
    else if (text == "false") v = 0.0f;
    else
    {
        char* end = nullptr;
        v = std::strtof (text.c_str(), &end);
        if (end == text.c_str() || *end != '\0')
        {
            if (def.kind != Kind::Choice) return false;
            const int c = choiceIndex (def, text);
            if (c < 0) return false;
            v = (float) c;
        }
    }

    v = std::clamp (v, def.min, def.max);
    if (def.kind != Kind::Float) v = std::round (v);
    values[index] = v;
    return true;
}

bool parsePreset (const std::string& json, Preset& out, std::string& error)
{
    Reader r { json, 0, {} };
    out = Preset {};

    if (! r.expect ('{')) { error = r.error; return false; }
    while (! r.peek ('}'))
    {
        std::string key;
        if (! r.readString (key) || ! r.expect (':')) { error = r.error; return false; }

        if (key == "params")
        {
            if (! r.expect ('{')) { error = r.error; return false; }
            while (! r.peek ('}'))
            {
                std::string id, text;
                if (! r.readString (id) || ! r.expect (':') || ! r.readScalar (text)) { error = r.error; return false; }
                if (! setParamFromText (out.values, id, text))
                {
                    error = "unknown parameter or value: " + id + " = " + text;
                    return false;
                }
                if (r.peek (',')) ++r.i;
            }
            ++r.i;
        }
        else if (key == "name" || key == "beakType")
        {
            std::string text;
            if (! r.readScalar (text)) { error = r.error; return false; }
            (key == "name" ? out.name : out.beakType) = text;
        }
        else if (! r.skipValue()) // documentation only: source, matching report, notes
        {
            error = r.error;
            return false;
        }
        if (r.peek (',')) ++r.i;
    }
    return true;
}

std::string presetToJson (const Preset& preset)
{
    std::ostringstream o;
    o << "{\n  \"name\": \"" << preset.name << "\",\n  \"beakType\": \"" << preset.beakType << "\",\n  \"params\": {\n";
    for (int i = 0; i < kNumParams; ++i)
    {
        const ParamDef& def = paramDef (i);
        o << "    \"" << def.id << "\": ";
        if (def.kind == Kind::Choice) o << '"' << choiceLabel (def, (int) preset.values[i]) << '"';
        else if (def.kind == Kind::Bool) o << (preset.values[i] >= 0.5f ? "true" : "false");
        else o << preset.values[i];
        o << (i + 1 < kNumParams ? ",\n" : "\n");
    }
    o << "  }\n}\n";
    return o.str();
}

} // namespace pbv
