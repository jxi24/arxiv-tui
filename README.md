# arxiv-tui

A keyboard-driven terminal user interface for browsing, managing, and downloading arXiv research papers — built in C++17 with [FTXUI](https://github.com/ArthurSonzogni/FTXUI).

---

## Features

- **Live feed fetching** — pulls articles from arXiv RSS feeds for any set of categories
- **Local persistence** — stores articles in a SQLite database so they remain available offline
- **Filtering** — switch between All Articles, Bookmarks, Today, a custom date range, text search, or Recommended
- **Full-text search** — search across titles, authors, and abstracts
- **Fuzzy search** — in-process fuzzy matching surfaces near-miss results in the search filter
- **Author subscriptions** — follow specific authors in addition to category feeds
- **Bookmarks** — mark papers for later reading; bulk-bookmark multiple articles at once
- **Multi-article selection** — select articles with `Space`, then apply bulk actions: bookmark, add to project, or delete
- **Article deletion** — remove individual articles or entire selections from the local database
- **Projects** — group related articles into named collections with sub-projects, per-article notes, and export/import (Markdown, plain text, JSON)
- **PDF download** — fetch papers directly to a configurable local directory
- **Configurable key bindings** — remap every action via a YAML file
- **Scrolling detail pane** — read full titles and abstracts without leaving the terminal
- **Personalised ranking** — rate articles 1–5 stars; bulk-rate an entire selection at once; a lightweight neural network learns your preferences and surfaces today's most relevant papers in the Recommended filter
- **BibTeX export** — generate `.bib` files for individual articles, selections, or entire projects, with automatic InspireHEP lookup and metadata fallback
- **Read/unread tracking** — articles are marked read when the detail pane opens, when you navigate while the detail pane is open, or when a PDF is downloaded; read articles render dimmer so unread papers stand out; an **Unread** filter shows everything not yet read
- **Auto-refresh** — configurable background feed refresh interval (0 = disabled)
- **`--fetch` headless mode** — `arxiv-tui --fetch` updates the database and exits without opening the TUI, enabling cron-based refresh
- **Database pruning** — optional `max_article_age_days` config key automatically removes old articles on startup unless they are bookmarked, rated, or in a project
- **Replay system and crash handler** — all UI actions are recorded to a JSONL replay log; on a crash, a report with backtrace and full replay is saved for debugging
- **Link deduplication** — incoming RSS and Atom feeds are normalised to a canonical URL form on ingestion, and any existing duplicates are cleaned up automatically on first run

---

## Screenshots

```
┌─ Filter ──────┬──────────────── Articles ─── [2 selected] ──────┬──── Detail ────┐
│ All           │ [*] Higgs boson production   [4.2★]  Doe   2024 │ Higgs boson    │
│ Bookmarks     │ [*] QCD corrections to top   [3.8★]  Smith 2024 │ production at  │
│ Today         │     Lattice QCD at finite    [3.1★]  Lee   2024 │ NLO            │
│ Range         │                                                   │                │
│ Search        │                                                   │ J. Doe et al.  │
│ Recommended   │                                                   │                │
│ my-project    │                                                   │                │
└───────────────┴───────────────────────────────────────────────────┴────────────────┘
```

---

## Requirements

| Dependency | Version | Notes |
|------------|---------|-------|
| CMake | ≥ 3.17 | Build system |
| GCC or Clang | C++17 support | Compiler |
| SQLite3 | any | System package |
| libcurl | any | System package (bundled automatically if absent) |
| Internet | — | Required on first build for CPM package downloads |

---

## Installation

The recommended way to install arxiv-tui for personal use is a user-space
install under `~/.local`, which requires no root access and places the binary
on the standard `$PATH` on most Linux distributions.

```bash
git clone https://github.com/jxi24/arxiv-tui.git
cd arxiv-tui
cmake -B build -DCMAKE_INSTALL_PREFIX=~/.local
cmake --build build -j$(nproc)
cmake --install build
```

After installation the binary is at `~/.local/bin/arxiv-tui`. If `~/.local/bin`
is already on your `PATH` (it is by default on most distributions) you can run
`arxiv-tui` from any directory.

### System-wide install

To install for all users, omit the prefix (defaults to `/usr/local`) and run
the install step as root:

```bash
cmake -B build
cmake --build build -j$(nproc)
sudo cmake --install build
```

### File layout after install

Regardless of prefix, runtime files are placed in standard per-user locations.
`XDG_DATA_HOME` and `XDG_STATE_HOME` override the data and state paths at
runtime; `XDG_CONFIG_HOME` overrides the config path.

| Path | Contents |
|------|----------|
| `~/.config/arxiv-tui/config.yml` | Configuration (created on first run) |
| `~/.local/share/arxiv-tui/articles.db` | Article database and ratings |
| `~/.local/share/arxiv-tui/ranker.bin` | Trained ranking model |
| `~/.local/share/arxiv-tui/downloads/` | Default PDF download directory |
| `~/.local/state/arxiv-tui/arxiv_tui.log` | Rotating application log |
| `~/.local/state/arxiv-tui/replay.jsonl` | UI action replay log |
| `~/.local/state/arxiv-tui/crash_*.txt` | Crash reports with backtrace |

The `~/.local/share` and `~/.local/state` paths above assume a `~/.local`
prefix. A system-wide install uses `/usr/local/share` and `/usr/local/var`
instead, but the per-user config and data files always live under `$HOME`.

---

## Building from source (without installing)

```bash
git clone https://github.com/jxi24/arxiv-tui.git
cd arxiv-tui
cmake -B build
cmake --build build -j$(nproc)

# Run directly from the build tree
./build/src/Arxiv/arxiv-tui
```

### Build with tests

```bash
cmake -B build -DARXIV_TUI_ENABLE_TESTING=ON
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

### Build with coverage

```bash
cmake -B build -DARXIV_TUI_ENABLE_TESTING=ON -DARXIV_TUI_COVERAGE=ON
cmake --build build -j$(nproc)
```

### Build the ImGui/GLFW GUI companion

The GUI target is not built by default (it requires X11 or Wayland headers).
Pass `-DARXIV_TUI_BUILD_GUI=ON` to enable it; Wayland support is auto-detected.

```bash
cmake -B build -DARXIV_TUI_BUILD_GUI=ON
cmake --build build -j$(nproc)
```

### Replay a crash report

```bash
arxiv-tui --replay ~/.local/state/arxiv-tui/crash_20260101_120000_SIGSEGV.txt
```

### Headless feed fetch (cron)

```bash
# Fetch new articles without opening the TUI and exit
arxiv-tui --fetch

# Example cron entry: refresh at 07:00 on weekdays
0 7 * * 1-5  arxiv-tui --fetch
```

---

## Configuration

On first launch, arxiv-tui creates `config.yml` at `~/.config/arxiv-tui/config.yml`:

```yaml
article_settings:
  download_dir: /home/user/.local/share/arxiv-tui/downloads
  topics:
    - hep-ph
    - hep-ex
    - hep-lat
    - hep-th
recommend_threshold: 3.5
retrain_interval: 5
auto_refresh_minutes: 0
scroll_margin: 3
max_article_age_days: 0
key_mappings:
  - action: next
    key: j
  - action: previous
    key: k
  - action: quit
    key: q
  - action: bookmark
    key: b
  - action: create_project
    key: p
  - action: delete_project
    key: x
  - action: delete_article
    key: D
  - action: download_article
    key: d
  - action: show_detail
    key: a
  - action: toggle_selection
    key: " "
```

**`topics`** accepts any valid arXiv category identifier, e.g. `cs.LG`, `quant-ph`, `math.AG`.

**`download_dir`** is the directory where PDFs are saved. It is created automatically if it does not exist.

**`recommend_threshold`** is the minimum predicted score (1.0–5.0) an article must have to appear in the Recommended filter. Default: `3.5`.

**`retrain_interval`** is the number of new article ratings that must accumulate before the ranking model is automatically retrained. Default: `5`. Press `R` at any time to force an immediate full retrain.

**`auto_refresh_minutes`** is how often the background thread re-fetches the arXiv feeds. Set to `0` to disable automatic refresh. Default: `0`.

**`scroll_margin`** is the number of context lines kept visible above and below the selected article when scrolling. Default: `3`.

**`max_article_age_days`** is the maximum age (in days) of articles kept in the database. Articles older than this threshold are deleted on startup unless they are bookmarked, rated, or assigned to a project. Set to `0` to disable pruning entirely. Default: `0`.

---

## Key Bindings

### Navigation

| Key | Action |
|-----|--------|
| `j` / `k` | Next / previous article |
| `h` / `l` | Move focus left / right between panes |
| `a` | Toggle detail view |
| `?` | Toggle help overlay |
| `q` | Quit |

### Article actions

| Key | Action |
|-----|--------|
| `Space` | Toggle selection on current article |
| `b` | Bookmark current article (or all selected if a selection is active) |
| `D` | Delete current article (or all selected), with confirmation |
| `d` | Download article PDF |
| `n` | Rate article 1–5 stars (rates all selected if a selection is active) |
| `c` | Export article as BibTeX |
| `N` | Edit per-article note (within a project) |

### Selection and bulk actions

Select any number of articles with `Space`. While a selection is active, the article pane header shows `[N selected]`.

| Key | Bulk behaviour |
|-----|---------------|
| `b` | Bookmark all selected articles |
| `n` | Rate all selected articles with a single score |
| `p` | Open project dialog in bulk-add mode — confirm links all selected to the checked projects |
| `D` | Delete all selected articles (confirmation required) |
| `g` | Export selected articles as a Markdown digest + PDF bundle |
| `o` | Export selected articles to an Obsidian vault |

### Filtering and search

| Key | Action |
|-----|--------|
| `/` | Open search dialog |
| `r` | Set date range filter (when Date Range filter is active) |
| `t` | Toggle category filter |

The filter pane includes an **Unread** entry that shows only articles not yet read. Articles are marked read automatically when the detail pane is opened, when you scroll through articles while the detail pane is open, or when a PDF is downloaded.

### Projects

| Key | Action |
|-----|--------|
| `p` | Assign/remove current article from projects |
| `x` | Delete the focused project |
| `e` | Export project (Markdown / plain text / BibTeX / JSON) |
| `I` | Import project from JSON |

### Ranking and settings

| Key | Action |
|-----|--------|
| `R` | Force full retrain of the ranking model |
| `S` | Open settings dialog |

All bindings are remappable in `config.yml` via the `key_mappings` section.

---

## Runtime Files

All runtime files are stored in standard per-user directories; see the
[File layout after install](#file-layout-after-install) table for the full
paths. The locations can be overridden with the standard XDG environment
variables: `XDG_CONFIG_HOME`, `XDG_DATA_HOME`, and `XDG_STATE_HOME`.

| File | Description |
|------|-------------|
| `config.yml` | Configuration (created on first run in `~/.config/arxiv-tui/`) |
| `articles.db` | SQLite database of fetched articles, ratings, projects, and notes |
| `ranker.bin` | Saved ranking model weights (created after first retrain) |
| `downloads/` | Default PDF download directory |
| `arxiv_tui.log` | Rotating application log (up to 3 × 5 MB files) |
| `replay.jsonl` | UI action replay log for crash reporting |
| `crash_*.txt` | Crash reports with backtrace and session replay |

---

## Project Structure

```
arxiv-tui/
├── CMakeLists.txt          # Root CMake configuration
├── Dependencies.txt        # CPM external dependency declarations
├── CMake/                  # CMake helper modules
├── LICENSES/               # SPDX license texts (GPL-3.0-only, MIT)
├── REUSE.toml              # REUSE compliance annotations for non-code files
├── include/Arxiv/          # Public headers
│   ├── App.hh              # TUI shell and event loop
│   ├── AppCore.hh          # Business logic, filtering, state
│   ├── Article.hh          # Article data structure
│   ├── Components.hh       # FTXUI component wrappers
│   ├── Config.hh           # YAML config loader/saver
│   ├── DatabaseManager.hh  # SQLite3 persistence interface
│   ├── Fetcher.hh          # arXiv RSS fetcher / PDF downloader
│   ├── FuzzyMatch.hh       # In-process fuzzy search
│   ├── KeyBindings.hh      # Keyboard action mapping
│   ├── Ranker.hh           # TF-IDF + MLP article ranking model
│   ├── Replay.hh           # JSONL action recorder and player
│   └── CrashHandler.hh     # Signal-based crash handler with backtrace
├── src/Arxiv/              # Implementations
└── test/                   # Catch2 unit tests + trompeloeil mocks
```

---

## External Libraries

| Library | Version | Purpose |
|---------|---------|---------|
| [fmt](https://github.com/fmtlib/fmt) | 11.0.0 | String formatting |
| [spdlog](https://github.com/gabime/spdlog) | v1.14.1 | Structured logging |
| [cpr](https://github.com/libcpr/cpr) | 1.10.4 | HTTP client |
| [FTXUI](https://github.com/ArthurSonzogni/FTXUI) | main | Terminal UI framework |
| [pugixml](https://github.com/zeux/pugixml) | 1.15.0 | XML/RSS parsing |
| [yaml-cpp](https://github.com/jbeder/yaml-cpp) | 0.7.0 | YAML configuration |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.11.3 | JSON export/import and replay logs |
| [SQLite3](https://sqlite.org) | system | Article persistence |
| [Catch2](https://github.com/catchorg/Catch2) | 3.5.0 | Test framework |
| [trompeloeil](https://github.com/rollbear/trompeloeil) | v47 | Mock objects |

All dependencies except SQLite3 are fetched automatically via [CPM](https://github.com/cpm-cmake/CPM.cmake) on first build.

---

## Article Ranking

arxiv-tui includes a built-in personalised ranking system that learns from the articles you rate and automatically surfaces the most relevant new papers each day.

### How it works

1. **Rate articles** — press `n` on any article to give it a score from 1 to 5 stars. With a selection active, `n` opens a "Rate Selection" dialog that applies the chosen score to all selected articles at once.
2. **Automatic learning** — once you have rated at least 3 articles, and every `retrain_interval` new ratings thereafter, the model retrains in the background on a separate thread so the TUI stays responsive. A `[Training…]` badge appears in the article pane header while training is in progress, and a `[N rating(s) pending]` counter shows how many more ratings are needed to trigger the next retrain.
3. **Recommended filter** — select **Recommended** in the filter pane to see today's articles that score at or above `recommend_threshold`. Articles are sorted by predicted score, and each entry shows a `[X.X★]` badge.
4. **Force retrain** — press `R` at any time to immediately trigger a full cold-start retrain (rebuilds the vocabulary and resets the network weights). Use this when the corpus has grown significantly or scores feel stale.

### Technical details

The ranker is implemented in pure C++17 with no external ML dependencies:

- **TF-IDF vectorisation** — article text (title weighted 2×, abstract 1×) is tokenised, stop-word filtered, and projected onto the top 512 vocabulary terms by document frequency.
- **2-layer MLP** — input (512) → hidden (32 units, ReLU) → output (1 unit, sigmoid scaled to [1.0, 5.0]). Weights are Xavier-initialised and trained with full-batch SGD + MSE loss for 200 epochs.
- **Warm-start retraining** — threshold-triggered retrains continue from the existing weights rather than re-initialising, so each incremental update builds on prior learning. Force retrain (`R`) performs a cold start with a fresh vocabulary.
- **Persistence** — the trained model (vocabulary map, IDF weights, all network tensors) is saved to `ranker.bin` after every retrain and loaded automatically on startup, so no retraining is needed between sessions.

---

## CI / Code Quality

The repository enforces quality gates on every push and pull request via GitHub Actions:

| Workflow | What it checks |
|----------|---------------|
| **Build & Test** | CMake configure, build, and full test suite |
| **Sanitizers** | ASan + UBSan (memory/UB errors) and TSan (data races), run as separate jobs |
| **Static Analysis** | clang-tidy on project sources only (CPM dependencies are excluded) |
| **clang-format** | Code style checked against `.clang-format` (clang-format v20) |
| **REUSE** | Every file carries a valid SPDX `FileCopyrightText` and `License-Identifier` |

Build artefacts are cached with ccache (per-job namespaces) and CPM dependencies are cached by `Dependencies.txt` hash to keep CI times short.

### Pre-commit hooks

Install [pre-commit](https://pre-commit.com) to run the same checks locally before pushing:

```bash
pip install pre-commit
pre-commit install        # installs git hook
pre-commit run --all-files  # run manually on the whole tree
```

Hooks: trailing whitespace, LF line endings, valid YAML/TOML, clang-format, REUSE lint.

[pre-commit.ci](https://pre-commit.ci) is enabled on the repository and will auto-commit formatting fixes on pull requests.

---

## Completed Milestones

- **Personalised ranking** (v0.3) — TF-IDF + MLP model learns from 1–5 star ratings; Recommended filter surfaces relevant papers; warm-start retraining with persistence to `ranker.bin`
- **Extended project management** (v0.4) — hierarchical sub-projects, per-article notes scoped to a project, export as Markdown / plain text / JSON, import from JSON
- **BibTeX export** (v0.5) — generate `.bib` files for individual articles, selections, or entire projects; InspireHEP API lookup with metadata fallback
- **Replay system and crash handler** (v0.5) — JSONL action recording, `--replay` headless mode, signal-based crash reports with backtrace
- **Auto-refresh and scroll margin** (v0.6) — configurable background feed refresh interval; scroll margin keeps context lines visible around the selected article
- **Fuzzy search** (v0.6) — in-process fuzzy matching in the search filter
- **Author subscriptions** (v0.6) — follow specific authors alongside category feeds
- **REUSE/SPDX compliance** — all source files carry `SPDX-FileCopyrightText` and `SPDX-License-Identifier` headers; `reuse lint` passes clean
- **CI pipeline** — GitHub Actions workflows for build/test, ASan+UBSan, TSan, clang-tidy, clang-format, and REUSE; ccache and CPM caching keep runs fast
- **Multi-article selection and bulk actions** (v0.7) — select articles with `Space`; bulk bookmark, bulk project assignment, and bulk delete with confirmation
- **Article deletion** (v0.7) — delete individual articles or entire selections from the local database; all associated ratings, notes, and project memberships are removed
- **Link deduplication** (v0.7) — arXiv links are normalised to canonical form (`https`, no version suffix) on ingestion; a one-time startup migration merges any existing duplicates in the database
- **Read/unread tracking** (v0.8) — `read_at` timestamp recorded when the detail pane opens, when navigating while it is open, or when a PDF downloads; **Unread** filter shows unseen articles; read articles render dimmer in the list; on-startup DB migration requires no manual action
- **`--fetch` headless mode** (v0.8) — `arxiv-tui --fetch` updates the database and exits, enabling cron-based feed refresh without opening the TUI
- **Database pruning** (v0.8) — `max_article_age_days` config key (default 0 = off) automatically removes old unprotected articles on startup, keeping the database lean
- **Clipboard integration** (v0.9) — pressing `c` copies BibTeX directly to the system clipboard via `xclip`, `xsel`, or `wl-clipboard`; `ARXIV_TUI_CLIPBOARD` env var selects the backend; file export remains available as a fallback
- **FTS5 full-text search** (v0.9) — SQLite FTS5 extension replaces `LIKE '%query%'` with ranked, stemmed full-text search over titles, authors, and abstracts; no new dependency required
- **Tag system** (v0.9) — user-defined labels outside the project hierarchy; articles can carry multiple tags; tags appear as filters alongside projects and are included in BibTeX exports
- **Auto-update project `.bib`** (v0.9) — adding an article to a project that has a previously exported `.bib` automatically appends the new entry without a manual re-export
- **Bulk rating** (v0.9) — `n` with a selection active opens a "Rate Selection" dialog and applies the chosen score to all selected articles in one operation, triggering a single model retrain check

---

## Roadmap to v1.0

### v1.0 — Polish and completeness

The features that make the tool feel finished rather than just functional.

- **Undo for destructive actions** — `u` undoes the last delete or bulk-delete by restoring the article row, its rating, project memberships, and notes from an in-memory ring buffer; the buffer holds the last 10 operations
- **Configurable article list columns** — allow the config to specify which columns appear in the article list (title, authors, date, category, score) and in what order; this makes the layout useful on narrow terminals and for non-physics categories where the arXiv ID is more informative than the date
- **arXiv category autocomplete** — when adding a topic in the settings dialog, offer autocomplete against the full arXiv category taxonomy (a ~200-entry static list bundled at compile time) to prevent silent typos that produce empty feeds
- **Help overlay search** — type to filter the help overlay when the binding count makes scrolling tedious; highlights matching rows
- **Export digest as archive** — wrap the Markdown digest and downloaded PDFs produced by `g` into a `.tar.gz` so the output can be shared as a single file
- **Documentation site** — publish a GitHub Pages site (via a `docs/` directory or a dedicated `gh-pages` branch) covering installation, configuration reference, all key bindings, the ranking system, and a getting-started walkthrough; generated with a static site tool (e.g. MkDocs or mdBook) and deployed automatically by a GitHub Actions workflow on every push to `main`

---

## Contributing

Contributions are welcome. Please open an issue to discuss a feature or bug before submitting a pull request. When working on the codebase, see [CLAUDE.md](CLAUDE.md) for architecture notes, naming conventions, and testing guidelines.

All new behaviour must be developed using TDD — write a failing test first, then implement the minimum code to make it pass.

---

## License

This project is licensed under the [GNU General Public License v3.0](LICENSES/GPL-3.0-only.txt) (GPLv3). You are free to use, modify, and distribute this software under the terms of the GPLv3.
