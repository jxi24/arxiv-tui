// SPDX-FileCopyrightText: 2024-2026 Josh Isaacson
//
// SPDX-License-Identifier: GPL-3.0-only

#include "Arxiv/Cli.hh"

#include "Arxiv/Version.hh"

#include <string_view>

namespace Arxiv::Cli {

Options Parse(int argc, const char* const* argv) {
    Options out;
    bool fetch = false;
    auto fail = [&](std::string message) {
        if (out.error.empty())
            out.error = std::move(message);
    };
    // An option that takes a file: the next argument, which must exist.
    auto value = [&](int& i, std::string_view flag, std::string& into) {
        if (i + 1 >= argc) {
            fail(std::string(flag) + " needs a file name");
            return;
        }
        into = argv[++i];
    };

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h")
            out.mode = Options::Mode::Help;
        else if (arg == "--version" || arg == "-V")
            out.mode = Options::Mode::Version;
        else if (arg == "--trace")
            out.trace = true;
        else if (arg == "--fetch")
            fetch = true;
        else if (arg == "--replay")
            value(i, arg, out.replay_file);
        else if (arg == "--export-today")
            value(i, arg, out.export_today);
        else if (arg == "--export-yaml")
            value(i, arg, out.export_yaml);
        else if (!arg.empty() && arg.front() == '-')
            fail("unknown option '" + std::string(arg) + "'");
        else
            fail("unexpected argument '" + std::string(arg) + "'");
    }

    // --help / --version win over everything else, even an error.
    if (out.mode == Options::Mode::Help || out.mode == Options::Mode::Version) {
        out.error.clear();
        return out;
    }
    const bool exporting = !out.export_today.empty() || !out.export_yaml.empty();
    const bool replay = !out.replay_file.empty();
    if (int(fetch) + int(exporting) + int(replay) > 1)
        fail("--fetch, --export-today/--export-yaml and --replay cannot be combined; "
             "run them one after another");
    if (fetch)
        out.mode = Options::Mode::Fetch;
    else if (exporting)
        out.mode = Options::Mode::Export;
    else if (replay)
        out.mode = Options::Mode::Replay;
    return out;
}

std::string Usage() {
    return "Usage: arxiv-tui [--trace] [MODE]\n"
           "\n"
           "Browse arXiv papers in the terminal. Without a mode the TUI starts.\n"
           "\n"
           "Modes (one at a time):\n"
           "  --fetch               fetch new papers into the database and exit (for cron)\n"
           "  --export-today FILE   write today's papers as a Markdown digest\n"
           "  --export-yaml FILE    write today's papers, ranked, as YAML\n"
           "                        (--export-today and --export-yaml may be combined)\n"
           "  --replay FILE         replay a replay.jsonl log or a crash report on a copy\n"
           "                        of the database\n"
           "\n"
           "Options:\n"
           "  --trace               log at trace level\n"
           "  -h, --help            show this help and exit\n"
           "  -V, --version         show the version and exit\n";
}

std::string VersionText() { return "arxiv-tui " + std::string(kVersion) + "\n"; }

} // namespace Arxiv::Cli
