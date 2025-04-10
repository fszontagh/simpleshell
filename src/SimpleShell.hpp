#ifndef SIMPLE_SHELL_HPP
#define SIMPLE_SHELL_HPP

// Standard C/C++
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

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

    using BuiltInCommand = void (*)(const std::vector<std::string> &);

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

    struct system_binaries {
        std::string                        full_path;
        std::string                        bin;
        std::map<std::string, std::string> params;
    };

    static std::optional<system_binaries> find_by_bin_or_path(
        const std::unordered_map<std::string, system_binaries> & map, const std::string & query,
        const std::string & param = "") {
        for (const auto & [key, value] : map) {
            if (value.bin == query || value.full_path == query) {
                if (param.empty()) {
                    return value;
                }
                if (value.params.contains(param)) {
                    return value;
                }
                return value;
            }
        }
        return std::nullopt;
    }

    enum custom_command_type : std::uint8_t {
        SL_CUSTOM_COMMAND_TYPE_BUILTIN,
        SL_CUSTOM_COMMAND_TYPE_PLUGIN,
        SL_CUSTOM_COMMAND_TYPE_ALIAS,
        SL_CUSTOM_COMMAND_TYPE_NONE,
    };

    struct custom_command_params {
        std::string name;
        std::string description;

        std::string toString() const { return name + "\n" + description; }

        custom_command_params(const std::string & from_string) {
            const auto r = utils::ConfigUtils::SplitAtFirstNewline(from_string);
            name         = r.first;
            description  = r.second;
        }

        custom_command_params(const std::string & name, const std ::string & description) :
            name(name),
            description(description) {}

        custom_command_params() = default;
    };

    struct custom_command {
        std::string                        command;
        std::vector<custom_command_params> params;
        std::string                        description;
        custom_command_type                type            = SL_CUSTOM_COMMAND_TYPE_NONE;
        BuiltInCommand                     builtin_command = nullptr;

        void ParamsFromVector(const std::vector<std::string> & params) {
            for (const auto & param : params) {
                this->params.push_back(custom_command_params(param));
            }
        }

        std::string GetFormattedHelp() const {
            std::stringstream ss;
            ss << "Command: " << command << "\n";
            ss << "Description: " << description << "\n";
            if (params.size() > 0) {
                ss << "Params: \n";
                for (const auto & param : params) {
                    ss << "\t  - " << param.name << "\t\t  " << param.description << "\n";
                }
            }
            return ss.str();
        }
    };

    std::unordered_map<std::string, custom_command> custom_commands_{
        { "cd",
         custom_command{ "cd", {}, "Change current directory", SL_CUSTOM_COMMAND_TYPE_BUILTIN, SimpleShell::cd }        },
        { "echo",
         custom_command{ "echo", {}, "Print out a string", SL_CUSTOM_COMMAND_TYPE_BUILTIN, SimpleShell::echo }          },
        { "env",           custom_command{ "env",
                                 {},
                                 "Print out the environment variables",
                                 SL_CUSTOM_COMMAND_TYPE_BUILTIN,
                                 SimpleShell::echo }                                                    },
        { "jobs",          custom_command{ "jobs", {}, "Show jobs", SL_CUSTOM_COMMAND_TYPE_BUILTIN, SimpleShell::jobs } },
        { "plugins",       custom_command{ "plugins",
                                     { custom_command_params{ "list", "List available plugins" },
                                       custom_command_params{ "disable <plugin id>", "Disable plugin with id" },
                                       custom_command_params{ "list", "List available plugins" },
                                       custom_command_params{ "reload", "Reload plugins" } },
                                     "Manage the plugins",
                                     SL_CUSTOM_COMMAND_TYPE_BUILTIN,
                                     SimpleShell::plugins }                                         },
        { "aliases",
         custom_command{ "aliases",
                          { custom_command_params{ "add <alias_name> <command>", "Add a new alias" },
                            custom_command_params{ "delete <alias_name>", "Delete alias with name <alias_name>" },
                            custom_command_params{ "list", "List all configured alias" } },
                          "Show jobs",
                          SL_CUSTOM_COMMAND_TYPE_BUILTIN,
                          SimpleShell::alias }                                                                          },

        { "bg",
         custom_command{ "bg", {}, "Send to the background a job", SL_CUSTOM_COMMAND_TYPE_BUILTIN, SimpleShell::bg }    },
        { "fg",
         custom_command{
              "fg",
              { { "job_id",
                  "The job id to bring into the foreground. If ommitted, the last available job will be used" } },
              "Bring back to the foreground a job",
              SL_CUSTOM_COMMAND_TYPE_BUILTIN,
              SimpleShell::bg }                                                                                         },
        { "reload_config",
         custom_command{
              "reload_config",
              {},
              "Re-read the configuration file and reload it's contents. WARN: all not saved changes will be lost",
              SL_CUSTOM_COMMAND_TYPE_BUILTIN,
              SimpleShell::reload_config }                                                                              },
    };
    static SimpleShell *                             instance;
    ProcessManager                                   process_manager_;
    std::string                                      prompt_;
    std::string                                      prompt_format_ = "[$PWD]$ ";
    std::vector<env_variable>                        shell_variables_;
    std::map<std::string, config_pair>               config_map_;
    std::string                                      home_directory_;
    std::map<pid_t, std::string>                     stopped_jobs_;
    std::map<pid_t, std::string>                     running_processes_;
    std::vector<std::string>                         vocabulary{ "cat", "dog", "canary", "cow", "hamster" };
    std::unordered_map<std::string, system_binaries> system_binaries_;

    system_binaries parse_params_from_help(SimpleShell::system_binaries & bin_info) {
        if (bin_info.full_path.empty()) {
            return bin_info;
        }
        std::string bin_path   = bin_info.full_path;
        // Extract binary name from path
        size_t      last_slash = bin_path.find_last_of('/');
        bin_info.bin           = (last_slash != std::string::npos) ? bin_path.substr(last_slash + 1) : bin_path;

        FILE * fp = popen((bin_path + " --help 2>&1").c_str(), "r");
        if (!fp) {
            return bin_info;
        }

        char               buffer[4096];
        std::ostringstream help_output;
        while (fgets(buffer, sizeof(buffer), fp)) {
            help_output << buffer;
        }
        pclose(fp);

        std::istringstream stream(help_output.str());
        std::string        line;

        std::regex option_regex(R"((-\w|--[a-zA-Z0-9-]+)(,\s*-\w|,\s*--[a-zA-Z0-9-]+)?\s+(.*))");

        while (std::getline(stream, line)) {
            std::smatch match;
            if (std::regex_match(line, match, option_regex)) {
                std::string opt1 = match[1];
                std::string opt2 = match[2].str();
                std::string desc = match[3];
                if (!opt1.empty()) {
                    bin_info.params[opt1] = desc;
                }
                if (!opt2.empty()) {
                    // clean ", " prefix
                    opt2.erase(0, opt2.find_first_not_of(", "));
                    bin_info.params[opt2] = desc;
                }
            }
        }

        return bin_info;
    }

    static char * completion_generator(const char * text, int state) {
        static std::vector<std::string> matches;
        static size_t                   match_index  = 0;
        std::string                     current_text = rl_line_buffer;

        if (state == 0) {
            // During initialization, compute the actual matches for 'text' and keep
            // them in a static vector.
            matches.clear();
            match_index         = 0;
            std::string textstr = std::string(text);

            if (current_text.size() > 0 && current_text.back() == ' ') {
                auto result = find_by_bin_or_path(instance->system_binaries_, current_text, textstr);

                if (result.has_value()) {
                    matches.push_back(result.value().bin);
                }
                for (const auto & builtIn : instance->custom_commands_) {
                    if (builtIn.first.size() >= textstr.size() &&
                        builtIn.first.compare(0, textstr.size(), textstr) == 0) {
                        matches.push_back(builtIn.first);
                    }
                }
            } else {
                // Collect a vector of matches: vocabulary words that begin with text.

                for (const auto & word : instance->vocabulary) {
                    if (word.size() >= textstr.size() && word.compare(0, textstr.size(), textstr) == 0) {
                        matches.push_back(word);
                    }
                }
                for (const auto & word : instance->system_binaries_) {
                    if (word.second.bin.size() >= textstr.size() &&
                        word.second.bin.compare(0, textstr.size(), textstr) == 0) {
                        matches.push_back(word.second.bin);
                    }
                }
            }
        }

        if (match_index >= matches.size()) {
            // We return nullptr to notify the caller no more matches are available.
            return nullptr;
        }  // Return a malloc'd char* for the match. The caller frees it.
        return strdup(matches[match_index++].c_str());
    }

    static char ** rl_completion(const char * text, int start, int end) {
        // Don't do filename completion even if our generator finds no matches.
        //rl_attempted_completion_over = 1;

        return rl_completion_matches(text, SimpleShell::completion_generator);
    }

    void LoadSystemBinaries();

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
                    } else {
                        base_path.insert(0, current_dir + "/");
                    }
                }
                std::string pattern_with_wildcard = base_path;
                pattern_with_wildcard += "/";
                pattern_with_wildcard += pattern;
                std::string matched_files = glob_files(pattern_with_wildcard, base_path + "/");

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

    bool custom_command_add(const std::string & command, const std::vector<std::string> & params,
                            const std::string & description, const SimpleShell::custom_command_type & type) const;

    bool custom_command_add(const SimpleShell::custom_command & command);

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

    bool config_delete_section_variable(const std::string & section, const std::string & key, bool flush = false) {
        if (this->config_map_.contains(section)) {
            auto & section_map = this->config_map_.at(section);
            if (section_map.contains(key)) {
                section_map.erase(key);

                if (flush) {
                    this->writeConfig();
                }
                return true;
            }
        }
        return false;
    }

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

    static void reload_config(const std::vector<std::string> & /*args*/) {
        instance->readConfig();
        instance->loadEnvironmentVariables();
        instance->parse_variables();
        instance->format_prompt();
        std::cout << "Configuration reloaded." << '\n';
    }

    static void cd(const std::vector<std::string> & args) {
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
            instance->env_set("PWD", path, SimpleShell::variable_type::SL_VAR_GLOBAL);
        }
    }

    static void echo(const std::vector<std::string> & args) {
        for (size_t i = 1; i < args.size(); ++i) {
            std::cout << args[i] << (i == args.size() - 1 ? "" : " ");
        }
        std::cout << '\n';
    }

    static void alias(const std::vector<std::string> & args) {
        if (args.size() < 2 || args[1] == "list") {
            for (const auto & cfg : instance->config_get_section_variables("aliases")) {
                std::cout << cfg.key << " = " << cfg.value << '\n';
            }
            return;
        }
        if (args.size() > 2 && args[1] == "add") {
            instance->config_set_section_variable("aliases", args[2], args[3], true);
            std::cout << "alias " << args[2] << " added" << '\n';
            return;
        }
        if (args.size() > 2 && args[1] == "delete") {
            if (instance->config_delete_section_variable("aliases", args[2])) {
                std::cout << "alias " << args[2] << " deleted" << '\n';
            } else {
                std::cerr << "alias " << args[2] << " not found" << '\n';
            }
        }
    }

    static void unalias(const std::vector<std::string> & args) {
        if (args.size() < 2) {
            std::cerr << "unalias: missing argument" << '\n';
            return;
        }
        if (instance->config_delete_section_variable("aliases", args[1])) {
            std::cout << "alias " << args[1] << " deleted" << '\n';
        } else {
            std::cerr << "alias " << args[1] << " not found" << '\n';
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
            for (const auto & plugin : instance->plugin_manager->getPlugins()) {
                std::cout << "ID: " << plugin.first << "\t\t";
                std::cout << "Name: " << plugin.second.displayName << '\t';
                std::cout << "Status: " << (plugin.second.enabled ? "active" : "disabled") << '\n';
                if (plugin.second.description.empty()) {
                    std::cout << "No description available.\n";
                } else {
                    std::cout << plugin.second.description << '\n';
                }
                std::cout << "-------------------------------------\n";
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

    static void jobs(const std::vector<std::string> & args) {
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
