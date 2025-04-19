#ifndef PLUGINMANAGER_HPP
#define PLUGINMANAGER_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

// Stub PluginManager: Lua/Sol2 support removed
class PluginManager {
public:
    using GetConfigValue = std::function<std::string(const std::string &, const std::string &)>;
    using SetConfigValue = std::function<void(const std::string &, const std::string &, const std::string &)>;
    using RegisterCustomCommand = std::function<bool(const std::string &, const std::vector<std::string> &, const std::string &)>;

    SetConfigValue setConfigCallback = nullptr;
    GetConfigValue getConfigCallback = nullptr;
    RegisterCustomCommand registerCustomCommand = nullptr;

    explicit PluginManager(const std::string & /*pluginDir*/) {}
    ~PluginManager() = default;

    // No-op: always return true to continue execution
    bool OnCommand(const std::vector<std::string> & /*args*/) { return true; }
    bool OnPromptFormat(std::string & /*prompt*/) { return true; }

    void loadPlugins(const std::unordered_map<std::string, bool> & /*enabled*/) {}
    void enablePlugin(const std::string & /*name*/) {}
    void disablePlugin(const std::string & /*name*/) {}
    bool pluginExists(const std::string & /*name*/) const { return false; }
    bool isPluginEnabled(const std::string & /*name*/) const { return false; }
    std::unordered_map<std::string, bool> getPlugins() const { return {}; }



  public:
    explicit PluginManager(const std::string & pluginDir);
    ~PluginManager();

    void loadPlugins(const std::unordered_map<std::string, bool> & enabledPlugins);
    void enablePlugin(const std::string & name);
    void disablePlugin(const std::string & name);
    bool pluginExists(const std::string & name) const;
    bool isPluginEnabled(const std::string & name) const;

    // callbacks
    // Handle command substitution via plugins; does not modify args
    bool OnCommand(const std::vector<std::string> & args);
    bool OnPromptFormat(std::string & prompt);

    SetConfigValue setConfigCallback = nullptr;
    GetConfigValue getConfigCallback = nullptr;

    RegisterCustomCommand registerCustomCommand = nullptr;

    std::unordered_map<std::string, PluginData> getPlugins() const { return plugins; }
};

#endif  // PLUGINMANAGER_HPP
