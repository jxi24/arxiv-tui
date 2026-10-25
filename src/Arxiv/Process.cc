// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/Process.hh"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>

#include "spdlog/spdlog.h"

extern char** environ;

namespace Arxiv::Process {

namespace {

// Start argv with stdout/stderr on /dev/null and stdin on `stdin_fd` (or
// /dev/null when it is -1). Returns the child's pid, or -1.
pid_t spawn(const std::vector<std::string>& argv, int stdin_fd = -1) {
    if (argv.empty())
        return -1;

    std::vector<char*> args;
    args.reserve(argv.size() + 1);
    for (const auto& a : argv)
        args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    if (stdin_fd >= 0)
        posix_spawn_file_actions_adddup2(&actions, stdin_fd, STDIN_FILENO);
    else
        posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    pid_t pid = -1;
    int rc = posix_spawnp(&pid, args[0], &actions, nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (rc != 0) {
        spdlog::warn("[Process]: Cannot start '{}': {}", argv[0], std::strerror(rc));
        return -1;
    }
    return pid;
}

int wait_for(pid_t pid) {
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR)
            return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

} // namespace

int RunAndWait(const std::vector<std::string>& argv) {
    pid_t pid = spawn(argv);
    return pid < 0 ? -1 : wait_for(pid);
}

int RunWithInput(const std::vector<std::string>& argv, const std::string& input) {
    // A socket rather than a pipe: send(MSG_NOSIGNAL) reports a reader that
    // went away as EPIPE instead of raising SIGPIPE in the whole process.
    int sv[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) != 0)
        return -1;
    pid_t pid = spawn(argv, sv[1]);
    ::close(sv[1]);
    if (pid < 0) {
        ::close(sv[0]);
        return -1;
    }
    const char* data = input.data();
    std::size_t left = input.size();
    while (left > 0) {
        ssize_t n = ::send(sv[0], data, left, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break; // the program stopped reading; its exit status decides
        data += n;
        left -= static_cast<std::size_t>(n);
    }
    ::close(sv[0]);
    return wait_for(pid);
}

bool SpawnDetached(const std::vector<std::string>& argv) {
    pid_t pid = spawn(argv);
    if (pid < 0)
        return false;
    // Reap the child when it exits so it never lingers as a zombie.
    std::thread([pid] { wait_for(pid); }).detach();
    return true;
}

} // namespace Arxiv::Process
