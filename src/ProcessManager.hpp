#ifndef PROCESS_MANAGER_HPP
#define PROCESS_MANAGER_HPP

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>  // for shared memory mutex operations
#include <cstdint>    // for uint8_t, uint32_t
#include <cstring>    // for strncpy, memset
#include <cstdio>     // for perror
#include <ctime>      // for time_t
#include <chrono>     // for high-resolution time

// Shared job table across shell instances
static constexpr const char* SHM_NAME = "/simpleshell_jobs";
static constexpr size_t SHM_MAX_JOBS = 256;

// Entry for each job in shared memory
struct SharedProcessEntry {
    bool        in_use;
    pid_t       pid;
    uint8_t     state;       // corresponds to ProcessManager::ProcessState
    uint8_t     type;        // corresponds to ProcessManager::ProcessType
    int         exit_status;
    int64_t     start_time_ms;  // milliseconds since epoch when the process was started
    char        command[256];
};

// Shared region header
struct SharedJobs {
    uint32_t            initialized;
    pthread_mutex_t     mutex;
    size_t              count;
    SharedProcessEntry  entries[SHM_MAX_JOBS];
};

// C++ STL
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <mutex>

// POSIX shared memory
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

class ProcessManager {
  public:
    static ProcessManager & instance() {
        static ProcessManager instance;
        return instance;
    }

    enum ProcessState : std::uint8_t {
        PM_PROC_STATE_RUNNING,
        PM_PROC_STATE_STOPPED,
        PM_PROC_STATE_COMPLETED,
        PM_PROC_STATE_ANY,
    };

    const inline static std::unordered_map<ProcessManager::ProcessState, std::string> process_state_map = {
        { ProcessManager::ProcessState::PM_PROC_STATE_RUNNING,   "running"   },
        { ProcessManager::ProcessState::PM_PROC_STATE_STOPPED,   "stopped"   },
        { ProcessManager::ProcessState::PM_PROC_STATE_COMPLETED, "completed" },
        { ProcessManager::ProcessState::PM_PROC_STATE_ANY,       ""          },
    };

    static std::string statusToString(ProcessManager::ProcessState state) { return process_state_map.at(state); }

    enum ProcessType : std::uint8_t {
        PM_PROC_TYPE_FOREGROUND,
        PM_PROC_TYPE_BACKGROUND,
        PM_PROC_TYPE_ANY,
    };

    const inline static std::unordered_map<ProcessManager::ProcessType, std::string> process_type_map = {
        { ProcessManager::ProcessType::PM_PROC_TYPE_FOREGROUND, "foreground" },
        { ProcessManager::ProcessType::PM_PROC_TYPE_BACKGROUND, "background" },
        { ProcessManager::ProcessType::PM_PROC_TYPE_ANY,        ""           },
    };

    static std::string typeToString(ProcessManager::ProcessType type) { return process_type_map.at(type); }

    struct Process {
        std::string              command;
        std::vector<std::string> args;
        pid_t                    pid;
        ProcessType              type        = ProcessManager::ProcessType::PM_PROC_TYPE_ANY;
        ProcessState             state       = ProcessManager::ProcessState::PM_PROC_STATE_RUNNING;
        bool                     deleted     = false;
        int                      exit_status = 0;
        int64_t                  start_time_ms  = 0;  // milliseconds since epoch when started

        static std::string argsToCommand(const std::vector<std::string> & args) {
            std::string command;
            for (const auto & arg : args) {
                command += arg + " ";
            }
            return command;
        }

        static std::vector<std::string> commandToArgs(const std::string & command) {
            std::vector<std::string> args;
            std::string              arg;
            std::stringstream        ss(command);
            while (ss >> arg) {
                args.push_back(arg);
            }
            return args;
        }
    };

    // Initialize shared memory region on construction
    ProcessManager();

    ~ProcessManager() {
        // Unmap and close shared memory (do not terminate jobs)
        if (shared_jobs_) {
            munmap(shared_jobs_, sizeof(SharedJobs));
            close(shm_fd_);
            shared_jobs_ = nullptr;
            shm_fd_ = -1;
        }
    }

    ProcessManager(const ProcessManager &)             = delete;
    ProcessManager & operator=(const ProcessManager &) = delete;

  public:
    // Shared memory management
    static SharedJobs *         shared_jobs_;
    static int                  shm_fd_;
    static std::once_flag       shm_init_flag;
    static void ensure_shared();

    static void start_process(const std::vector<std::string> & args, bool run_in_background) {
        const auto grpid = getpgrp();
        pid_t      pid   = fork();
        if (pid == -1) {
            perror("Fork failed");
            return;
        }

        if (pid == 0) {
            // Child process
            setpgid(0, 0);  // Create a new process group for the child
            signal(SIGINT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);

            std::vector<char *> c_args(args.size() + 1);
            for (size_t i = 0; i < args.size(); ++i) {
                c_args[i] = const_cast<char *>(args[i].c_str());
            }
            c_args[args.size()] = nullptr;

            if (execvp(c_args[0], c_args.data()) == -1) {
                perror("Exec failed");
                exit(EXIT_FAILURE);
            }
        } else {
            const auto type =
                run_in_background ? ProcessType::PM_PROC_TYPE_BACKGROUND : ProcessType::PM_PROC_TYPE_FOREGROUND;

            setpgid(pid, pid);

            Process proc                         = { Process::argsToCommand(args), args, pid, type };
            proc.state                           = ProcessState::PM_PROC_STATE_RUNNING;
            // Record start time in milliseconds since epoch
            proc.start_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            std::shared_ptr<Process> process_ptr = std::make_shared<Process>(std::move(proc));
            ProcessManager::instance().process_add(process_ptr);

            if (run_in_background) {
                // Handle background process logic
                std::cout << "Process " << pid << " running in background.\n";
                ProcessManager::instance().process_set_type(pid, ProcessType::PM_PROC_TYPE_BACKGROUND);
            } else {
                // Handle foreground process logic
                ProcessManager::process_handle_foreground(pid, grpid);
            }
        }
    }

    bool process_delete(const pid_t & pid, const int & status_code = -1) {
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(processes_mutex_);
            for (auto it = processes_.begin(); it != processes_.end(); ++it) {
                if ((*it)->pid == pid) {
                    (*it)->deleted = true;
                    (*it)->state   = ProcessState::PM_PROC_STATE_COMPLETED;
                    if (status_code != -1 && (*it)->exit_status != status_code) {
                        (*it)->exit_status = status_code;
                    }
                    found = true;
                    break;
                }
            }
        }
        // Remove from shared memory regardless
        ensure_shared();
        pthread_mutex_lock(&shared_jobs_->mutex);
        for (size_t i = 0; i < SHM_MAX_JOBS; ++i) {
            auto & entry = shared_jobs_->entries[i];
            if (entry.in_use && entry.pid == pid) {
                entry.in_use = false;
                shared_jobs_->count = (shared_jobs_->count > 0 ? shared_jobs_->count - 1 : 0);
                break;
            }
        }
        pthread_mutex_unlock(&shared_jobs_->mutex);
        return found;
    }

    bool process_add(std::shared_ptr<Process> & process) {
        std::lock_guard<std::mutex> lock(processes_mutex_);
        for (const auto & _process : processes_) {
            if (_process->pid == process->pid) {
                return false;
            }
        }
        this->processes_.push_back(process);
        // Also add to shared job table
        ensure_shared();
        pthread_mutex_lock(&shared_jobs_->mutex);
        for (size_t i = 0; i < SHM_MAX_JOBS; ++i) {
            auto & entry = shared_jobs_->entries[i];
            if (!entry.in_use) {
                entry.in_use = true;
                entry.pid = process->pid;
                entry.state = static_cast<uint8_t>(process->state);
                entry.type = static_cast<uint8_t>(process->type);
                entry.exit_status = process->exit_status;
                entry.start_time_ms  = process->start_time_ms;
                strncpy(entry.command, process->command.c_str(), sizeof(entry.command) - 1);
                entry.command[sizeof(entry.command) - 1] = '\0';
                shared_jobs_->count++;
                break;
            }
        }
        pthread_mutex_unlock(&shared_jobs_->mutex);
        return true;
    }

    std::shared_ptr<Process> process_get(const pid_t & pid) {
        std::lock_guard<std::mutex> lock(processes_mutex_);
        for (const auto & _process : processes_) {
            if (_process->pid == pid) {
                return _process;
            }
        }
        return nullptr;
    }

    std::shared_ptr<Process> process_get(const pid_t & pid, const ProcessType & type) {
        std::lock_guard<std::mutex> lock(processes_mutex_);
        for (const auto & _process : processes_) {
            if (_process->pid == pid && _process->type == type) {
                return _process;
            }
        }
        return nullptr;
    }

    std::shared_ptr<Process> process_get(const pid_t & pid, const ProcessState & state) {
        std::lock_guard<std::mutex> lock(processes_mutex_);
        for (const auto & _process : processes_) {
            if (_process->pid == pid && _process->state == state) {
                return _process;
            }
        }
        return nullptr;
    }

    std::shared_ptr<Process> process_get(const pid_t & pid, const ProcessState & state, const ProcessType & type) {
        std::lock_guard<std::mutex> lock(processes_mutex_);
        for (const auto & _process : processes_) {
            if (_process->pid == pid && _process->state == state && _process->type == type) {
                return _process;
            }
        }
        return nullptr;
    }

    pid_t process_get_latest_stopped_pid() {
        std::lock_guard<std::mutex> lock(processes_mutex_);
        for (auto it = processes_.rbegin(); it != processes_.rend(); ++it) {
            if ((*it)->state == ProcessManager::ProcessState::PM_PROC_STATE_STOPPED) {
                return (*it)->pid;
            }
        }
        return -1;
    }

    static void process_handle_foreground(const pid_t & pid, const pid_t group_id) {
        bool bring_foreground = false;

        auto proc = ProcessManager::instance().process_get(pid);
        if (!proc) {
            std::cerr << "No such process\n";
            return;
        }

        tcsetpgrp(STDIN_FILENO, pid);
        tcsetpgrp(STDOUT_FILENO, pid);
        tcsetpgrp(STDERR_FILENO, pid);

        if (proc->type == ProcessType::PM_PROC_TYPE_BACKGROUND) {
            bring_foreground = true;
            std::cout << "Bringing job " << pid << " to foreground\n";

            if (proc->state == ProcessState::PM_PROC_STATE_STOPPED) {
                if (kill(-pid, SIGCONT) == -1) {
                    perror("kill(SIGCONT)");
                } else {
                    ProcessManager::instance().process_set_state(pid, ProcessState::PM_PROC_STATE_RUNNING);
                }
            }
        }

        ProcessManager::instance().process_set_type(pid, ProcessType::PM_PROC_TYPE_FOREGROUND);

        pid_t w;
        int   status;

        do {
            w = waitpid(pid, &status, WUNTRACED | WCONTINUED);
            if (w == -1) {
                perror("waitpid");
                break;
            }

            //            std::cout << "(waitpid) Process " << pid << " state: " << ProcessManager::statusToString(proc->state)
            //                      << " type: " << ProcessManager::typeToString(proc->type) << "\n";

            if (WIFEXITED(status)) {
                //printf("%d exited, status=%d\n", pid, WEXITSTATUS(status));
                ProcessManager::instance().process_delete(pid, WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                //printf("%d killed by signal %d\n", pid, WTERMSIG(status));
                ProcessManager::instance().process_delete(pid, WTERMSIG(status));
            } else if (WIFSTOPPED(status)) {
                //printf("%d stopped by signal %d\n", pid, WSTOPSIG(status));
                ProcessManager::instance().process_set_state(pid, ProcessState::PM_PROC_STATE_STOPPED);
                ProcessManager::instance().process_set_type(pid, ProcessType::PM_PROC_TYPE_BACKGROUND);
                break;
            } else if (WIFCONTINUED(status)) {
                //printf("%d continued\n", pid);
                ProcessManager::instance().process_set_exit_status(pid, 0);
            }

        } while (!WIFEXITED(status) && !WIFSIGNALED(status));

        tcsetpgrp(STDIN_FILENO, group_id);
        tcsetpgrp(STDOUT_FILENO, group_id);
        tcsetpgrp(STDERR_FILENO, group_id);
    }

    void process_set_type(const pid_t & pid, const ProcessType & type) {
        std::lock_guard<std::mutex> lock(processes_mutex_);
        for (auto & _process : processes_) {
            if (_process->pid == pid) {
                _process->type = type;
                break;
            }
        }
        // Update shared type
        ensure_shared();
        pthread_mutex_lock(&shared_jobs_->mutex);
        for (size_t i = 0; i < SHM_MAX_JOBS; ++i) {
            auto & entry = shared_jobs_->entries[i];
            if (entry.in_use && entry.pid == pid) {
                entry.type = static_cast<uint8_t>(type);
                break;
            }
        }
        pthread_mutex_unlock(&shared_jobs_->mutex);
    }

    void process_set_state(const pid_t & pid, const ProcessState & state) {
        std::lock_guard<std::mutex> lock(processes_mutex_);
        for (auto & _process : processes_) {
            if (_process->pid == pid) {
                _process->state = state;
                break;
            }
        }
        // Update shared state
        ensure_shared();
        pthread_mutex_lock(&shared_jobs_->mutex);
        for (size_t i = 0; i < SHM_MAX_JOBS; ++i) {
            auto & entry = shared_jobs_->entries[i];
            if (entry.in_use && entry.pid == pid) {
                entry.state = static_cast<uint8_t>(state);
                break;
            }
        }
        pthread_mutex_unlock(&shared_jobs_->mutex);
    }

    void process_set_exit_status(const pid_t & pid, const int & exit_status) {
        std::lock_guard<std::mutex> lock(processes_mutex_);
        for (auto & _process : processes_) {
            if (_process->pid == pid) {
                _process->exit_status = exit_status;
                break;
            }
        }
        // Update shared exit status
        ensure_shared();
        pthread_mutex_lock(&shared_jobs_->mutex);
        for (size_t i = 0; i < SHM_MAX_JOBS; ++i) {
            auto & entry = shared_jobs_->entries[i];
            if (entry.in_use && entry.pid == pid) {
                entry.exit_status = exit_status;
                break;
            }
        }
        pthread_mutex_unlock(&shared_jobs_->mutex);
    }

    static void handle_completed_processes() {
        int   status;
        int   wait;
        pid_t pid;

        while (true) {
            pid = wait3(&status, WNOHANG, (struct rusage *) NULL);
            if (pid == 0) {
                return;
            }
            if (pid == -1) {
                return;
            }
            const auto rcode = WEXITSTATUS(status);
            if (ProcessManager::instance().process_delete(pid, rcode)) {
                std::cout << "Process " << pid << " completed.\n";
            }
            return;
        }
    }

    static void send_signal_to_process(pid_t pid, int signal) {
        switch (signal) {
            case SIGKILL:
                std::cout << "Killing process " << pid << "\n";
                if (kill(pid, SIGKILL) != 1) {
                    ProcessManager::instance().process_set_state(pid, ProcessState::PM_PROC_STATE_COMPLETED);
                }
                break;
            case SIGSTOP:
                std::cout << "Stopping process " << pid << "\n";
                if (kill(pid, SIGSTOP) != -1) {
                    ProcessManager::instance().process_set_state(pid, ProcessState::PM_PROC_STATE_STOPPED);
                }
                break;
            case SIGCONT:
                std::cout << "Continuing process " << pid << "\n";
                if (kill(pid, SIGCONT) != -1) {
                    ProcessManager::instance().process_set_state(pid, ProcessState::PM_PROC_STATE_RUNNING);
                }
                break;
            default:
                std::cout << "Unknown signal\n";
                break;
        }
    }

    void send_signal_to_foregound(int signal) {
        std::lock_guard<std::mutex> lock(processes_mutex_);
        for (auto & process : processes_) {
            if (process->state == ProcessState::PM_PROC_STATE_RUNNING &&
                process->type == ProcessType::PM_PROC_TYPE_FOREGROUND) {
                kill(process->pid, signal);
            }
        }
    }

    void send_signal_to_all_processes(int signal) {
        std::lock_guard<std::mutex> lock(processes_mutex_);
        std::cout << "Sending signal " << signal << " to all processes\n";
        for (auto & process : processes_) {
            if (signal == SIGKILL) {
                kill(process->pid, signal);
            }
            if (signal == SIGSTOP && process->state == ProcessState::PM_PROC_STATE_RUNNING) {
                process->state = ProcessState::PM_PROC_STATE_STOPPED;
                kill(process->pid, signal);
            }
            if (signal == SIGCONT && process->state == ProcessState::PM_PROC_STATE_STOPPED) {
                process->state = ProcessState::PM_PROC_STATE_RUNNING;
                kill(process->pid, signal);
            }
        }
    }

    std::vector<Process> get_running_processes() const {
        // Return all running jobs from shared memory
        ensure_shared();
        if (!shared_jobs_) {
            return {};
        }
        std::vector<Process> running;
        pthread_mutex_lock(&shared_jobs_->mutex);
        for (size_t i = 0; i < SHM_MAX_JOBS; ++i) {
            auto & entry = shared_jobs_->entries[i];
            if (entry.in_use && entry.state == static_cast<uint8_t>(ProcessState::PM_PROC_STATE_RUNNING)) {
                Process p;
                p.pid = entry.pid;
                p.command = std::string(entry.command);
                p.args = Process::commandToArgs(p.command);
                p.state = static_cast<ProcessState>(entry.state);
                p.type = static_cast<ProcessType>(entry.type);
                p.exit_status = entry.exit_status;
                p.start_time_ms  = entry.start_time_ms;
                running.push_back(std::move(p));
            }
        }
        pthread_mutex_unlock(&shared_jobs_->mutex);
        return running;
    }

    std::vector<Process> get_stopped_processes() const {
        // Return all stopped jobs from shared memory
        ensure_shared();
        if (!shared_jobs_) {
            return {};
        }
        std::vector<Process> stopped;
        pthread_mutex_lock(&shared_jobs_->mutex);
        for (size_t i = 0; i < SHM_MAX_JOBS; ++i) {
            auto & entry = shared_jobs_->entries[i];
            if (entry.in_use && entry.state == static_cast<uint8_t>(ProcessState::PM_PROC_STATE_STOPPED)) {
                Process p;
                p.pid = entry.pid;
                p.command = std::string(entry.command);
                p.args = Process::commandToArgs(p.command);
                p.state = static_cast<ProcessState>(entry.state);
                p.type = static_cast<ProcessType>(entry.type);
                p.exit_status = entry.exit_status;
                p.start_time_ms  = entry.start_time_ms;
                stopped.push_back(std::move(p));
            }
        }
        pthread_mutex_unlock(&shared_jobs_->mutex);
        return stopped;
    }

    size_t get_running_processes_count() const {
        // Count running jobs in shared memory
        ensure_shared();
        if (!shared_jobs_) {
            return 0;
        }
        size_t count = 0;
        pthread_mutex_lock(&shared_jobs_->mutex);
        for (size_t i = 0; i < SHM_MAX_JOBS; ++i) {
            auto & entry = shared_jobs_->entries[i];
            if (entry.in_use && entry.state == static_cast<uint8_t>(ProcessState::PM_PROC_STATE_RUNNING)) {
                count++;
            }
        }
        pthread_mutex_unlock(&shared_jobs_->mutex);
        return count;
    }

    size_t get_stopped_processes_count() const {
        // Count stopped jobs in shared memory
        ensure_shared();
        if (!shared_jobs_) {
            return 0;
        }
        size_t count = 0;
        pthread_mutex_lock(&shared_jobs_->mutex);
        for (size_t i = 0; i < SHM_MAX_JOBS; ++i) {
            auto & entry = shared_jobs_->entries[i];
            if (entry.in_use && entry.state == static_cast<uint8_t>(ProcessState::PM_PROC_STATE_STOPPED)) {
                count++;
            }
        }
        pthread_mutex_unlock(&shared_jobs_->mutex);
        return count;
    }
  private:
    std::vector<std::shared_ptr<Process>> processes_;
    mutable std::mutex                    processes_mutex_;
};
#endif

// Static member definitions and shared memory init
inline ProcessManager::ProcessManager() {
    ensure_shared();
}
inline SharedJobs * ProcessManager::shared_jobs_ = nullptr;
inline int ProcessManager::shm_fd_ = -1;
inline std::once_flag ProcessManager::shm_init_flag;
inline void ProcessManager::ensure_shared() {
    std::call_once(shm_init_flag, []() {
        // Open or create shared memory
        shm_fd_ = shm_open(SHM_NAME, O_RDWR | O_CREAT, 0666);
        if (shm_fd_ < 0) {
            perror("shm_open");
            return;
        }
        size_t sz = sizeof(SharedJobs);
        if (ftruncate(shm_fd_, sz) == -1) {
            perror("ftruncate");
        }
        void * addr = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
        if (addr == MAP_FAILED) {
            perror("mmap");
            close(shm_fd_);
            shm_fd_ = -1;
            return;
        }
        shared_jobs_ = static_cast<SharedJobs *>(addr);
        // First-time initialization
        if (shared_jobs_->initialized != 1) {
            pthread_mutexattr_t mattr;
            pthread_mutexattr_init(&mattr);
            pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
            pthread_mutex_init(&shared_jobs_->mutex, &mattr);
            shared_jobs_->count = 0;
            for (size_t i = 0; i < SHM_MAX_JOBS; ++i) {
                shared_jobs_->entries[i].in_use = false;
            }
            shared_jobs_->initialized = 1;
        }
    });
}
