# gb2-mcp

An [MCP](https://modelcontextprotocol.io) server that exposes GB2's headless
plotting/metrics CLI as tools an AI agent (Claude Desktop, Claude Code, or any
other MCP client) can call directly — no manual command-line invocation
needed.

> **Prefer the built-in server for most cases.** GB2 now speaks MCP itself:
> just run `GB2.exe --mcp` and point your MCP client at it — no Python, no
> separate install. Register it with
> `claude mcp add gb2 -- "C:\path\to\GB2.exe" --mcp`. This `gb2-mcp` Python
> package is the alternative for macOS, or when you want the adapter to live
> outside the app. Both expose the identical tools.

It works by shelling out to the built GB2 executable with the documented
headless flags (`--xvar`/`--yvar`/`--save`/`--metrics`/`--scatter`/
`--scatter-vars`/`--scatter-metrics`/`--boxplot`; see the "Command-Line /
Headless Mode" section of the in-app user manual, `MainWindow.cpp`), and
reports back what was produced. **Works on both macOS and Windows** — the
server auto-detects which OS it's running on and picks the matching binary
path, DSSAT base, and Qt headless setup (see Configuration below).

## Tools

| Tool | Purpose |
|---|---|
| `list_output_files(crop_dir)` | List DSSAT output files (`PlantGro.OUT`, `Evaluate.OUT`, ...) available in a crop directory. |
| `plot_timeseries(crop_dir, output_files, xvar, yvars, save_path, dssat_base?, metrics_path?, boxplot?)` | Render a time-series or box plot to PNG/PDF, optionally with fit-metrics CSV. |
| `plot_scatter(crop_name, save_path, scatter_vars?, scatter_metrics?)` | Render a simulated-vs-measured scatter plot (from `Evaluate.OUT`) to PNG. |
| `read_plot_image(path)` | Return a previously generated PNG so the agent can view it inline. |

## Setup

```bash
cd mcp_server
python3 -m venv .venv
./.venv/bin/pip install -e .
```

This installs the `gb2-mcp` console script into `.venv/bin/gb2-mcp`.

### Configuration (all optional — sensible defaults per OS)

| Env var | Default | Purpose |
|---|---|---|
| `GB2_BINARY` | macOS: `/Applications/DSSAT48/Tools/GB2CPP/build_macos/bin/GB2.app/Contents/MacOS/GB2`<br>Windows: `C:\DSSAT48\Tools\GB2CPP\build_win\bin\GB2.exe` | Path to the built GB2 executable. |
| `GB2_DSSAT_BASE` | macOS: `/Applications/DSSAT48`<br>Windows: `C:/DSSAT48` | DSSAT installation root, used as the first positional arg for time-series plots. |
| `GB2_QT_PLATFORM_PLUGIN_PATH` (macOS only) | auto-detected from Homebrew Qt (`brew install qt`) | Directory containing `libqoffscreen.dylib`. Required for headless (`--save`/`--scatter`) runs — the shipped app bundle only ships the `cocoa` plugin, and headless mode SIGABRTs without this. |
| `GB2_QT_BIN_DIR` (Windows only) | auto-detected under `C:\Qt\<version>\mingw_64\bin` (same version list as `build_and_deploy.bat`) | Qt bin directory, prepended to `PATH` so Qt can find the offscreen platform plugin alongside `qwindows.dll` — mirrors `run_headless.bat`. |

On Windows the server also sets `QT_QPA_FONTDIR=C:\Windows\Fonts` and
`QTFRAMEWORK_BYPASS_LICENSE_CHECK=1` automatically, matching
`run_headless.bat`.

> **Note:** the cross-platform logic (`_default_binary`, `_default_dssat_base`,
> `_find_windows_qt_bin_dir`) was written from `run_headless.bat` and
> `build_and_deploy.bat` and verified end-to-end on macOS, but has not been
> run on an actual Windows machine yet — if a headless call fails there,
> check the tool's `stderr`/`warning` fields first, then adjust `GB2_BINARY`
> / `GB2_QT_BIN_DIR` as needed.

## Registering with an MCP client

### Claude Code (this project)

```bash
claude mcp add gb2 -- /Applications/DSSAT48/Tools/GB2CPP/mcp_server/.venv/bin/gb2-mcp
```

Or add to `.mcp.json` in the project root:

```json
{
  "mcpServers": {
    "gb2": {
      "command": "/Applications/DSSAT48/Tools/GB2CPP/mcp_server/.venv/bin/gb2-mcp"
    }
  }
}
```

### Claude Desktop

Add to `~/Library/Application Support/Claude/claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "gb2": {
      "command": "/Applications/DSSAT48/Tools/GB2CPP/mcp_server/.venv/bin/gb2-mcp"
    }
  }
}
```

Restart Claude Desktop after editing. GB2's tools will then show up under the
🔌 icon, and you can ask things like *"Plot LAI vs DAS for the Wheat crop and
show me the RMSE"* — Claude will call `list_output_files`, then
`plot_timeseries`, then `read_plot_image` on its own.

## Manual test (no MCP client required)

```bash
./.venv/bin/python -c "
from gb2_mcp import server as s
print(s.plot_timeseries(
    crop_dir='/Applications/DSSAT48/Wheat',
    output_files=['PlantGro.OUT'],
    xvar='DAS',
    yvars=['LAID'],
    save_path='/tmp/test.png',
))
"
```

## Known quirks inherited from the CLI

- `plot_scatter`'s underlying `--scatter` mode resolves `crop_name` via GB2's
  own crop-directory mapping (`DETAIL.CDE`), not a filesystem path — pass the
  crop name (e.g. `"Wheat"`), not a directory.
- `CommandLineHandler.cpp` hardcodes a Windows-style DSSAT base
  (`C:/DSSAT48`) internally for scatter mode; this is harmless on macOS
  because crop-folder resolution falls back to a name match, but it means
  `GB2_DSSAT_BASE` has no effect on `plot_scatter` specifically.
