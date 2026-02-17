# CLAUDE.md — arxiv-tui

Guidance for AI assistants working in this repository.

---

## Project Overview

**arxiv-tui** is a C++17 terminal user interface (TUI) for browsing and managing arXiv research papers. It fetches articles from arXiv RSS feeds, persists them in a local SQLite database, and presents an interactive keyboard-driven interface built with FTXUI.

---

## Repository Layout

```
arxiv-tui/
├── CMakeLists.txt          # Root CMake config (C++17, options, dependencies)
├── Dependencies.txt        # CPM package declarations
├── CMake/                  # CMake modules (CPM, sanitizers, warnings, etc.)
├── include/Arxiv/          # Public headers (.hh)
│   ├── App.hh              # ArxivApp — TUI shell and event loop
│   ├── AppCore.hh          # AppCore — business logic, filtering, state
│   ├── Article.hh          # Article data structure
│   ├── Components.hh       # FTXUI component wrappers
│   ├── Config.hh           # YAML config loader/saver
│   ├── DatabaseManager.hh  # SQLite3 persistence interface
│   ├── Fetcher.hh          # arXiv RSS fetcher / PDF downloader
│   └── KeyBindings.hh      # Keyboard action mapping
├── src/Arxiv/              # Implementation files (.cc) + src CMakeLists.txt
│   ├── main.cc             # Entry point
│   ├── App.cc
│   ├── AppCore.cc
│   ├── Components.cc
│   ├── Config.cc
│   ├── DatabaseManager.cc
│   ├── Fetcher.cc
│   └── KeyBindings.cc
└── test/
    ├── CMakeLists.txt      # Catch2 + trompeloeil setup
    ├── unit/               # Unit test files
    ├── fixtures/           # Shared test data (test_data.hh)
    └── mocks/              # trompeloeil mock classes
```

---

## Build System

### Requirements
- CMake ≥ 3.17
- C++17-capable compiler (GCC or Clang)
- SQLite3 (system package)
- CURL (optional system package; bundled via CPM if absent)
- Internet access on first build (CPM downloads dependencies)

### External Dependencies (via CPM)

| Library | Version | Purpose |
|---------|---------|---------|
| fmt | 11.0.0 | String formatting |
| spdlog | v1.14.1 | Structured logging |
| cpr | 1.10.4 | HTTP client (arXiv API) |
| FTXUI | main | Terminal UI framework |
| pugixml | 1.15.0 | XML/RSS feed parsing |
| yaml-cpp | 0.7.0 | YAML config parsing |
| SQLite3 | system | Article persistence |

### CMake Targets

| Target | Type | Description |
|--------|------|-------------|
| `libarxiv-tui` | Static library | All application logic (no main) |
| `arxiv-tui` | Executable | Links `libarxiv-tui` + `main.cc` |
| `unit_tests` | Executable | Catch2 test runner |

### Build Commands

```bash
# Standard build (no tests)
cmake -B build
cmake --build build

# Build with tests
cmake -B build -DARXIV_TUI_ENABLE_TESTING=ON
cmake --build build

# Build with coverage
cmake -B build -DARXIV_TUI_ENABLE_TESTING=ON -DARXIV_TUI_COVERAGE=ON
cmake --build build

# Run the application
./build/src/Arxiv/arxiv-tui

# Run tests
cd build && ctest --output-on-failure

# Or run the test binary directly
./build/test/unit_tests
```

`compile_commands.json` is generated automatically for IDE/clangd integration.

---

## Architecture

### Initialization Flow

```
main()
  └── Config(".arxiv-tui.yml")       # Load/create YAML config
       └── ArxivApp(config)
             ├── AppCore(config, DatabaseManager, Fetcher)
             │     └── FetchArticles()  # Populate DB from arXiv RSS
             └── SetupUI()             # Build FTXUI component tree
                   └── screen.Loop()   # Event loop
```

### Key Classes

**`ArxivApp`** (`App.hh/cc`)
TUI shell. Owns the FTXUI `ScreenInteractive`, all UI `Component` objects, and the background refresh thread. Delegates all data operations to `AppCore`. Manages dialogs (project, date range, search, help).

**`AppCore`** (`AppCore.hh/cc`)
Business logic. Receives `DatabaseManager` and `Fetcher` via `unique_ptr` (dependency injection). Manages filter state, article index, date range, search query, and project membership. Triggers UI refresh via callbacks.

**`DatabaseManager`** (`DatabaseManager.hh/cc`)
SQLite3 persistence layer. All virtual methods — mock-friendly for testing.

**`Fetcher`** (`Fetcher.hh/cc`)
Parses arXiv RSS/XML feeds via `cpr` + `pugixml`. Downloads PDFs. All virtual methods.

**`Config`** (`Config.hh/cc`)
Loads `.arxiv-tui.yml` on startup; creates defaults if missing. Stores topics, download directory, and key bindings.

**`KeyBindings`** (`KeyBindings.hh/cc`)
Maps string action names to `ftxui::Event` instances from the config.

**`Article`** (`Article.hh`)
Plain data struct: `link`, `title`, `authors`, `abstract`, `date` (Unix timestamp), `bookmarked`.

### Database Schema

```sql
CREATE TABLE articles (
    link       TEXT PRIMARY KEY,
    title      TEXT,
    authors    TEXT,
    abstract   TEXT,
    date       INTEGER,   -- Unix timestamp
    bookmarked INTEGER DEFAULT 0
);

CREATE TABLE projects (
    name TEXT PRIMARY KEY
);

CREATE TABLE project_articles (
    project_name TEXT REFERENCES projects(name),
    article_link TEXT REFERENCES articles(link),
    PRIMARY KEY (project_name, article_link)
);
```

### UI Layout

```
┌─ Filter ─┬──────── Articles ─────────┬──── Detail ────┐
│ All       │ [Title]   Authors   Date  │ Full title     │
│ Bookmarks │ ...                       │ Authors        │
│ Today     │                           │ Abstract       │
│ Range     │                           │                │
│ Search    │                           │                │
│ [projects]│                           │                │
└───────────┴───────────────────────────┴────────────────┘
```

---

## Default Key Bindings (configurable in YAML)

| Key | Action |
|-----|--------|
| `j` / `k` | Next / previous article |
| `h` / `l` | Move focus left / right between panes |
| `b` | Toggle bookmark |
| `d` | Download article PDF |
| `a` | Toggle detail view |
| `p` | Open project assignment dialog |
| `x` | Delete project |
| `r` | Set date range filter |
| `/` | Open search dialog |
| `?` | Toggle help overlay |
| `q` | Quit |

---

## Configuration File

Location: `.arxiv-tui.yml` (working directory). Created with defaults on first run.

```yaml
articles:
  download_dir: downloads/
  topics:
    - hep-ph
    - hep-ex
    - hep-lat
    - hep-th
key_bindings:
  - action: next_article
    key: j
  - action: prev_article
    key: k
  # ...
```

Runtime files:
- `articles.db` — SQLite database (created at runtime)
- `arxiv_tui.log` — spdlog output (created at runtime)

---

## Testing

### Framework
- **Catch2** v3.5.0 — test runner and assertions
- **trompeloeil** v47 — mock objects

### Test Files

| File | What it tests |
|------|--------------|
| `test/unit/DatabaseManagerTest.cc` | Article persistence, bookmarking, project CRUD |
| `test/unit/FetcherTest.cc` | RSS parsing, PDF download, error handling |
| `test/unit/AppTest.cc` | AppCore filtering (category, date, search), project/bookmark logic |

### Mocks
- `test/mocks/DatabaseManagerMock.hh` — mocks all `DatabaseManager` virtual methods
- `test/mocks/FetcherMock.hh` — mocks `Fetcher` virtual methods

### Fixtures
- `test/fixtures/test_data.hh` — sample `Article` objects and a mock RSS XML string used across all tests

### Adding a Test
1. Write test cases in the appropriate `test/unit/` file using `TEST_CASE` / `SECTION` macros.
2. Use the existing mocks via trompeloeil `REQUIRE_CALL` / `ALLOW_CALL`.
3. Rebuild with `-DARXIV_TUI_ENABLE_TESTING=ON` and run `ctest`.

---

## Code Conventions

### Naming
| Kind | Convention | Example |
|------|-----------|---------|
| Classes | PascalCase | `ArxivApp`, `DatabaseManager` |
| Methods | PascalCase | `FetchArticles`, `ToggleBookmark` |
| Private member variables | `m_` prefix + snake_case | `m_current_articles`, `m_filter_index` |
| Local variables | snake_case | `article_link`, `filter_options` |
| Constants | snake_case or ALL_CAPS | `scroll_speed`, `MAX_VISIBLE` |
| Namespaces | lowercase | `Arxiv` |

### Header Style
- `#ifndef ARXIV_<NAME>` guards in `.hh` files (older style); `#pragma once` is also used — be consistent with the file being edited.
- All public declarations live in `include/Arxiv/`.
- Implementation files mirror the path under `src/Arxiv/`.

### Memory Management
- Use `std::unique_ptr` for owned heap objects (`DatabaseManager`, `Fetcher`).
- RAII throughout; no raw `new`/`delete`.

### Error Handling
- Throw exceptions for unrecoverable errors (e.g., database open failure).
- Log errors with `spdlog` at `error` level.
- Use try-catch in network/download operations.
- Avoid returning error codes; prefer exceptions or `std::optional`.

### Testability
- `DatabaseManager` and `Fetcher` expose all methods as `virtual` so they can be mocked.
- `AppCore` receives both via constructor injection (`unique_ptr`) — never construct them inside `AppCore`.

### Logging
- Use `spdlog` — the logger is initialised in `main.cc` before `ArxivApp` is constructed.
- Log levels: `trace` for detailed flow, `info` for significant events, `error` for failures.

### UI (FTXUI)
- UI components are built in `ArxivApp::SetupUI()`.
- A background `std::thread` drives periodic UI refresh via `screen.PostEvent(Event::Custom)`.
- Key events are dispatched through the `event_handler` component which checks `KeyBindings` before falling through to child components.
- `TESTING` preprocessor macro is defined when building tests; use it to skip UI-only code paths if needed.

---

## Common Development Tasks

### Add a New arXiv Topic
Edit `.arxiv-tui.yml` and add the topic string under `articles.topics`. Topics correspond to arXiv category identifiers (e.g., `cs.LG`, `quant-ph`).

### Add a New Key Binding
1. Define the action string in `KeyBindings.hh/.cc`.
2. Handle the action in `ArxivApp`'s event handler in `App.cc`.
3. Add a default entry in `Config.cc` (or document it for users to add to YAML).

### Add a New Filter
1. Add a filter name to `AppCore::RefreshFilterOptions()`.
2. Handle the filter index in `AppCore::SetFilterIndex()` (update `m_current_articles`).
3. Update `ArxivApp` event handling if the filter requires a dialog.

### Add a New Database Table/Column
1. Modify the `CREATE TABLE` statements in `DatabaseManager.cc`.
2. Add corresponding getter/setter virtual methods to `DatabaseManager.hh`.
3. Update `DatabaseManagerMock.hh` to mock the new methods.
4. Write tests in `DatabaseManagerTest.cc`.

---

## What to Avoid

- Do not add non-virtual methods to `DatabaseManager` or `Fetcher` that need mocking — tests depend on virtual dispatch.
- Do not construct `DatabaseManager` or `Fetcher` inside `AppCore`; always inject them.
- Do not mix UI logic into `AppCore` — it should remain testable without FTXUI.
- Do not use `using namespace std;` globally; prefer explicit `std::` qualifiers.
- Do not commit `articles.db`, `arxiv_tui.log`, or the `build/` directory.
