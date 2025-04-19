#include "SimpleShell.hpp"

#include <filesystem>
#include <iostream>
#include <fcntl.h>
#include <sys/stat.h>

SimpleShell * SimpleShell::instance = nullptr;

SimpleShell::SimpleShell() : prompt_("$ ") {
    SimpleShell::instance = this;
    const char * homeDir  = getenv("HOME");

    if (homeDir == nullptr) {
        std::cerr << "HOME directory not found." << utils::ENDLINE;
        return;
    }

    this->home_directory_ = std::string(homeDir);

    this->plugin_manager = std::make_shared<PluginManager>(PLUGINS_DIR);

    this->readConfig();
    this->loadEnvironmentVariables();

    this->plugin_manager->setConfigCallback = [this](const std::string & section, const std::string & key,
                                                     const std::string & value) {
        this->config_set_section_variable(section, key, value, true);
    };

    this->plugin_manager->getConfigCallback = [this](const std::string & section, const std::string & key) {
        return this->config_get_value(section, key, "");
    };

    this->plugin_manager->registerCustomCommand = [this](const std::string &              command,
                                                         const std::vector<std::string> & params,
                                                         const std::string &              description) -> bool {
        return this->custom_command_add(command, params, description,
                                        SimpleShell::custom_command_type::SL_CUSTOM_COMMAND_TYPE_PLUGIN);
    };

    this->plugin_manager->loadPlugins(instance->config_get_plugins_enabled());

    this->parse_variables();
    this->format_prompt();
    read_history((std::string(this->home_directory_) + "/.pshell_history").c_str());

    this->LoadSystemBinaries();

    rl_attempted_completion_function = SimpleShell::rl_completion;
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
                      << utils::ENDLINE;
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
            std::cerr << "Failed to add local variable: " << e.what() << utils::ENDLINE;
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
        if (instance->sigwinch_received) {
            rl_resize_terminal();
            instance->sigwinch_received = false;
        }

        if (command.empty()) {
            char * input = readline(prompt_.c_str());
            if (input == nullptr) {
                std::cout << utils::ENDLINE;
                break;
            }
            command = std::string(input);
            free(input);
        }

        if (command == "exit") {
            std::cout << "Exiting..." << utils::ENDLINE;
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
    // Full pipeline and redirection handling
    // Parse tokens
    std::vector<std::string> raw_args;
    utils::parse_arguments(command, raw_args);
    if (raw_args.empty()) {
        return;
    }

    // Background flag
    bool run_in_background = false;
    if (raw_args.back() == "&") {
        run_in_background = true;
        raw_args.pop_back();
    }

    // Alias expansion
    for (const auto & alias : config_get_section_variables("aliases")) {
        if (!alias.key.empty() && raw_args[0] == alias.key) {
            std::vector<std::string> alias_args;
            std::istringstream ss(alias.value);
            std::string seg;
            while (ss >> seg) alias_args.push_back(seg);
            alias_args.insert(alias_args.end(), raw_args.begin() + 1, raw_args.end());
            raw_args = std::move(alias_args);
            break;
        }
    }

    auto args = replace_stars(raw_args);

    // Split into pipeline commands and collect redirections
    std::vector<std::vector<std::string>> cmds;
    std::vector<std::vector<Redirection>> redirs;
    cmds.emplace_back();
    redirs.emplace_back();
    for (size_t i = 0; i < args.size(); ++i) {
        const auto & tok = args[i];
        if (tok == "|") {
            cmds.emplace_back();
            redirs.emplace_back();
        } else if (tok == "<" || tok == ">" || tok == ">>" || tok == "2>" || tok == "2>>") {
            if (i + 1 >= args.size()) {
                std::cerr << "Missing file after redirection " << tok << utils::ENDLINE;
                return;
            }
            Redirection rd;
            if (tok == "<") { rd.type = RedirType::IN; rd.mode = RedirMode::TRUNC; }
            else if (tok == ">") { rd.type = RedirType::OUT; rd.mode = RedirMode::TRUNC; }
            else if (tok == ">>") { rd.type = RedirType::OUT; rd.mode = RedirMode::APPEND; }
            else if (tok == "2>") { rd.type = RedirType::ERR; rd.mode = RedirMode::TRUNC; }
            else /* "2>>" */    { rd.type = RedirType::ERR; rd.mode = RedirMode::APPEND; }
            rd.file = args[++i];
            redirs.back().push_back(rd);
        } else {
            cmds.back().push_back(tok);
        }
    }

    if (cmds.size() == 1 && redirs[0].empty()) {
        // No pipeline or redirection: default behavior
        if (!instance->plugin_manager->OnCommand(args)) return;
        for (const auto & cmd : custom_commands_) {
            if (cmd.first == args[0] && cmd.second.type == custom_command_type::SL_CUSTOM_COMMAND_TYPE_BUILTIN) {
                if (args.size() > 1 && args[1] == "help") {
                    std::cout << cmd.second.GetFormattedHelp();
                    return;
                }
                cmd.second.builtin_command(args);
                return;
            }
        }
        ProcessManager::start_process(args, run_in_background);
    } else if (cmds.size() == 1) {
        // Single command with redirection
        handle_single_command(cmds[0], redirs[0], run_in_background);
        } else {
            // Multiple commands: pipeline
            execute_pipeline(cmds, redirs, run_in_background);
        }
    }

// Single command execution with redirections
void SimpleShell::handle_single_command(std::vector<std::string> cmd, const std::vector<Redirection> &redirs, bool background) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }
    if (pid == 0) {
        // Child: apply redirections
        for (const auto & rd : redirs) {
            int fd = -1;
            switch (rd.type) {
                case RedirType::IN:
                    fd = open(rd.file.c_str(), O_RDONLY);
                    if (fd < 0) perror("open"); else { dup2(fd, STDIN_FILENO); close(fd); }
                    break;
                case RedirType::OUT:
                    fd = open(rd.file.c_str(), O_WRONLY | O_CREAT | (rd.mode == RedirMode::APPEND ? O_APPEND : O_TRUNC), 0644);
                    if (fd < 0) perror("open"); else { dup2(fd, STDOUT_FILENO); close(fd); }
                    break;
                case RedirType::ERR:
                    fd = open(rd.file.c_str(), O_WRONLY | O_CREAT | (rd.mode == RedirMode::APPEND ? O_APPEND : O_TRUNC), 0644);
                    if (fd < 0) perror("open"); else { dup2(fd, STDERR_FILENO); close(fd); }
                    break;
            }
        }
        // Plugin command
        if (!instance->plugin_manager->OnCommand(cmd)) exit(EXIT_SUCCESS);
        // Built-in command
        for (const auto & bc : custom_commands_) {
            if (bc.first == cmd[0] && bc.second.type == custom_command_type::SL_CUSTOM_COMMAND_TYPE_BUILTIN) {
                bc.second.builtin_command(cmd);
                exit(EXIT_SUCCESS);
            }
        }
        // External command
        std::vector<char *> cargs(cmd.size() + 1);
        for (size_t i = 0; i < cmd.size(); ++i) cargs[i] = const_cast<char *>(cmd[i].c_str());
        cargs[cmd.size()] = nullptr;
        execvp(cargs[0], cargs.data());
        perror("execvp");
        exit(EXIT_FAILURE);
    } else {
        // Parent
        if (background) {
            std::cout << "Process " << pid << " running in background." << utils::ENDLINE;
        } else {
            int status = 0;
            waitpid(pid, &status, 0);
        }
    }
}

// Pipeline execution
void SimpleShell::execute_pipeline(const std::vector<std::vector<std::string>> &cmds, const std::vector<std::vector<Redirection>> &redirs, bool background) {
    size_t n = cmds.size();
    std::vector<int> pipefds(2 * (n - 1));
    for (size_t i = 0; i + 1 < n; ++i) {
        if (pipe(pipefds.data() + 2 * i) < 0) {
            perror("pipe");
            return;
        }
    }
    std::vector<pid_t> pids(n);
    pid_t pgid = 0;
    for (size_t i = 0; i < n; ++i) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return;
        }
        if (pid == 0) {
            // Child: set process group
            if (i == 0) setpgid(0, 0);
            else setpgid(0, pgid);
            // Set up pipeline fds
            if (i > 0) {
                dup2(pipefds[2 * (i - 1)], STDIN_FILENO);
            }
            if (i + 1 < n) {
                dup2(pipefds[2 * i + 1], STDOUT_FILENO);
            }
            // Close all pipe fds
            for (int fd : pipefds) close(fd);
            // Apply segment redirections
            for (const auto & rd : redirs[i]) {
                int fd = -1;
                switch (rd.type) {
                    case RedirType::IN:
                        fd = open(rd.file.c_str(), O_RDONLY);
                        if (fd < 0) perror("open"); else { dup2(fd, STDIN_FILENO); close(fd); }
                        break;
                    case RedirType::OUT:
                        fd = open(rd.file.c_str(), O_WRONLY | O_CREAT | (rd.mode == RedirMode::APPEND ? O_APPEND : O_TRUNC), 0644);
                        if (fd < 0) perror("open"); else { dup2(fd, STDOUT_FILENO); close(fd); }
                        break;
                    case RedirType::ERR:
                        fd = open(rd.file.c_str(), O_WRONLY | O_CREAT | (rd.mode == RedirMode::APPEND ? O_APPEND : O_TRUNC), 0644);
                        if (fd < 0) perror("open"); else { dup2(fd, STDERR_FILENO); close(fd); }
                        break;
                }
            }
            // Plugin
            if (!instance->plugin_manager->OnCommand(cmds[i])) exit(EXIT_SUCCESS);
            // Built-in
            for (const auto & bc : custom_commands_) {
                if (bc.first == cmds[i][0] && bc.second.type == custom_command_type::SL_CUSTOM_COMMAND_TYPE_BUILTIN) {
                    bc.second.builtin_command(cmds[i]);
                    exit(EXIT_SUCCESS);
                }
            }
            // External
            std::vector<char *> cargs(cmds[i].size() + 1);
            for (size_t j = 0; j < cmds[i].size(); ++j) cargs[j] = const_cast<char *>(cmds[i][j].c_str());
            cargs[cmds[i].size()] = nullptr;
            execvp(cargs[0], cargs.data());
            perror("execvp");
            exit(EXIT_FAILURE);
        } else {
            // Parent: set process group
            pids[i] = pid;
            if (i == 0) pgid = pid;
            setpgid(pid, pgid);
        }
    }
    // Parent: close all pipe fds
    for (int fd : pipefds) close(fd);
    if (background) {
        std::cout << "Pipeline running in background pids:";
        for (auto pid : pids) std::cout << " " << pid;
        std::cout << utils::ENDLINE;
    } else {
        int status = 0;
        for (auto pid : pids) waitpid(pid, &status, 0);
    }
}

void SimpleShell::format_prompt() {
    this->prompt_ = this->prompt_format_ = this->config_get_value("shell", "prompt_format", this->prompt_format_);
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

void SimpleShell::LoadSystemBinaries() {
    if (!instance) {
        return;
    }

    const char * path_env = std::getenv("PATH");
    if (!path_env) {
        std::cout << "PATH environment variable not found." << std::endl;
        return;
    }

    std::string                        path_str = path_env;
    std::vector<std::filesystem::path> path_dirs;
    // Split the PATH environment variable into individual directories
    size_t                             start = 0;
    size_t                             end   = 0;
    while ((end = path_str.find(':', start)) != std::string::npos) {
        path_dirs.emplace_back(path_str.substr(start, end - start));
        start = end + 1;
    }
    path_dirs.emplace_back(path_str.substr(start));

    for (const auto & dir : path_dirs) {
        if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
            continue;
        }

        for (const auto & entry : std::filesystem::directory_iterator(dir)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            const auto & path     = entry.path();
            std::string  filename = path.filename().string();

            if (access(path.c_str(), X_OK) == 0) {
                //
                if (!instance->system_binaries_.contains(filename)) {
                    SimpleShell::system_binaries info    = { path.string(), filename, {} };
                    //instance->parse_params_from_help(info); // slow :)
                    instance->system_binaries_[filename] = std::move(info);
                }
            }
        }
    }
}

bool SimpleShell::custom_command_add(const std::string & command, const std::vector<std::string> & params,
                                     const std::string &                      description,
                                     const SimpleShell::custom_command_type & type) const {
    if (!instance) {
        return false;
    }
    if (command.empty()) {
        return false;
    }
    if (instance->custom_commands_.contains(command)) {
        return false;
    }

    SimpleShell::custom_command new_command{ command, {}, description, type };
    new_command.ParamsFromVector(params);
    instance->custom_command_add(new_command);
    return true;
}

bool SimpleShell::custom_command_add(const SimpleShell::custom_command & command) {
    if (!instance) {
        return false;
    }
    if (command.command.empty()) {
        return false;
    }
    if (instance->custom_commands_.contains(command.command)) {
        return false;
    }
    instance->custom_commands_[command.command] = command;
    return true;
}
