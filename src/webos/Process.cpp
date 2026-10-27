/**
 * @file Process.cpp
 * @brief Implementation of Process.h.
 */
#include "Process.h"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>

namespace webmon
{

int RunProcess(const std::vector<std::string> &argv, std::string *output)
{
    if (argv.empty())
        return -1;

    int pipeFds[2] = { -1, -1 };
    if (pipe(pipeFds) != 0)
        return -1;

    // Build argv before forking: only async-signal-safe calls are allowed in the child.
    std::vector<char *> args;
    args.reserve(argv.size() + 1);
    for (const std::string &arg : argv)
        args.push_back(const_cast<char *>(arg.c_str()));
    args.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) {
        close(pipeFds[0]);
        close(pipeFds[1]);
        return -1;
    }
    if (pid == 0) {
        dup2(pipeFds[1], STDOUT_FILENO);
        const int devNull = open("/dev/null", O_WRONLY);
        if (devNull >= 0)
            dup2(devNull, STDERR_FILENO);
        close(pipeFds[0]);
        close(pipeFds[1]);
        execv(args[0], args.data());
        _exit(127);
    }

    close(pipeFds[1]);
    std::array<char, 4096> buffer{};
    std::string captured;
    for (;;) {
        const ssize_t length = read(pipeFds[0], buffer.data(), buffer.size());
        if (length > 0) {
            if (output)
                captured.append(buffer.data(), static_cast<std::size_t>(length));
            continue;
        }
        if (length < 0 && errno == EINTR)
            continue;
        break;
    }
    close(pipeFds[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR)
            return -1;
    }
    if (output)
        *output = std::move(captured);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

bool IsExecutable(const std::string &path)
{
    return access(path.c_str(), X_OK) == 0;
}

} // namespace webmon
