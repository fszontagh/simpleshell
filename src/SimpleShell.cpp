#include "SimpleShell.hpp"

#include <filesystem>
#include <iostream>

SimpleShell * SimpleShell::instance = nullptr;

SimpleShell::SimpleShell() : prompt_("$ ") {
    SimpleShell::instance = this;
    const char * homeDir  = getenv("HOME");

    if (homeDir == nullptr) {
        std::cerr << "HOME directory not found." << '\n';
        return;
    }

    this->home_directory_ = std::string(homeDir);
    this->plugin_manager  = std::make_shared<PluginManager>(PLUGINS_DIR);

    this->readConfig();
    this->loadEnvironmentVariables();

    this->plugin_manager->loadPlugins(instance->config_get_plugins_enabled());
    this->parse_variables();
    this->format_prompt();
    read_history((std::string(this->home_directory_) + "/.pshell_history").c_str());
}

void SimpleShell::parse_variables() {
    /// load variables from config file
    const auto env_vars   = config_get_section_variables("environment");
    const auto local_vars = config_get_section_variables("variables");

    for (const auto & entry : env_vars) {
        const auto & key   = entry.key;
        const auto & value = entry.value;
        if (key.empty() || value.empty()) {
            continue;
        }
        try {
            this->env_set(key, value, SimpleShell::variable_type::SL_VAR_GLOBAL);
        } catch (const std::exception & e) {
            std::cerr << "Failed to add environment variable: " << e.what() << " " << __FILE__ << ":" << __LINE__
                      << '\n';
        }
    }
    for (const auto & entry : local_vars) {
        const auto & key   = entry.key;
        const auto & value = entry.value;
        if (key.empty() || value.empty()) {
            continue;
        }
        try {
            this->env_set(key, value, SimpleShell::variable_type::SL_VAR_LOCAL);
        } catch (const std::exception & e) {
            std::cerr << "Failed to add local variable: " << e.what() << '\n';
        }
    }

    for (auto it = this->shell_variables_.begin(); it != this->shell_variables_.end(); it++) {
        auto & entry = *it;

        // skip environment variables to execute
        if (entry.type == SimpleShell::variable_type::SL_VAR_ENVIRONMENT) {
            continue;
        }
        // clean up
        //entry.key            = utils::ConfigUtils::trim_string(entry.key);
        //entry.value          = utils::ConfigUtils::trim_string(entry.value);
        //entry.original_value = utils::ConfigUtils::trim_string(entry.original_value);

        if (entry.original_value.find('`') != std::string::npos) {
            std::string command = entry.original_value;
            // remove quotes
            command.erase(0, 1);
            command.erase(command.length() - 1);
            auto result = exec_shell_command(command);
            if (!result.empty()) {
                entry.value = utils::ConfigUtils::trim_string(result);
            }
        }
    }
}

void SimpleShell::run(const std::string & maybefile, const std::vector<std::string> & params) {
    std::string command;

    bool one_shot = false;
    if ((!maybefile.empty()) && (maybefile != "-")) {
        if (std::filesystem::exists(maybefile)) {
            command = maybefile;
            for (const auto & param : params) {
                command.append(" ");
                command.append(param);
            }
            one_shot = true;
        }
    }

    while (true) {
        this->parse_variables();
        this->format_prompt();

        if (command.empty()) {
            char * input = readline(prompt_.c_str());
            if (input == nullptr) {
                break;
            }
            command = std::string(input);
            free(input);
        }

        if (command == "exit") {
            std::cout << "Exiting..." << '\n';
            break;
        }

        const std::string original_command = SimpleShell::replace_variables(command);

        if (!command.empty()) {
            this->execute_command(command);
            add_history(original_command.c_str());
        }
        command.clear();
        if (one_shot) {
            break;
        }
    }

    const char * homeDir = getenv("HOME");
    if (homeDir != nullptr) {
        write_history((std::string(homeDir) + "/.pshell_history").c_str());
    }
}

SimpleShell::~SimpleShell() {
    this->writeConfig();
}

void SimpleShell::execute_command(const std::string & command) {
    std::stringstream        ss(command);
    std::string              segment;
    std::vector<std::string> args;
    while (ss >> segment) {
        args.push_back(segment);
    }

    if (args.empty()) {
        return;
    }

    // get the aliases
    for (const auto & alias : this->config_get_section_variables("aliases")) {
        if (alias.key.empty()) {
            continue;
        }
        if (args[0] == alias.key) {
            // replace the command with the alias
            std::vector<std::string> alias_args = {};
            std::stringstream        ss(alias.value);
            std::string              segment;
            while (ss >> segment) {
                alias_args.push_back(segment);
            }
            alias_args.insert(alias_args.end(), args.begin() + 1, args.end());
            args = alias_args;

            instance->plugin_manager->call("OnAlias", { "", alias.key, alias.value });
            break;
        }
    }

    args = SimpleShell::replace_stars(args);
    instance->plugin_manager->call("OnCommand", { "", args[0], command });

    if (args[0] == "echo") {
        SimpleShell::echo(args);
        return;
    }
    if (args[0] == "cd") {
        this->cd(args);
        return;
    }
    if (args[0] == "history") {
        return;
    }

    if (args[0] == "export") {
        //   this->export_variable(args);
        return;
    }
    if (args[0] == "alias") {
        this->alias(args);
        return;
    }
    if (args[0] == "reload_config") {
        this->reload_config();
        return;
    }

    if (args[0] == "plugins") {
        SimpleShell::plugins(args);
        return;
    }
    if (args[0] == "fg") {
        SimpleShell::fg(args);
        return;
    }
    if (args[0] == "bg") {
        SimpleShell::bg(args);
        return;
    }
    if (args[0] == "env") {
        for (const auto & env : this->get_env_variables()) {
            std::cout << env.key << "=" << env.value << "\n";
        }
        return;
    }
    if (args[0] == "jobs") {
        const auto n_stopped = ProcessManager::instance().get_stopped_processes_count();
        const auto n_running = ProcessManager::instance().get_running_processes_count();

        std::cout << "Running processes: " << n_running << "\n";

        if (n_running > 0) {
            for (const auto & process : ProcessManager::instance().get_running_processes()) {
                const std::string status = ProcessManager::statusToString(process.state);
                std::cout << "PID: " << process.pid << " status: " << status << ", Command: " << process.command
                          << "\n";
            }
        }

        std::cout << "Stopped jobs: " << n_stopped << "\n";

        if (n_stopped > 0) {
            for (const auto & process : ProcessManager::instance().get_stopped_processes()) {
                const std::string status = ProcessManager::statusToString(process.state);
                std::cout << "PID: " << process.pid << " status: " << status << ", Command: " << process.command
                          << "\n";
            }
        }
        std::cout << '\n';
        return;
    }
    bool run_in_background = false;
    if (!args.empty() && args.back() == "&") {
        run_in_background = true;
        args.pop_back();
        std::cout << "Running in background: " << command << '\n';
        instance->plugin_manager->call("OnRunBackground", { "", args[0], command });
    }else{
        instance->plugin_manager->call("OnRunForeground", { "", args[0], command });
    }

    ProcessManager::start_process(args, run_in_background);
    instance->plugin_manager->call("OnFinish", { "", args[0], command });
}

void SimpleShell::format_prompt() {

    this->prompt_ = this->prompt_format_ = this->config_get_value("shell", "prompt_format", this->prompt_format_);

    instance->plugin_manager->call("OnBeforePromptFormat", { "", this->prompt_ });

    SimpleShell::replace_colors(this->prompt_);
    SimpleShell::replace_variables(this->prompt_);
}

int SimpleShell::config_handler(void * user, const char * section, const char * name, const char * value) {
    SimpleShell * shell = static_cast<SimpleShell *>(user);
    if (shell == nullptr) {
        return 0;
    }
    std::string section_str(section);
    std::string name_str(name);

    conf_variable var(name_str, value);

    if (shell->config_map_.find(section_str) == shell->config_map_.end()) {
        shell->config_map_[section_str] = std::map<std::string, conf_variable>();
    }
    auto & cfg_section    = shell->config_map_[section_str];
    cfg_section[name_str] = std::move(var);
    return 1;
}

std::string SimpleShell::config_get_value(const std::string & section_name, const std::string & key_name,
                                          const std::string & default_value) {
    if (this->config_map_.find(section_name) != this->config_map_.end()) {
        const auto section = this->config_map_[section_name];
        for (const auto & entry : section) {
            if (entry.first == key_name) {
                return entry.second.value;
            }
        }
    }
    return default_value;
}

std::vector<SimpleShell::env_variable> SimpleShell::get_env_variables(SimpleShell::variable_type type) {
    {
        std::vector<SimpleShell::env_variable> variables;
        for (const auto & var : shell_variables_) {
            if (var.type == type || type == SimpleShell::variable_type::SL_VAR_ANY) {
                variables.push_back(var);
            }
        }
        return variables;
    }
}

std::vector<SimpleShell::conf_variable> SimpleShell::config_get_section_variables(const std::string & section) {
    std::vector<SimpleShell::conf_variable> variables;

    if (this->config_map_.find(section) != this->config_map_.end()) {
        for (const auto & var : this->config_map_[section]) {
            variables.push_back(var.second);
        }
        return variables;
    }
    return {};
}

void SimpleShell::config_set_section_variable(const std::string & section, const std::string & key,
                                              const std::string & value, bool flush) {
    if (this->config_map_.find(section) == this->config_map_.end()) {
        this->config_map_[section] = std::map<std::string, conf_variable>();
    }
    auto & cfg_section = this->config_map_[section];
    cfg_section[key]   = conf_variable(key, value);
    if (flush) {
        this->writeConfig();
    }
}

void SimpleShell::env_set(const std::string & key, const std::string & value, SimpleShell::variable_type type) {
    if (key.empty()) {
        throw std::invalid_argument("key cannot be empty");
    }

    if (type == SimpleShell::variable_type::SL_VAR_ENVIRONMENT) {
        auto * const env_test = getenv(key.c_str());
        if (env_test == nullptr) {
            return;
        }
    }

    auto it = std::find_if(shell_variables_.begin(), shell_variables_.end(),
                           [&key](const SimpleShell::env_variable & var) { return var.key == key; });
    if (it != shell_variables_.end()) {
        it->value = value;
    } else {
        shell_variables_.push_back(SimpleShell::env_variable(key, value, type));
    }

    if (type == SimpleShell::variable_type::SL_VAR_GLOBAL) {
        setenv(key.c_str(), value.c_str(), 1);
    }
}
