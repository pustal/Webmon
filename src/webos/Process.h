/**
 * @file Process.h
 * @brief Runs helper programs (curl, luna-send) directly, without a shell, so
 *        file names and URLs never need quoting.
 */
#ifndef WEBMON_PROCESS_H
#define WEBMON_PROCESS_H

#include <string>
#include <vector>

namespace webmon
{

/**
 * Runs @p argv (argv[0] is the program's absolute path) and waits for it.
 * stdout is captured into @p output when given; stderr is discarded.
 * @return The exit status, or -1 if the program could not be started or was killed.
 */
int RunProcess(const std::vector<std::string> &argv, std::string *output = nullptr);

/** True if @p path exists and is executable. */
bool IsExecutable(const std::string &path);

} // namespace webmon

#endif // WEBMON_PROCESS_H
