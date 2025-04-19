## 0.1.0 (2025-04-19)

First release with the following updates since initial prototype:

### Added
- Register redirections and pipeline children in `ProcessManager`, so all forked processes (simple, redirected, or pipelined) are tracked.
- Updated `jobs` builtin to only list background jobs (and a separate stopped section) with numbered job IDs `[1]`, `[2]`, etc.
- Enhanced `bg` and `fg` builtins to accept explicit job numbers (with or without `%` prefix) and default to the most recent stopped job.
- Centralized SIGTSTP handling to suspend all foreground jobs and move them to background via `ProcessManager`.
- Built-in `echo` now supports `-n` (no trailing newline) and `-e` (interpret backslash escapes) flags.
- Added `docs/TESTCASES.md` with manual test scenarios for job control and pipeline handling.

### Changed
- Improved `jobs` output format to separate running background jobs and stopped jobs, each with numbering and clear status.
- Refactored process launch paths to consistently set process types and states in `ProcessManager`.

### Removed
- Old changelog entries from initial draft.