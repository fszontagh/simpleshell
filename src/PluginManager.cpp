#include "PluginManager.hpp"
#include <filesystem>

PluginManager::PluginManager(const std::string & pluginDir) : pluginDirectory(pluginDir) {
    L.open_libraries();
    L.set_function("error", [&](const std::string & msg) { throw msg; });
}

PluginManager::~PluginManager() {

}

void PluginManager::loadPlugins(const std::unordered_map<std::string, bool> & enabledPlugins) {
    namespace fs = std::filesystem;

    L.stack_clear();

    std::string luaPathExtension = pluginDirectory + "/base/?.lua";
    std::string currentPath      = L["package"]["path"];
    currentPath += ";" + luaPathExtension;
    L["package"]["path"] = currentPath;

    // Pluginok betöltése
    for (const auto & entry : fs::directory_iterator(pluginDirectory)) {
        if (entry.path().extension() == ".lua") {
            std::string path       = entry.path().string();
            std::string pluginName = entry.path().stem().string();
            std::string globalName = "plugin_" + pluginName;
            plugins[pluginName]    = { globalName, false, path };

            if (enabledPlugins.contains(pluginName) && enabledPlugins.at(pluginName) == true) {
                this->initPlugin(pluginName);
            }
        }
    }
}

void PluginManager::enablePlugin(const std::string & name) {
    if (plugins.contains(name)) {
        plugins[name].enabled = true;
    }
}

void PluginManager::disablePlugin(const std::string & name) {
    if (plugins.contains(name)) {
        plugins[name].enabled = false;
    }
}

bool PluginManager::pluginExists(const std::string & name) const {
    return plugins.contains(name);
}

bool PluginManager::isPluginEnabled(const std::string & name) const {
    auto it = plugins.find(name);
    return it != plugins.end() && it->second.enabled;
}

std::map<std::string, std::string> PluginManager::call(const std::string &              functionName,
                                                       const std::vector<std::string> & args) {
    std::map<std::string, std::string> results;

    for (const auto & [name, plugin] : plugins) {
        if (!plugin.enabled) {
            continue;
        }
        results[name] = "";

        sol::table pluginTable = L[plugin.globalName];

        if (!pluginTable.valid()) {
            std::cout << "[Lua error] Plugin '" << name << "' must define a global '" << plugin.globalName << "' table."
                      << std::endl;
            continue;
        }

        sol::function luaFunction = pluginTable[functionName];

        if (!luaFunction.valid()) {
            std::cerr << "[Lua call error] Plugin '" << name << "': '" << functionName
                      << "' is not a function! It might be a table or undefined." << std::endl;
            continue;
        }

        try {
            sol::protected_function_result result = luaFunction(sol::as_args(args));
            if (result.valid()) {
                if (!result.get<std::string>().empty()) {
                    results[name] = result.get<std::string>();
                }
            }
        } catch (const std::exception & e) {
            std::cerr << "[Lua call error] Plugin '" << name << "': " << e.what() << std::endl;
        }
    }

    return results;
}
