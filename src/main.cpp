#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>  // getenv, setenv, unsetenv
#include <cstring>

#include "SimpleShell.hpp"

int main(int argc, char * argv[]) {
    setpgid(0, 0);
    tcsetpgrp(STDIN_FILENO, getpgrp());
    tcsetpgrp(STDOUT_FILENO, getpgrp());
    tcsetpgrp(STDERR_FILENO, getpgrp());

    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);

    SimpleShell shell;
    signal(SIGINT, SimpleShell::signal_handler_wrapper);
    signal(SIGTSTP, SimpleShell::signal_handler_wrapper);
    signal(SIGCHLD, SimpleShell::signal_handler_wrapper);

    std::vector<std::string> params = {};
    std::string              runnable;
    if (argc > 1) {
        std::string arg = argv[1];
        if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [OPTION]...\n"
                      << "Simple shell\n\n"
                      << "  -h, --help     display this help and exit\n"
                      << "  -v, --version  output version information and exit\n"
                      << utils::ENDLINE;
            return 0;
        }
        if (arg == "--version") {
            std::cout << "Simple shell 1.0\n"
                      << "Copyright (C) 2023 Simple shell contributors\n"
                      << "License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>\n"
                      << "This is free software: you are free to change and redistribute it.\n"
                      << "There is NO WARRANTY, to the extent permitted by law.\n"
                      << utils::ENDLINE;
            return 0;
        }
        runnable = arg[1];
        for (int i = 2; i < argc; ++i) {
            params.push_back(argv[i]);
        }
    }

    shell.run(runnable, params);
    return 0;
}
