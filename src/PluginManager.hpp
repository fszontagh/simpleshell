#ifndef PLUGINCONNECTOR_HPP
#define PLUGINCONNECTOR_HPP

#include <iostream>
#include <map>
#include <sol/sol.hpp>
#include <string>
#include <unordered_map>
#include <vector>

class PluginManager {
  private:
    sol::state  L;
    std::string pluginDirectory;

    struct PluginData {
        std::string globalName;
        bool        enabled = false;
        std::string path;
        std::string displayName;
        std::string description;
        sol::table  table;
    };

    std::unordered_map<std::string, PluginData> plugins;

    using GetConfigValue = std::function<std::string(const std::string &, const std::string &)>;
    using SetConfigValue = std::function<void(const std::string &, const std::string &, const std::string &)>;

    // command, params, description
    using RegisterCustomCommand =
        std::function<bool(const std::string &, const std::vector<std::string> &, const std::string &)>;

    void initPlugin(const std::string & pluginName) {
        if (!this->plugins.contains(pluginName)) {
            return;
        }
        const auto plugin = plugins[pluginName];

        try {
            L.script_file(plugin.path);
        } catch (const std::exception & e) {
            std::cerr << "[Lua error] Failed to load plugin: " << pluginName << "\n" << e.what() << std::endl;
            return;
        }

        if (!L[pluginName].valid()) {
            std::cerr << "[Lua error] Plugin '" << pluginName << "' must define a global '" << pluginName << "' table."
                      << std::endl;
            return;
        }

        sol::table pluginTable = L[pluginName];
        L[plugin.globalName]   = L[pluginName];

        if (!pluginTable.valid()) {
            std::cout << "[Lua error] Plugin '" << pluginName << "' must define a global '" << pluginName << "' table."
                      << std::endl;
            plugins[pluginName].enabled = false;
            return;
        }

        if (this->getConfigCallback) {
            pluginTable.set_function(
                "getConfigValue", [this, pluginName](const std::string & key, const std::string & defaultValue = "") {
                    return this->getConfigCallback(pluginName, key).empty() ? defaultValue :
                                                                              this->getConfigCallback(pluginName, key);
                });
        }
        if (this->setConfigCallback) {
            pluginTable.set_function("setConfigValue",
                                     [this, pluginName](const std::string & key, const std::string & value) {
                                         this->setConfigCallback(pluginName, key, value);
                                     });
        }

        sol::function initFunction = pluginTable["init"];
        if (!initFunction.valid()) {
            std::cout << "[Lua error] Plugin '" << pluginName << "' must define an 'init' function." << std::endl;
            plugins[pluginName].enabled = false;
            return;
        }
        const auto _pluginName = pluginTable["name"];
        if (!_pluginName.valid()) {
            std::cerr << "[Lua error] Plugin '" << pluginName << "' must define a 'name' field." << std::endl;
            plugins[pluginName].enabled = false;
            return;
        }
        const auto _pluginDescription = pluginTable["description"];
        if (!_pluginDescription.valid()) {
            std::cerr << "[Lua error] Plugin '" << pluginName << "' must define a 'description' field." << std::endl;
            plugins[pluginName].enabled = false;
            return;
        }

        try {
            initFunction();
            plugins[pluginName].enabled     = true;
            plugins[pluginName].displayName = _pluginName.get<std::string>();
            plugins[pluginName].description = _pluginDescription.get<std::string>();
            plugins[pluginName].table       = std::move(pluginTable);
        } catch (const std::exception & e) {
            std::cerr << "[Lua error] Plugin '" << pluginName << "' init function failed: " << e.what() << std::endl;
            plugins[pluginName].enabled = false;
            return;
        }
    }



  public:
    explicit PluginManager(const std::string & pluginDir);
    ~PluginManager();

    void loadPlugins(const std::unordered_map<std::string, bool> & enabledPlugins);
    void enablePlugin(const std::string & name);
    void disablePlugin(const std::string & name);
    bool pluginExists(const std::string & name) const;
    bool isPluginEnabled(const std::string & name) const;

    // callbacks
    bool OnCommand(std::vector<std::string> & args);
    bool OnPromptFormat(std::string & prompt);

    SetConfigValue setConfigCallback = nullptr;
    GetConfigValue getConfigCallback = nullptr;

    RegisterCustomCommand registerCustomCommand = nullptr;

    std::unordered_map<std::string, PluginData> getPlugins() const { return plugins; }
};

#endif  // PLUGINCONNECTOR_HPP
