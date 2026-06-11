# arxiv-tui

[![Build & Test](https://github.com/jxi24/arxiv-tui/actions/workflows/ci.yml/badge.svg)](https://github.com/jxi24/arxiv-tui/actions/workflows/ci.yml)
[![Sanitizers](https://github.com/jxi24/arxiv-tui/actions/workflows/sanitizers.yml/badge.svg)](https://github.com/jxi24/arxiv-tui/actions/workflows/sanitizers.yml)
[![Static Analysis](https://github.com/jxi24/arxiv-tui/actions/workflows/static-analysis.yml/badge.svg)](https://github.com/jxi24/arxiv-tui/actions/workflows/static-analysis.yml)
[![Coverage](https://codecov.io/gh/jxi24/arxiv-tui/branch/main/graph/badge.svg)](https://codecov.io/gh/jxi24/arxiv-tui)
[![REUSE compliant](https://api.reuse.software/badge/github.com/jxi24/arxiv-tui)](https://api.reuse.software/info/github.com/jxi24/arxiv-tui)

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
- **Configurable article list columns** — choose which columns appear in the article list (title, authors, date, category, score, id) and in what order via `article_columns` in `config.yml`
- **Scrolling detail pane** — read full titles and abstracts without leaving the terminal
- **Personalised ranking** — rate articles 1–5 stars with `n`; bulk-rate an entire selection with `W`; a lightweight neural network learns your preferences and surfaces today's most relevant papers in the Recommended filter
- **Open in browser** — press `O` to open the focused article (or all selected articles) directly in the system default browser via `xdg-open`
- **BibTeX export** — generate `.bib` files for individual articles, selections, or entire projects, with automatic InspireHEP lookup and metadata fallback
- **Read/unread tracking** — articles are marked read when the detail pane opens, when you navigate while the detail pane is open, or when a PDF is downloaded; read articles render dimmer so unread papers stand out; an **Unread** filter shows everything not yet read
- **Auto-refresh** — configurable background feed refresh interval (0 = disabled)
- **`--fetch` headless mode** — `arxiv-tui --fetch` updates the database and exits without opening the TUI, enabling cron-based refresh
- **Database pruning** — optional `max_article_age_days` config key automatically removes old articles on startup unless they are bookmarked, rated, or in a project
- **Replay system and crash handler** — all UI actions are recorded to a JSONL replay log; on a crash, a report with backtrace and full replay is saved for debugging
- **Link deduplication** — incoming RSS and Atom feeds are normalised to a canonical URL form on ingestion, and any existing duplicates are cleaned up automatically on first run
- **Undo delete** — press `u` to restore the last deleted article (or entire bulk-deleted selection) including its rating, project memberships, notes, and tags; a configurable ring buffer (default 10 steps) keeps the last N operations available for undo
- **Help overlay search** — type while the `?` overlay is open to filter key bindings in real time; Backspace trims the query, Escape clears it, a second Escape closes the overlay
- **Export digest as archive** — `G` packs the selected-articles digest (Markdown + PDFs) into a portable `.tar.gz` file

---

## Screenshots

![arxiv-tui interface](assets/tui-overview.png)

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
article_columns:
  - title
  - date
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

**`article_columns`** controls which columns are shown in the article list and in what order. Available values:

| Column | Description | Width |
|--------|-------------|-------|
| `title` | Article title (scrolls on the focused row) | flexible |
| `date` | Publication date (`YYYY-MM-DD`) | 10 |
| `authors` | Author list (truncated) | 24 |
| `category` | Primary arXiv category (e.g. `hep-ph`) | 8 |
| `id` | arXiv identifier (e.g. `2403.12345`) | 12 |
| `score` | Predicted ranking score (Recommended view only) | 8 |

Default: `[title, date]`. The `title` column is strongly recommended; without it the list shows only metadata. Example for a narrow terminal focused on category browsing:

```yaml
article_columns:
  - title
  - category
  - date
```

---

## Key Bindings

### Navigation

| Key | Action |
|-----|--------|
| `j` / `k` | Next / previous article |
| `h` / `l` | Move focus left / right between panes |
| `a` | Toggle detail view |
| `?` | Toggle help overlay (type to filter bindings) |
| `q` | Quit |

### Article actions

| Key | Action |
|-----|--------|
| `Space` | Toggle selection on current article |
| `b` | Bookmark current article (or all selected if a selection is active) |
| `D` | Delete current article (or all selected), with confirmation |
| `u` | Undo the last delete (restores article, rating, project memberships, and tags) |
| `d` | Download article PDF |
| `O` | Open article in the default browser (`xdg-open`) |
| `n` | Rate focused article 1–5 stars (always rates the single focused article) |
| `c` | Export article as BibTeX |
| `N` | Edit per-article note (within a project) |

### Selection and bulk actions

Select any number of articles with `Space`. While a selection is active, the article pane header shows `[N selected]`.

| Key | Bulk behaviour |
|-----|---------------|
| `b` | Bookmark all selected articles |
| `W` | Rate all selected articles with a single score |
| `O` | Open all selected articles in the default browser |
| `p` | Open project dialog in bulk-add mode — confirm links all selected to the checked projects |
| `D` | Delete all selected articles (confirmation required) |
| `g` | Export selected articles as a Markdown digest + PDF bundle |
| `G` | Pack the digest output into a `.tar.gz` archive |
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
| `K` | Edit interest keywords (cold-start ranking) |

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

1. **Rate articles** — press `n` on any article to give it a score from 1 to 5 stars. `n` always rates the single focused article regardless of any active selection. To rate an entire selection at once, press `W` instead — this opens a "Rate Selection" dialog and applies the chosen score to all selected articles.
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
| **REUSE** | Every file carries a valid SPDX `FileCopyrightText` and `License-Identifier` (checked via `pip install reuse && reuse lint`) |

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
- **Bulk rating** (v0.9) — with a selection active, a single dialog applies the chosen score to all selected articles in one operation, triggering a single model retrain check (binding changed to `W` in v1.1)
- **Documentation site** (v0.9.2) — Sphinx docs site published to GitHub Pages; versioned by tag with a root redirect to the latest release
- **Undo delete** (v0.9.4) — `u` restores the last deleted article or bulk-deleted selection, including its rating, project memberships, notes, and tags; backed by a configurable ring buffer (`undo_buffer_size`, default 10)
- **Help overlay search** (v0.9.6) — type while the `?` overlay is open to filter key bindings in real time (case-insensitive substring match); Backspace trims, first Escape clears the query, second closes
- **Export digest as archive** (v0.9.6) — `G` packs the selected-digest directory (Markdown + PDFs) into a `.tar.gz` alongside it; `KeyBindings::filter_bindings(query)` drives both the help overlay and is available to any future UI that needs filtered binding lists
- **Configurable article list columns** (v0.9.7) — `article_columns` config key selects which columns appear in the article list and in what order; available: `title`, `date`, `authors`, `category`, `id`, `score`; default `[title, date]`; a column header row is always shown
- **Settings dialog** (v1.0) — `S` opens a five-section in-app settings editor (General, Topics, Ranker, Export, Keys) allowing every config value to be changed without editing YAML; Escape saves and closes
- **Category filter** (v1.0) — `t` opens a dialog to toggle individual arXiv categories on or off; only articles matching an active category are shown; `a` activates all, `n` deactivates all; articles with no category always pass through
- **Obsidian vault export** (v1.0) — `o` exports selected articles as Obsidian-formatted Markdown notes (wikilinks, PDF embeds, frontmatter) into the configured `obsidian_vault` directory; vault path is set via the Settings dialog or directly in `config.yml`
- **Keyword boosts** (v1.0) — `K` opens the interest-keyword editor; keywords are saved to a plain-text file and used by the ranker as a cold-start signal before any star ratings exist; useful for seeding recommendations immediately after install
- **Open in browser** (v1.1) — `O` opens the focused article in the system default browser via `xdg-open`; with a selection active, all selected articles are opened; links are drawn from the selection set if non-empty, otherwise the focused article
- **Separate rate-article and rate-selection bindings** (v1.1) — `n` now always rates the single focused article regardless of any active selection; `W` (configurable) is the dedicated "Rate Selection" binding that opens the bulk-rating dialog; this allows rating individual papers while a multi-article selection is active

---

## Contributing

Contributions are welcome. Please open an issue to discuss a feature or bug before submitting a pull request. When working on the codebase, see [CLAUDE.md](CLAUDE.md) for architecture notes, naming conventions, and testing guidelines.

All new behaviour must be developed using TDD — write a failing test first, then implement the minimum code to make it pass.

---

## License

This project is licensed under the [GNU General Public License v3.0](LICENSES/GPL-3.0-only.txt) (GPLv3). You are free to use, modify, and distribute this software under the terms of the GPLv3.
