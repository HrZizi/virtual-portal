#pragma once
#include <string>
#include <map>
#include <memory>

class IConfigProvider {
public:
    virtual ~IConfigProvider() = default;
    virtual bool Load() = 0;
    virtual bool GetBool(const std::string& section, const std::string& key, bool defaultVal) const = 0;
    virtual std::string GetString(const std::string& section, const std::string& key, const std::string& defaultVal) const = 0;
    virtual int GetInt(const std::string& section, const std::string& key, int defaultVal) const = 0;
};

class IniConfigProvider : public IConfigProvider {
public:
    explicit IniConfigProvider(const std::string& path);
    bool Load() override;
    bool GetBool(const std::string& section, const std::string& key, bool defaultVal) const override;
    std::string GetString(const std::string& section, const std::string& key, const std::string& defaultVal) const override;
    int GetInt(const std::string& section, const std::string& key, int defaultVal) const override;

private:
    std::string m_filePath;
    std::map<std::string, std::map<std::string, std::string>> m_data;
};

class Config {
public:
    static void SetProvider(std::unique_ptr<IConfigProvider> provider);
    static IConfigProvider& Get();
private:
    static std::unique_ptr<IConfigProvider> s_provider;
};
