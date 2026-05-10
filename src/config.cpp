#include "config.h"
#include <fstream>
#include <sstream>
#include <algorithm>

std::unique_ptr<IConfigProvider> Config::s_provider = nullptr;

void Config::SetProvider(std::unique_ptr<IConfigProvider> provider) {
    s_provider = std::move(provider);
}

IConfigProvider& Config::Get() {
    return *s_provider;
}

IniConfigProvider::IniConfigProvider(const std::string& path) : m_filePath(path) {}

static std::string Trim(std::string s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
    return s;
}

bool IniConfigProvider::Load() {
    std::ifstream file(m_filePath);
    if (!file.is_open()) return false;

    std::string line, currentSection;
    while (std::getline(file, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == ';') continue;

        if (line[0] == '[' && line.back() == ']') {
            currentSection = line.substr(1, line.size() - 2);
        } else {
            size_t pos = line.find('=');
            if (pos != std::string::npos) {
                std::string key = Trim(line.substr(0, pos));
                std::string value = Trim(line.substr(pos + 1));
                m_data[currentSection][key] = value;
            }
        }
    }
    return true;
}

bool IniConfigProvider::GetBool(const std::string& section, const std::string& key, bool defaultVal) const {
    auto sec = m_data.find(section);
    if (sec == m_data.end()) return defaultVal;
    auto k = sec->second.find(key);
    if (k == sec->second.end()) return defaultVal;
    std::string val = k->second;
    std::transform(val.begin(), val.end(), val.begin(), ::tolower);
    return (val == "true" || val == "1" || val == "yes");
}

std::string IniConfigProvider::GetString(const std::string& section, const std::string& key, const std::string& defaultVal) const {
    auto sec = m_data.find(section);
    if (sec == m_data.end()) return defaultVal;
    auto k = sec->second.find(key);
    if (k == sec->second.end()) return defaultVal;
    return k->second;
}

int IniConfigProvider::GetInt(const std::string& section, const std::string& key, int defaultVal) const {
    auto sec = m_data.find(section);
    if (sec == m_data.end()) return defaultVal;
    auto k = sec->second.find(key);
    if (k == sec->second.end()) return defaultVal;
    try {
        return std::stoi(k->second);
    } catch (...) {
        return defaultVal;
    }
}
