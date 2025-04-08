#ifndef SIMPLE_SHELL_HPP
#define SIMPLE_SHELL_HPP

// Standard C/C++
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>

// System (POSIX)
#include <glob.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <sys/wait.h>
#include <unistd.h>


#include "options.hpp"
#include "utils.h"

// Third-party
#include "ini.h"
#include "PluginManager.hpp"
#include "ProcessManager.hpp"

class SimpleShell {
  public:
    SimpleShell();
    ~SimpleShell();

    void run(const std::string & maybefile = "", const std::vector<std::string> & params = {});

    static void signal_handler_wrapper(int sig) {
        if (instance) {
            if (sig == SIGINT) {
                SimpleShell::handle_sigint(sig);
                return;
            }
            if (sig == SIGTSTP) {
                SimpleShell::handle_sigtstp(sig);
                return;
            }
            if (sig == SIGCONT) {
                SimpleShell::handle_sigcont(sig);
                return;
            }
            if (sig == SIGCHLD) {
                SimpleShell::handle_sigchld(sig);
                return;
            }
            std::cerr << "Unknown signal received: " << sig << '\n';
            return;
        }
        std::cerr << "Instance not initialized." << '\n';
    }

  private:
    std::shared_ptr<PluginManager> plugin_manager = nullptr;

    enum variable_type : std::uint8_t {
        // internal variable
        SL_VAR_LOCAL,
        // global variable and set as environment variable
        SL_VAR_GLOBAL,
        // environment variable coming from the environment
        SL_VAR_ENVIRONMENT,
        // filter to return all variables
        SL_VAR_ANY
    };

    struct env_variable {
        std::string   key;
        std::string   value;
        std::string   original_value;
        variable_type type = SL_VAR_LOCAL;

        env_variable(const std::string & key, const std::string & value, variable_type type = SL_VAR_LOCAL) :
            key(key),
            value(value),
            original_value(value),
            type(type) {
            if (key.empty()) {
                throw std::invalid_argument("key cannot be empty");
            }
        }
    };

    enum conf_variable_format_type : std::uint8_t { SL_CONF_VAR_ESCAPED, SL_CONF_VAR_QUOTED };

    struct conf_variable {
        std::string               key;
        std::string               value;
        conf_variable_format_type format_type = SL_CONF_VAR_ESCAPED;

        conf_variable(const std::string & key, const std::string & value) : key(key), value(value) {
            if (key.empty()) {
                throw std::invalid_argument("key cannot be empty");
            }
            if (value.substr(0, 1) == "\"" || value.substr(0, 1) == "'") {
                format_type = SL_CONF_VAR_QUOTED;
                this->value = value.substr(1, value.length() - 2);
            } else {
                format_type = SL_CONF_VAR_ESCAPED;
                this->value = utils::ConfigUtils::unescape(value);
            }
        }

        conf_variable() = default;
    };

    typedef std::map<std::string, conf_variable> config_pair;
    typedef std::map<std::string, config_pair>   env_pair;

    inline static const std::unordered_map<std::string, std::string> color_codes = {
        { "COLOR_BLACK",    "\033[30m" },
        { "COLOR_RED",      "\033[31m" },
        { "COLOR_GREEN",    "\033[32m" },
        { "COLOR_YELLOW",   "\033[33m" },
        { "COLOR_BLUE",     "\033[34m" },
        { "COLOR_MAGENTA",  "\033[35m" },
        { "COLOR_CYAN",     "\033[36m" },
        { "COLOR_WHITE",    "\033[37m" },
        { "COLOR_RESET",    "\033[0m"  },
        { "FONT_BOLD",      "\033[1m"  },
        { "FONT_UNDERLINE", "\033[4m"  },
        { "FONT_REVERSED",  "\033[7m"  }
    };

    static SimpleShell *               instance;
    ProcessManager                     process_manager_;
    std::string                        prompt_;
    std::string                        prompt_format_ = "[$PWD]$ ";
    std::vector<env_variable>          shell_variables_;
    std::map<std::string, config_pair> config_map_;
    std::string                        home_directory_;
    std::map<pid_t, std::string>       stopped_jobs_;
    std::map<pid_t, std::string>       running_processes_;

    static std::string exec_shell_command(const std::string & command) {
        char        buffer[128];
        std::string result;
        FILE *      pipe = popen(command.c_str(), "r");
        if (!pipe) {
            throw std::runtime_error("popen() failed!");
        }
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result += buffer;
        }
        pclose(pipe);
        return result;
    }

    static std::string replace_variables(std::string & input) {
        const auto  original_input = input;
        std::smatch match;
        std::regex  var_pattern(R"(\$\{([A-Z0-9_]+)(:-([^}]*))?\})");

        auto search_start = input.cbegin();
        while (std::regex_search(search_start, input.cend(), match, var_pattern)) {
            std::string full_match = match[0];
            std::string var_name   = match[1];
            std::string fallback   = match[3];

            std::string value;

            auto it = std::find_if(SimpleShell::instance->shell_variables_.begin(),
                                   SimpleShell::instance->shell_variables_.end(),
                                   [&](const env_variable & var) { return var.key == var_name; });

            if (it != SimpleShell::instance->shell_variables_.end()) {
                value = it->value;
            } else {
                const char * env_val = getenv(var_name.c_str());
                if (env_val) {
                    value = env_val;
                } else {
                    value = fallback;
                }
            }

            // Replace
            auto position = std::distance(input.cbegin(), match[0].first);
            input.replace(position, full_match.length(), value);
            search_start = input.cbegin() + position + value.length();
        }
        // replace ~ with home directory
        std::string home = getenv("HOME");
        if (!home.empty()) {
            // replace all occurrences of ~ with home directory
            size_t pos = 0;
            while ((pos = input.find('~', pos)) != std::string::npos) {
                input.replace(pos, 1, home);
                pos += home.length();
            }
        }
        return original_input;
    }

    static std::vector<std::string> replace_stars(const std::vector<std::string> & args) {
        std::vector<std::string> result;
        const std::string        current_dir = getenv("PWD");

        for (const auto & s : args) {
            const auto _pos = s.find('*');
            if (_pos != std::string::npos) {
                if (_pos > 0 && s.substr(_pos - 1, 1) == "\\") {
                    result.push_back(s);
                    continue;
                }
                if (_pos > 0 && s.substr(_pos - 1, 1) == "\"") {
                    result.push_back(s);
                    continue;
                }
                std::string base_path = current_dir;
                std::string pattern   = s;

                if (s.find('/') != std::string::npos) {
                    std::filesystem::path p(s);
                    pattern   = p.filename().string();
                    base_path = p.parent_path().string();
                    if (base_path.empty()) {
                        base_path = current_dir;
                    }
                }

                std::string matched_files = glob_files(pattern, base_path);


                std::istringstream iss(matched_files);
                std::string        token;
                while (iss >> std::quoted(token)) {
                    result.push_back(token);
                }
            } else {
                result.push_back(s);
            }
        }

        return result;
    }

    static void replace_colors(std::string & input) {
        std::smatch match;
        std::regex  var_pattern(R"(\$\{([A-Z0-9_]+)\})");

        auto search_start = input.cbegin();
        while (std::regex_search(search_start, input.cend(), match, var_pattern)) {
            std::string full_match = match[0];
            std::string var_name   = match[1];
            std::string value;

            if (var_name.find("COLOR_") == 0 || var_name.find("FONT_") == 0 || var_name.find("BG_") == 0) {
                auto it = color_codes.find(var_name);
                if (it != color_codes.end()) {
                    value = it->second;
                } else {
                    value = "";
                }

                // Replace
                auto position = std::distance(input.cbegin(), match[0].first);
                input.replace(position, full_match.length(), value);
                search_start = input.cbegin() + position + value.length();
            } else {
                search_start = match[0].second;
            }
        }
    };

    static int                 config_handler(void * user, const char * section, const char * name, const char * value);
    std::string                config_get_value(const std::string & section_name, const std::string & key_name,
                                                const std::string & default_value = "");
    std::vector<conf_variable> config_get_section_variables(const std::string & section);

    std::unordered_map<std::string, bool> config_get_plugins_enabled() {
        std::unordered_map<std::string, bool> plugins;
        for (const auto & cfg_key : this->config_get_section_variables("plugins")) {
            const auto & plugin_name = cfg_key.key;
            const auto & plugin_cfg  = cfg_key.value;
            if (plugin_name.empty() || plugin_cfg.empty()) {
                continue;
            }
            plugins[plugin_name] = plugin_cfg == "true";
        }
        return plugins;
    }

    void config_set_section_variable(const std::string & section, const std::string & key, const std::string & value,
                                     bool flush = false);

    void loadEnvironmentVariables() {
        for (char ** env = environ; *env != nullptr; ++env) {
            std::string env_var(*env);
            std::string key;
            std::string value;
            std::size_t pos = env_var.find('=');
            if (pos != std::string::npos) {
                key   = env_var.substr(0, pos);
                value = env_var.substr(pos + 1);
            }
            try {
                this->env_set(key, value, SimpleShell::variable_type::SL_VAR_ENVIRONMENT);
            } catch (const std::exception & e) {
                std::cerr << "Failed to add environment variable: " << e.what() << " ( " << key << ": " << value
                          << " ) " << __FILE__ << ":" << __LINE__ << '\n';
            }
        }
    }

    void readConfig() {
        std::string configFilePath = std::string(this->home_directory_) + "/.pshell";
        if (ini_parse(configFilePath.c_str(), config_handler, this) < 0) {
            std::cerr << "Failed to read configuration file: " << configFilePath << '\n';
        }
    }

    void writeConfig() {
        std::string   configFilePath = std::string(this->home_directory_) + "/.pshell";
        std::ofstream configFile(configFilePath);
        if (!configFile.is_open()) {
            std::cerr << "Failed to open configuration file for writing: " << configFilePath << '\n';
            return;
        }

        for (const auto & var : this->config_map_) {
            configFile << "[" << var.first << "]\n";
            for (const auto & var2 : var.second) {
                if (var2.second.format_type == conf_variable_format_type::SL_CONF_VAR_ESCAPED) {
                    configFile << var2.second.key << " = " << utils::ConfigUtils::escape(var2.second.value) << "\n";
                } else {
                    configFile << var2.second.key << " = \"" << var2.second.value << "\"\n";
                }
            }
            configFile << "\n";
        }

        configFile.close();
    }

    static void handle_sigchld(const int & /*signal*/) { ProcessManager::handle_completed_processes(); }

    static void handle_sigint(const int & signal) {
        ProcessManager::instance().send_signal_to_foregound(signal);
        SimpleShell::instance->parse_variables();
        SimpleShell::instance->format_prompt();
        rl_replace_line("", 0);
        rl_crlf();
        rl_on_new_line();
        rl_redisplay();
    }

    static void handle_sigtstp(const int & signal) {
        ProcessManager::instance().send_signal_to_foregound(signal);
        SimpleShell::instance->parse_variables();
        SimpleShell::instance->format_prompt();
        rl_replace_line("", 0);
        rl_crlf();
        rl_on_new_line();
        rl_redisplay();
    }

    static void handle_sigcont(const int & signal) { ProcessManager::instance().send_signal_to_foregound(signal); }

    void format_prompt();

    void                      parse_variables();
    void                      set_environment_variables();
    void                      env_set(const std::string & key, const std::string & value, variable_type type);
    std::vector<env_variable> get_env_variables(variable_type type = SL_VAR_ANY);

    void execute_command(const std::string & command);

    void reload_config() {
        this->readConfig();
        this->loadEnvironmentVariables();
        this->parse_variables();
        this->format_prompt();
        std::cout << "Configuration reloaded." << '\n';
    }

    void cd(const std::vector<std::string> & args) {
        if (args.empty()) {
            std::cerr << "cd: missing argument" << '\n';
            return;
        }
        std::string path = args[1];
        if (path.empty()) {
            std::cerr << "cd: missing argument" << '\n';
            return;
        }

        if (chdir(path.c_str()) != 0) {
            perror("cd");
        } else {
            path = std::string(getcwd(nullptr, 0));
            this->env_set("PWD", path, SimpleShell::variable_type::SL_VAR_GLOBAL);
        }
    }

    static void echo(const std::vector<std::string> & args) {
        for (size_t i = 1; i < args.size(); ++i) {
            std::cout << args[i] << (i == args.size() - 1 ? "" : " ");
        }
        std::cout << '\n';
    }

    void alias(const std::vector<std::string> & args) {
        if (args.size() < 1) {
            for (const auto & cfg : this->config_get_section_variables("aliases")) {
                std::cout << cfg.key << " = " << cfg.value << '\n';
            }
            return;
        }
    }

    static void bg(const std::vector<std::string> & args) {
        pid_t pid = ProcessManager::instance().process_get_latest_stopped_pid();
        if (args.size() == 2) {
            {
                pid = std::stoi(args[1]);
            }
        }
        if (pid < 0) {
            std::cout << "No stopped jobs.\n";
            return;
        }
        ProcessManager::send_signal_to_process(pid, SIGCONT);
    }

    static void fg(const std::vector<std::string> & args) {
        pid_t pid = ProcessManager::instance().process_get_latest_stopped_pid();
        if (args.size() == 2) {
            {
                pid = std::stoi(args[1]);
            }
        }
        if (pid < 0) {
            std::cout << "No stopped jobs.\n";
            return;
        }
        ProcessManager::process_handle_foreground(pid, getpgrp());
    }

    static void plugins(const std::vector<std::string> & args) {
        if (args.size() < 2) {
            std::cout << "Usage: plugins [list|enable|disable|reload]\n";
            return;
        }
        auto * isntance = SimpleShell::instance;
        if (args[1] == "list") {
            std::cout << "Plugin name \t\t | enabled\t \n";
            for (const auto & plugin : instance->plugin_manager->getPlugins()) {
                std::cout << plugin.first << " \t|" << plugin.second.enabled << "\n";
            }
            return;
        }
        if (args[1] == "enable" && args.size() == 3) {
            instance->plugin_manager->enablePlugin(args[2]);
            instance->config_set_section_variable("plugins", args[2], "true");
            return;
        }
        if (args[1] == "disable" && args.size() == 3) {
            instance->plugin_manager->disablePlugin(args[2]);
            instance->config_set_section_variable("plugins", args[2], "false");
            return;
        }
        if (args[1] == "reload" && args.size() == 2) {
            instance->plugin_manager->loadPlugins(instance->config_get_plugins_enabled());
            return;
        }
    }

    [[nodiscard]] static std::string glob_files(const std::string & pattern, const std::string & base_path = "") {
        std::cout << "Globbing: " << pattern << " Base path: " << base_path << '\n';
        // glob struct resides on the stack
        glob_t glob_result;
        memset(&glob_result, 0, sizeof(glob_result));

        // do the glob operation
        int return_value = glob(pattern.c_str(), GLOB_TILDE, NULL, &glob_result);
        if (return_value != 0) {
            globfree(&glob_result);
            return "";
        }

        // collect all the filenames into a std::list<std::string>
        std::string filenames;
        for (size_t i = 0; i < glob_result.gl_pathc; ++i) {
            std::string p = glob_result.gl_pathv[i];
            if (base_path.empty() == false) {
                // remove the base_path from the p
                if (p.find(base_path) == 0) {
                    p = p.substr(base_path.size());
                }
            }
            filenames.append(utils::ConfigUtils::escape(p));
            filenames.append(" ");
        }
        filenames = filenames.substr(0, filenames.size() - 1);
        // cleanup
        globfree(&glob_result);
        std::cout << "Filenames: " << filenames << '\n';
        // done
        return filenames;
    }
};

#endif  // SIMPLE_SHELL_HPP
