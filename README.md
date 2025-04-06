
# SimpleShell

A lightweight, feature-rich shell implementation with advanced process management capabilities.

## Overview

SimpleShell is a modern, POSIX-compliant shell designed for both usability and extensibility. Built with C++, it offers a robust alternative to traditional shells with enhanced process management, customizable prompts, and intuitive job control.

## Features

### Powerful Process Management

-   **Foreground & Background Execution**: Run commands in the foreground or background with the  `&`  operator
-   **Job Control**: Pause running processes with Ctrl+Z and resume them with  `fg`  or  `bg`  commands
-   **Process Tracking**: Comprehensive tracking of all running and stopped processes

### User Experience

-   **Customizable Prompt**: Personalize your shell experience with color-coded, dynamic prompts
-   **Command History**: Navigate through previous commands with built-in history support
-   **Tab Completion**: Efficiently complete commands and file paths with tab completion

### Configuration

-   **INI Configuration**: Easily customize shell behavior through simple configuration files
-   **Environment Variables**: Set and manage environment variables with intuitive syntax
-   **Aliases**: Create shortcuts for frequently used commands

### Signal Handling

-   **Graceful Interruption**: Properly handles Ctrl+C (SIGINT) and Ctrl+Z (SIGTSTP)
-   **Child Process Management**: Automatically cleans up zombie processes

## Getting Started

### Prerequisites

-   C++ compiler with C++20 support
-   POSIX-compliant operating system (Linux, macOS, etc.)
-   GNU Readline library

### Building from Source

```bash
git clone https://github.com/fszontagh/simpleshell.git
cd simpleshell
mkdir build && cd build
cmake ..
make

```


Send command to Terminal

### Running SimpleShell

```bash
./simpleshell

```

### Reload configuration

```shell
$ reload_config
```

Send command to Terminal

## Usage Examples

### Basic Command Execution

```shell
$ ls -la
$ echo "Hello, world!"
$ cd ~/Documents

```


### Background Processes

```arduino
$ sleep 30 &
Running in background: sleep 30 &
Process 123456 running in background.

```

### Job Control

```shell
$ sleep 60
^Z

$ jobs
Running processes: 1
PID: 123455 status: running, Command: sleep 30
Stopped jobs: 1
PID: 123456 status: stopped, Command: sleep 30

$ bg
Continuing process 123456
Process 123456 completed.

$ fg 123457
Bringing job 123457 to foreground

```


### Custom Prompt

```ini
# In ~/.pshell configuration
[shell]
prompt_format = "${COLOR_GREEN}[${PWD}]${COLOR_RESET}$ "

```
### Example configuration file
```ini
[shell]
prompt_format = "${COLOR_RED}${USER}${COLOR_RESET}@${COLOR_GREEN}${HOSTNAME}${COLOR_RESET} [${FONT_BOLD}${CURRENT_DATE}${COLOR_RESET}] [${PWD}]${SHELL_PROMPT} "

[environment]
HOSTNAME=`hostname -f`

[variables]
CURRENT_DATE=`date '+%Y-%m-%d %H:%M:%S'`
SHELL_PROMPT="$"

[aliases]
ls= "ls -ltrh --color=auto"
ll= "ls -ltr --color=auto"
l= "ls -ltrh --color=auto"
```

## Architecture

SimpleShell is built with a modular architecture:

-   **SimpleShell**: Core shell functionality, command parsing, and execution
-   **ProcessManager**: Handles process creation, tracking, and signal management
-   **Configuration**: Manages user preferences and environment settings

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Acknowledgments

-   Inspired by traditional UNIX shells like Bash and Zsh
-   Built with modern C++ practices for reliability and performance

_"SimpleShell: The power of a full-featured shell with the simplicity you deserve."_