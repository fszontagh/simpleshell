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
    };

    std::unordered_map<std::string, PluginData> plugins;

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
            plugins[pluginName].enabled = true;
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

    std::unordered_map<std::string, PluginData> getPlugins() const { return plugins; }

    std::map<std::string, std::string> call(const std::string & functionName, const std::vector<std::string> & args);
};

#endif  // PLUGINCONNECTOR_HPP
