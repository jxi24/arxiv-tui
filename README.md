# arxiv-tui

A keyboard-driven terminal user interface for browsing, managing, and downloading arXiv research papers — built in C++17 with [FTXUI](https://github.com/ArthurSonzogni/FTXUI).

---

## Features

- **Live feed fetching** — pulls articles from arXiv RSS feeds for any set of categories
- **Local persistence** — stores articles in a SQLite database so they remain available offline
- **Filtering** — switch between All Articles, Bookmarks, Today, a custom date range, or a text search
- **Full-text search** — search across titles, authors, and abstracts
- **Bookmarks** — mark papers for later reading
- **Projects** — group related articles into named collections
- **PDF download** — fetch papers directly to a configurable local directory
- **Configurable key bindings** — remap every action via a YAML file
- **Scrolling detail pane** — read full titles and abstracts without leaving the terminal

---

## Screenshots

```
┌─ Filter ─┬──────────────── Articles ─────────────────┬──── Detail ────┐
│ All       │ Higgs boson production at NLO   Doe  2024 │ Higgs boson    │
│ Bookmarks │ QCD corrections to top pair     Smith2024 │ production at  │
│ Today     │ Lattice QCD at finite density   Lee  2024 │ NLO            │
│ Range     │                                           │                │
│ Search    │                                           │ J. Doe et al.  │
│ my-project│                                           │                │
└───────────┴───────────────────────────────────────────┴────────────────┘
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
cd build && ctest --output-on-failure
```

### Build with coverage

```bash
cmake -B build -DARXIV_TUI_ENABLE_TESTING=ON -DARXIV_TUI_COVERAGE=ON
cmake --build build
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
| `?` | Toggle help overlay |
| `q` | Quit |

All bindings are remappable in `.arxiv-tui.yml`.

---

## Runtime Files

| File | Description |
|------|-------------|
| `.arxiv-tui.yml` | Configuration (created on first run) |
| `articles.db` | SQLite database of fetched articles |
| `arxiv_tui.log` | Application log (spdlog) |
| `downloads/` | Default PDF download directory |

---

## Project Structure

```
arxiv-tui/
├── CMakeLists.txt          # Root CMake configuration
├── Dependencies.txt        # CPM external dependency declarations
├── CMake/                  # CMake helper modules
├── include/Arxiv/          # Public headers
│   ├── App.hh              # TUI shell and event loop
│   ├── AppCore.hh          # Business logic, filtering, state
│   ├── Article.hh          # Article data structure
│   ├── Components.hh       # FTXUI component wrappers
│   ├── Config.hh           # YAML config loader/saver
│   ├── DatabaseManager.hh  # SQLite3 persistence interface
│   ├── Fetcher.hh          # arXiv RSS fetcher / PDF downloader
│   └── KeyBindings.hh      # Keyboard action mapping
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
| [SQLite3](https://sqlite.org) | system | Article persistence |
| [Catch2](https://github.com/catchorg/Catch2) | 3.5.0 | Test framework |
| [trompeloeil](https://github.com/rollbear/trompeloeil) | v47 | Mock objects |

All dependencies except SQLite3 are fetched automatically via [CPM](https://github.com/cpm-cmake/CPM.cmake) on first build.

---

## Future Goals

### Article Ranking
- Assign a personal relevance score (1–5 stars) to individual articles
- Sort the article list by score in addition to date
- Persist scores in the SQLite database alongside bookmarks
- Surface highly-rated papers prominently in a dedicated "Top Rated" filter

### Extended Project Management
- Nest projects in a hierarchy (sub-projects / collections)
- Annotate articles with free-form notes scoped to a project
- Export an entire project as a plain-text or Markdown reading list
- Import/export projects as portable JSON for sharing with collaborators

### BibTeX Generation
- Generate a `.bib` file for any selected article, project, or the current filter view
- Populate standard BibTeX fields (`@article`, `author`, `title`, `year`, `eprint`, `archivePrefix`, `primaryClass`, `url`) from the stored metadata
- Copy a single entry to the clipboard directly from the detail pane via a key binding
- Optionally auto-update a project's `.bib` file whenever a new article is added to it

### Other Planned Improvements
- Configurable refresh interval for automatic feed updates
- Support for author-based subscriptions in addition to category feeds
- Tag system for cross-project, user-defined labels
- Read/unread tracking to highlight new papers since last session
- Fuzzy search powered by an in-process search index

---

## Contributing

Contributions are welcome. Please open an issue to discuss a feature or bug before submitting a pull request. When working on the codebase, see [CLAUDE.md](CLAUDE.md) for architecture notes, naming conventions, and testing guidelines.

---

## License

This project does not currently specify a license. All rights reserved by the author unless otherwise noted.
