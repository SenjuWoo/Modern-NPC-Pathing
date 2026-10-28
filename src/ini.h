#pragma once

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>

// Tiny INI reader shared by Settings and the compat layer: "section/key" (lowercased) -> value.
namespace Ini
{
    using Values = std::unordered_map<std::string, std::string>;

    inline std::string Trim(const std::string& a_str)
    {
        const auto begin = a_str.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) {
            return {};
        }
        const auto end = a_str.find_last_not_of(" \t\r\n");
        return a_str.substr(begin, end - begin + 1);
    }

    inline std::string Lower(std::string a_str)
    {
        std::ranges::transform(a_str, a_str.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return a_str;
    }

    inline Values Parse(const char* a_path)
    {
        Values values;
        std::ifstream file(a_path);
        if (!file.is_open()) {
            return values;
        }
        std::string line;
        std::string section;
        while (std::getline(file, line)) {
            line = Trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#') {
                continue;
            }
            if (line.front() == '[' && line.back() == ']') {
                section = Lower(Trim(line.substr(1, line.size() - 2)));
                continue;
            }
            const auto eq = line.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            auto value = Trim(line.substr(eq + 1));
            if (const auto comment = value.find(';'); comment != std::string::npos) {
                value = Trim(value.substr(0, comment));  // trailing "; note"
            }
            values[section + "/" + Lower(Trim(line.substr(0, eq)))] = std::move(value);
        }
        return values;
    }

    // Accepts 1/0, true/false, yes/no, on/off (Fluid Motion writes "true"/"false", ours writes 1/0).
    inline bool Bool(const Values& a_values, const char* a_key, bool a_default)
    {
        const auto it = a_values.find(a_key);
        if (it == a_values.end()) {
            return a_default;
        }
        const auto v = Lower(it->second);
        if (v == "true" || v == "yes" || v == "on") {
            return true;
        }
        if (v == "false" || v == "no" || v == "off") {
            return false;
        }
        return std::atoi(v.c_str()) != 0;
    }
}
