#include "PluginManager.hpp"

#include <filesystem>

PluginManager::PluginManager(const std::string & pluginDir) : pluginDirectory(pluginDir) {
    L.open_libraries();
    L.set_function("error", [&](const std::string & msg) { throw msg; });
    L.set_function("print", [](const std::string & msg) { std::cout << msg << std::endl; });
    L.set_function("print_error", [](const std::string & msg) { std::cerr << msg << std::endl; });
}

PluginManager::~PluginManager() {}

void PluginManager::loadPlugins(const std::unordered_map<std::string, bool> & enabledPlugins) {
    namespace fs = std::filesystem;

    L.stack_clear();
    if (this->registerCustomCommand != nullptr) {
        L.set_function("RegisterCommand", this->registerCustomCommand);
    }

    std::string luaPathExtension = pluginDirectory + "/base/?.lua";
    std::string currentPath      = L["package"]["path"];
    currentPath += ";" + luaPathExtension;
    L["package"]["path"] = currentPath;

    // Pluginok betöltése
    for (const auto & entry : fs::directory_iterator(pluginDirectory)) {
        if (entry.path().extension() == ".lua") {
            std::string path       = entry.path().string();
            std::string pluginName = entry.path().stem().string();
            std::string globalName = pluginName;
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
        this->initPlugin(name);
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

bool PluginManager::OnCommand(std::vector<std::string> & args) {
    if (args.empty()) {
        return true;
    }

    const std::string &      command = args[0];
    std::vector<std::string> commandArgs(args.begin() + 1, args.end());

    for (const auto & [name, plugin] : plugins) {
        if (!plugin.enabled) {
            continue;
        }

        sol::protected_function luaFunc = plugin.table["OnCommand"];
        if (!luaFunc.valid()) {
            continue;
        }

        try {
            sol::protected_function_result result = luaFunc(plugin.table, command, commandArgs);
            if (result.valid()) {
                bool allow = result.get<bool>();
                if (!allow) {
                    return false;  // plugin elutasította a futást
                }
            } else {
                return false;  // Lua hiba
            }
        } catch (const sol::error & e) {
            std::cerr << "[Lua error] Plugin '" << name << "' OnCommand() failed: " << e.what() << std::endl;
            return true;  // nem tiltom a parancsot, de logolom
        }
    }

    return true;
}

bool PluginManager::OnPromptFormat(std::string & prompt) {
    bool modified = false;

    for (const auto & [name, plugin] : plugins) {
        if (!plugin.enabled) {
            continue;
        }

        sol::protected_function luaFunc = plugin.table["OnPromptFormat"];
        if (!luaFunc.valid()) {
            continue;
        }

        try {
            sol::protected_function_result result = luaFunc(plugin.table, prompt);
            if (result.valid()) {
                std::string updatedPrompt = result.get<std::string>();
                if (updatedPrompt != prompt) {
                    prompt   = updatedPrompt;
                    modified = true;
                }
            } else {
                return false;  // Lua hiba
            }
        } catch (const sol::error & e) {
            std::cerr << "[Lua error] Plugin '" << name << "' OnPromptFormat() failed: " << e.what() << std::endl;
            return false;
        }
    }

    return modified;
}
