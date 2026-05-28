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
- **Bookmarks** — mark papers for later reading
- **Projects** — group related articles into named collections with sub-projects, per-article notes, and export/import (Markdown, plain text, JSON)
- **PDF download** — fetch papers directly to a configurable local directory
- **Configurable key bindings** — remap every action via a YAML file
- **Scrolling detail pane** — read full titles and abstracts without leaving the terminal
- **Personalised ranking** — rate articles 1–5 stars; a lightweight neural network learns your preferences and surfaces today's most relevant papers in the Recommended filter
- **BibTeX export** — generate `.bib` files for individual articles, selections, or entire projects, with automatic InspireHEP lookup and metadata fallback
- **Auto-refresh** — configurable background feed refresh interval (0 = disabled)
- **Replay system and crash handler** — all UI actions are recorded to a JSONL replay log; on a crash, a report with backtrace and full replay is saved for debugging

---

## Screenshots

```
┌─ Filter ──────┬──────────────── Articles ──────────────────────┬──── Detail ────┐
│ All           │ Higgs boson production at NLO [4.2★]  Doe  2024│ Higgs boson    │
│ Bookmarks     │ QCD corrections to top pair   [3.8★]  Smith2024│ production at  │
│ Today         │ Lattice QCD at finite density [3.1★]  Lee  2024│ NLO            │
│ Range         │                                                 │                │
│ Search        │                                                 │ J. Doe et al.  │
│ Recommended   │                                                 │                │
│ my-project    │                                                 │                │
└───────────────┴─────────────────────────────────────────────────┴────────────────┘
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

## Building

```bash
# Clone the repository
git clone https://github.com/jxi24/arxiv-tui.git
cd arxiv-tui

# Configure and build
cmake -B build
cmake --build build

# Run
./build/src/Arxiv/arxiv-tui
```

### Build with tests

```bash
cmake -B build -DARXIV_TUI_ENABLE_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

### Build with coverage

```bash
cmake -B build -DARXIV_TUI_ENABLE_TESTING=ON -DARXIV_TUI_COVERAGE=ON
cmake --build build
```

### Build the ImGui/GLFW GUI companion

The GUI target is not built by default (it requires X11 or Wayland headers).
Pass `-DARXIV_TUI_BUILD_GUI=ON` to enable it; Wayland support is auto-detected.

```bash
cmake -B build -DARXIV_TUI_BUILD_GUI=ON
cmake --build build
```

### Replay a crash report

```bash
./build/src/Arxiv/arxiv-tui --replay crash_report.jsonl
```

---

## Configuration

On first launch, arxiv-tui creates `.arxiv-tui.yml` in your working directory:

```yaml
article_settings:
  download_dir: downloads
  topics:
    - hep-ph
    - hep-ex
    - hep-lat
    - hep-th
recommend_threshold: 3.5
retrain_interval: 5
auto_refresh_minutes: 0
scroll_margin: 3
key_mappings:
  - action: next
    key: j
  - action: previous
    key: k
  - action: quit
    key: q
  - action: create_project
    key: p
  - action: delete_project
    key: x
  - action: download_article
    key: d
  - action: show_detail
    key: a
```

**`topics`** accepts any valid arXiv category identifier, e.g. `cs.LG`, `quant-ph`, `math.AG`.

**`download_dir`** is the directory where PDFs are saved. It is created automatically if it does not exist.

**`recommend_threshold`** is the minimum predicted score (1.0–5.0) an article must have to appear in the Recommended filter. Default: `3.5`.

**`retrain_interval`** is the number of new article ratings that must accumulate before the ranking model is automatically retrained. Default: `5`. Press `R` at any time to force an immediate full retrain.

**`auto_refresh_minutes`** is how often the background thread re-fetches the arXiv feeds. Set to `0` to disable automatic refresh. Default: `0`.

**`scroll_margin`** is the number of context lines kept visible above and below the selected article when scrolling. Default: `3`.

---

## Key Bindings

| Key | Action |
|-----|--------|
| `j` / `k` | Next / previous article |
| `h` / `l` | Move focus left / right between panes |
| `b` | Toggle bookmark on selected article |
| `d` | Download article PDF |
| `a` | Toggle detail view |
| `p` | Open project management dialog |
| `x` | Delete selected project |
| `r` | Set date range filter |
| `/` | Open search dialog |
| `n` | Rate selected article (1–5 stars) |
| `R` | Force a full retrain of the ranking model |
| `c` | Export current article as BibTeX |
| `N` | Edit article note (within a project) |
| `e` | Open project export dialog (Markdown / plain text / BibTeX / JSON) |
| `I` | Import a project from JSON |
| `?` | Toggle help overlay |
| `q` | Quit |

All bindings except `R` (force retrain) are remappable in `.arxiv-tui.yml`.

---

## Runtime Files

| File | Description |
|------|-------------|
| `.arxiv-tui.yml` | Configuration (created on first run) |
| `articles.db` | SQLite database of fetched articles and ratings |
| `ranker.bin` | Saved ranking model weights (created after first retrain) |
| `replay.jsonl` | Action replay log for crash reporting |
| `arxiv_tui.log` | Application log (spdlog) |
| `downloads/` | Default PDF download directory |

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

1. **Rate articles** — press `n` on any article to give it a score from 1 to 5 stars.
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

---

## Future Goals

- Read/unread tracking to highlight new papers since last session
- Tag system for cross-project, user-defined labels
- Copy BibTeX entry to clipboard directly from the detail pane
- Auto-update a project's `.bib` file when a new article is added

---

## Contributing

Contributions are welcome. Please open an issue to discuss a feature or bug before submitting a pull request. When working on the codebase, see [CLAUDE.md](CLAUDE.md) for architecture notes, naming conventions, and testing guidelines.

All new behaviour must be developed using TDD — write a failing test first, then implement the minimum code to make it pass.

---

## License

This project is licensed under the [GNU General Public License v3.0](LICENSES/GPL-3.0-only.txt) (GPLv3). You are free to use, modify, and distribute this software under the terms of the GPLv3.
