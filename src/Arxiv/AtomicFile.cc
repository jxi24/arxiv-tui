// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/AtomicFile.hh"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "spdlog/spdlog.h"

namespace Arxiv {

namespace {

bool write_all(int fd, std::string_view content) {
    while (!content.empty()) {
        ssize_t n = ::write(fd, content.data(), content.size());
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        content.remove_prefix(static_cast<std::size_t>(n));
    }
    return true;
}

} // namespace

bool WriteFileAtomically(const std::filesystem::path& path, std::string_view content) {
    namespace fs = std::filesystem;
    // Write to what a symlink points at, so the link survives the rename. (A
    // missing file is not an error here: it is simply created.)
    std::error_code ec;
    fs::path target = path;
    if (fs::is_symlink(fs::symlink_status(path, ec))) {
        target = fs::canonical(path, ec);
        if (ec) {
            spdlog::error("[AtomicFile]: Cannot resolve {}: {}", path.string(), ec.message());
            return false;
        }
    }

    struct stat st{};
    const bool exists = ::stat(target.c_str(), &st) == 0;
    if (exists && !S_ISREG(st.st_mode)) {
        // A device, FIFO or socket (/dev/stdout, /dev/full): renaming over
        // it would replace it with a regular file. Write into it instead.
        int fd = ::open(target.c_str(), O_WRONLY | O_TRUNC | O_CLOEXEC);
        bool ok = fd >= 0 && write_all(fd, content);
        int saved_errno = errno;
        if (fd >= 0)
            ok = (::close(fd) == 0) && ok;
        if (!ok)
            spdlog::error(
                "[AtomicFile]: Writing {} failed: {}", target.string(), std::strerror(saved_errno));
        return ok;
    }
    // Keep the existing file's permissions; a new file gets 0644.
    const mode_t mode = exists ? (st.st_mode & 07777) : 0644;

    std::string temp = target.string() + ".tmp-XXXXXX";
    int fd = ::mkostemp(temp.data(), O_CLOEXEC);
    if (fd < 0) {
        spdlog::error("[AtomicFile]: Cannot create a temporary file for {}: {}",
                      target.string(),
                      std::strerror(errno));
        return false;
    }
    bool ok = write_all(fd, content) && ::fchmod(fd, mode) == 0 && ::fsync(fd) == 0;
    int saved_errno = errno;
    ok = (::close(fd) == 0) && ok;
    if (ok && ::rename(temp.c_str(), target.c_str()) == 0)
        return true;

    spdlog::error("[AtomicFile]: Writing {} failed: {}",
                  target.string(),
                  std::strerror(ok ? errno : saved_errno));
    ::unlink(temp.c_str());
    return false;
}

} // namespace Arxiv
