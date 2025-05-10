#ifndef SIMPLESHELL_UTILS_H
#define SIMPLESHELL_UTILS_H
#include <unistd.h>

#include <filesystem>
#include <string>
#include <vector>

class SimpleShell;

namespace utils {

static const char ENDLINE = '\n';

class ConfigUtils {
  public:
    static std::string escape(const std::string & input) {
        std::string result;
        bool        inside_quotes = false;  //

        for (char c : input) {
            if (c == '\"') {
                inside_quotes = !inside_quotes;  //
            }

            if (inside_quotes) {
                result += c;  //
            } else {
                switch (c) {
                    case ' ':
                        result += "\\ ";  //
                        break;
                    case '\\':
                        result += "\\\\";  //
                        break;
                    case utils::ENDLINE:
                        result += "\\n";  //
                        break;
                    case '\r':
                        result += "\\r";  //
                        break;
                    case '\t':
                        result += "\\t";  //
                        break;
                    case '\b':
                        result += "\\b";  //
                        break;
                    case '\f':
                        result += "\\f";  //
                        break;
                    case '=':
                        result += "\\=";  //
                        break;
                    case '#':
                        result += "\\#";  //
                        break;
                    case ';':
                        result += "\\;";  //
                        break;
                    case '\"':
                        result += "\\\"";  //
                        break;
                    default:
                        if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E) {
                            // Nem-printelhető karaktert hexával escape-elünk
                            char buf[5];
                            snprintf(buf, sizeof(buf), "\\x%02X", static_cast<unsigned char>(c));
                            result += buf;
                        } else {
                            result += c;
                        }
                        break;
                }
            }
        }
        return result;
    }

    static std::string unescape(const std::string & input) {
        std::string result;
        bool        inside_quotes = false;  //

        for (size_t i = 0; i < input.length(); ++i) {
            if (input[i] == '\"') {
                inside_quotes = !inside_quotes;  //
            }

            if (inside_quotes) {
                result += input[i];  //
            } else {
                if (input[i] == '\\' && i + 1 < input.length()) {
                    char next = input[i + 1];
                    switch (next) {
                        case ' ':
                            result += ' ';  //
                            break;
                        case '\\':
                            result += '\\';  //
                            break;
                        case 'n':
                            result += utils::ENDLINE;  //
                            break;
                        case 'r':
                            result += '\r';  //
                            break;
                        case 't':
                            result += '\t';  //
                            break;
                        case 'b':
                            result += '\b';  //
                            break;
                        case 'f':
                            result += '\f';  //
                            break;
                        case '=':
                            result += '=';  //
                            break;
                        case '#':
                            result += '#';  //
                            break;
                        case ';':
                            result += ';';  //
                            break;
                        case '"':
                            result += '\"';  //
                            break;
                        case 'x':
                            if (i + 3 < input.length()) {
                                std::string hex = input.substr(i + 2, 2);
                                char        ch  = static_cast<char>(std::stoi(hex, nullptr, 16));
                                result += ch;
                                i += 3;         // \xHH -> step 4 chars forward
                            } else {
                                result += 'x';  // wrong \x
                                i++;
                            }
                            break;
                        default:
                            result += next;
                            break;
                    }
                    ++i;
                } else {
                    result += input[i];
                }
            }
        }
        return result;
    }

    static std::string trim_string(const std::string & string) {
        size_t start = string.find_first_not_of(" \t\n\r\f\v");
        size_t end   = string.find_last_not_of(" \t\n\r\f\v");
        if (start == std::string::npos || end == std::string::npos) {
            return "";
        }
        return string.substr(start, end - start + 1);
    };

    static std::pair<std::string, std::string> SplitAtFirstNewline(const std::string & input) {
        size_t pos = input.find(utils::ENDLINE);
        if (pos == std::string::npos) {
            return { input, "" };
        }

        std::string first = input.substr(0, pos);
        std::string rest  = input.substr(pos + 1);

        // Eltávolítjuk az előző \r-t, ha Windows-style sortörés volt (\r\n)
        if (!first.empty() && first.back() == '\r') {
            first.pop_back();
        }

        return { first, rest };
    }
};

static void parse_arguments(const std::string & command, std::vector<std::string> & args) {
    args.clear();
    std::string current_arg;
    bool        in_quotes  = false;
    char        quote_char = 0;

    for (size_t i = 0; i < command.length(); ++i) {
        char c = command[i];

        if ((c == '"' || c == '\'') && !in_quotes) {
            in_quotes  = true;
            quote_char = c;
        } else if (c == quote_char && in_quotes) {
            in_quotes = false;
        } else if ((std::isspace(c) || c == '|') && !in_quotes) {
            if (!current_arg.empty()) {
                args.push_back(current_arg);
                current_arg.clear();
            }
            if (c == '|') {
                args.push_back("|");
            }
        } else {
            current_arg += c;
        }
    }

    if (!current_arg.empty()) {
        args.push_back(current_arg);
    }

    // Wildcard expansion with directory preservation
    std::vector<std::string> expanded_args;
    for (const auto& arg : args) {
        if (arg.find('*') != std::string::npos) {
            glob_t glob_result;
            std::string pattern = arg;
            
            // Separate directory and pattern
            size_t last_slash = pattern.find_last_of("/");
            std::string dir = (last_slash != std::string::npos) ? pattern.substr(0, last_slash + 1) : "";
            std::string file_pattern = (last_slash != std::string::npos) ? pattern.substr(last_slash + 1) : pattern;
            
            glob(file_pattern.c_str(), GLOB_TILDE, NULL, &glob_result);
            for (size_t i = 0; i < glob_result.gl_pathc; ++i) {
                expanded_args.push_back(dir + glob_result.gl_pathv[i]);
            }
            globfree(&glob_result);
        } else {
            expanded_args.push_back(arg);
        }
    }
    args = expanded_args;
}

};  // namespace utils
#endif
