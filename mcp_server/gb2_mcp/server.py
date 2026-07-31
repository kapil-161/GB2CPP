"""MCP server exposing GB2 (GB2CPP) headless plotting/metrics as agent tools.

GB2 is a Qt desktop app with a headless CLI mode (see CommandLineHandler.cpp /
MainWindow.cpp "Command-Line / Headless Mode" section). This server shells out
to the built executable with the documented headless flags
(--xvar/--yvar/--save/--metrics/--scatter/--scatter-vars/--scatter-metrics/
--boxplot) and reports back what was produced.

Cross-platform: macOS and Windows are both supported (see _default_binary(),
_default_dssat_base(), and _run_gb2() below). Headless runs (--save/--scatter)
need Qt's offscreen platform plugin on both platforms — see
_find_macos_qt_offscreen_plugin_path() / _find_windows_qt_bin_dir().
"""

from __future__ import annotations

import glob
import os
import platform
import subprocess
from pathlib import Path
from typing import Any

from mcp.server.fastmcp import FastMCP, Image

mcp = FastMCP("gb2")

# ---------------------------------------------------------------------------
# Configuration (override via environment variables)
# ---------------------------------------------------------------------------

# Qt versions build_and_deploy.bat auto-detects under C:\Qt — keep in sync.
WINDOWS_QT_VERSIONS = ["6.9.1", "6.8.2", "6.8.1", "6.11.1", "6.11.0", "6.7.1", "6.6.1"]

# Extensions GB2 treats as DSSAT output files (see DataProcessor::prepareOutFiles)
OUTPUT_FILE_EXTENSIONS = {
    "OUT", "OSU", "CSV", "OVT", "OPT", "OPG", "OEB",
    "OEV", "OG2", "OGF", "OLN", "OLC", "OME",
}


def _is_windows() -> bool:
    return platform.system() == "Windows"


def _default_binary() -> str:
    if _is_windows():
        return r"C:\DSSAT48\Tools\GB2CPP\build_win\bin\GB2.exe"
    return "/Applications/DSSAT48/Tools/GB2CPP/build_macos/bin/GB2.app/Contents/MacOS/GB2"


def _default_dssat_base() -> str:
    return "C:/DSSAT48" if _is_windows() else "/Applications/DSSAT48"


def _binary_path() -> Path:
    path = Path(os.environ.get("GB2_BINARY", _default_binary()))
    if not path.is_file():
        raise FileNotFoundError(
            f"GB2 executable not found at {path}. Build it first, or set the "
            "GB2_BINARY environment variable to the correct path."
        )
    return path


def _dssat_base() -> str:
    return os.environ.get("GB2_DSSAT_BASE", _default_dssat_base())


def _find_macos_qt_offscreen_plugin_path() -> str | None:
    """Locate a Homebrew Qt install that ships the offscreen platform plugin.

    The GB2.app bundle only ships the "cocoa" plugin, so headless runs need
    QT_QPA_PLATFORM_PLUGIN_PATH pointed at a full Qt install's plugins/platforms
    directory (containing libqoffscreen.dylib) or the process aborts (SIGABRT).
    """
    override = os.environ.get("GB2_QT_PLATFORM_PLUGIN_PATH")
    if override:
        return override
    for pattern in (
        "/opt/homebrew/Cellar/qt/*/share/qt/plugins/platforms",
        "/usr/local/Cellar/qt/*/share/qt/plugins/platforms",
    ):
        for candidate in sorted(glob.glob(pattern), reverse=True):
            if Path(candidate, "libqoffscreen.dylib").is_file():
                return candidate
    return None


def _find_windows_qt_bin_dir() -> str | None:
    """Locate a Qt mingw_64 install (mirrors run_headless.bat / build_and_deploy.bat).

    Windows headless runs need the Qt bin dir on PATH so Qt can find its
    offscreen platform plugin alongside qwindows.dll.
    """
    override = os.environ.get("GB2_QT_BIN_DIR")
    if override:
        return override
    for version in WINDOWS_QT_VERSIONS:
        candidate = Path(f"C:/Qt/{version}/mingw_64/bin")
        if candidate.is_dir():
            return str(candidate)
    return None


def _run_gb2(args: list[str], timeout_seconds: float) -> dict[str, Any]:
    """Run the GB2 binary in headless (offscreen) mode with the given args."""
    binary = _binary_path()
    env = dict(os.environ)
    env["QT_QPA_PLATFORM"] = "offscreen"
    warning = None

    if _is_windows():
        # Match run_headless.bat: Qt bin on PATH, font dir, license bypass.
        qt_bin = _find_windows_qt_bin_dir()
        if qt_bin:
            env["PATH"] = qt_bin + os.pathsep + env.get("PATH", "")
        else:
            warning = (
                "Could not find a Qt mingw_64 install under C:\\Qt — headless "
                "export may fail to find the offscreen platform plugin. Set "
                "GB2_QT_BIN_DIR to your Qt bin directory "
                "(e.g. C:\\Qt\\6.9.1\\mingw_64\\bin)."
            )
        env.setdefault("QT_QPA_FONTDIR", r"C:\Windows\Fonts")
        env.setdefault("QTFRAMEWORK_BYPASS_LICENSE_CHECK", "1")
    else:
        plugin_path = _find_macos_qt_offscreen_plugin_path()
        if plugin_path:
            env["QT_QPA_PLATFORM_PLUGIN_PATH"] = plugin_path
        else:
            warning = (
                "Could not find a Homebrew Qt offscreen plugin "
                "(libqoffscreen.dylib) — headless export may SIGABRT. Install "
                "Qt via `brew install qt`, or set GB2_QT_PLATFORM_PLUGIN_PATH."
            )

    try:
        proc = subprocess.run(
            [str(binary), *args],
            env=env,
            capture_output=True,
            text=True,
            timeout=timeout_seconds,
        )
        timed_out = False
        returncode = proc.returncode
        stdout, stderr = proc.stdout, proc.stderr
    except subprocess.TimeoutExpired as exc:
        timed_out = True
        returncode = None
        stdout = exc.stdout or ""
        stderr = (exc.stderr or "") + "\n[gb2-mcp] process timed out and was killed"

    result: dict[str, Any] = {
        "command": [str(binary), *args],
        "returncode": returncode,
        "timed_out": timed_out,
        "stdout": stdout.strip(),
        "stderr": stderr.strip(),
    }
    if warning:
        result["warning"] = warning
    return result


def _existing_or_none(path: str) -> str | None:
    return path if Path(path).is_file() else None


# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------


@mcp.tool()
def list_output_files(crop_dir: str) -> list[str]:
    """List DSSAT output files (Overview.OUT, PlantGro.OUT, Evaluate.OUT, ...)
    available in a crop's output directory. Use this to discover valid
    `output_files` values for plot_timeseries before calling it.

    Args:
        crop_dir: Absolute path to the crop's output directory
            (e.g. "/Applications/DSSAT48/Wheat").
    """
    directory = Path(crop_dir)
    if not directory.is_dir():
        raise FileNotFoundError(f"crop_dir does not exist or is not a directory: {crop_dir}")
    files = [
        entry.name
        for entry in directory.iterdir()
        if entry.is_file() and entry.suffix.lstrip(".").upper() in OUTPUT_FILE_EXTENSIONS
    ]
    return sorted(files)


@mcp.tool()
def plot_timeseries(
    crop_dir: str,
    output_files: list[str],
    xvar: str,
    yvars: list[str],
    save_path: str,
    dssat_base: str | None = None,
    metrics_path: str | None = None,
    boxplot: bool = False,
    timeout_seconds: float = 30.0,
) -> dict[str, Any]:
    """Generate a headless time-series (or box) plot from DSSAT model output
    and save it as a PNG (or vector PDF if save_path ends in .pdf).

    Args:
        crop_dir: Absolute path to the crop's output directory
            (e.g. "/Applications/DSSAT48/Wheat"). Use list_output_files first
            if you don't already know the crop's directory/files.
        output_files: DSSAT output filenames within crop_dir to load, e.g.
            ["PlantGro.OUT"]. Get valid names from list_output_files.
        xvar: X-axis variable code, e.g. "DAS" or "DAP".
        yvars: Y-axis variable code(s), e.g. ["LAID", "TOPWT"].
        save_path: Absolute path to write the plot image to (.png or .pdf).
            Parent directories are created if needed.
        dssat_base: DSSAT installation base directory. Defaults to
            "/Applications/DSSAT48" (or the GB2_DSSAT_BASE env var).
        metrics_path: Optional absolute path to also save fit metrics
            (RMSE, R2, d-stat, ...) as CSV.
        boxplot: If true, render as a box plot instead of a line plot.
        timeout_seconds: Max time to wait for the headless run to finish.
    """
    base = dssat_base or _dssat_base()
    Path(save_path).parent.mkdir(parents=True, exist_ok=True)
    if metrics_path:
        Path(metrics_path).parent.mkdir(parents=True, exist_ok=True)

    args = [base, crop_dir, *output_files, "--xvar", xvar, "--yvar", ",".join(yvars), "--save", save_path]
    if metrics_path:
        args += ["--metrics", metrics_path]
    if boxplot:
        args.append("--boxplot")

    result = _run_gb2(args, timeout_seconds)
    result["plot_path"] = _existing_or_none(save_path)
    result["metrics_path"] = _existing_or_none(metrics_path) if metrics_path else None
    result["success"] = result["plot_path"] is not None
    return result


@mcp.tool()
def plot_scatter(
    crop_name: str,
    save_path: str,
    scatter_vars: list[str] | None = None,
    scatter_metrics: list[str] | None = None,
    timeout_seconds: float = 30.0,
) -> dict[str, Any]:
    """Generate a headless simulated-vs-measured scatter plot from a crop's
    Evaluate.OUT file and save it as a PNG.

    Args:
        crop_name: Crop name as GB2 knows it (e.g. "Wheat"), not a filesystem
            path — GB2 resolves this via its own crop-directory mapping.
        save_path: Absolute path to write the plot image to (.png).
        scatter_vars: Variable codes to plot (e.g. ["ADAP", "CWAM"]). If
            omitted, GB2 auto-selects up to 4 from Evaluate.OUT.
        scatter_metrics: Fit statistics to annotate, e.g. ["RMSE", "R2", "d-stat"].
        timeout_seconds: Max time to wait for the headless run to finish.
    """
    Path(save_path).parent.mkdir(parents=True, exist_ok=True)

    args = [crop_name, "--scatter"]
    if scatter_vars:
        args += ["--scatter-vars", ",".join(scatter_vars)]
    if scatter_metrics:
        args += ["--scatter-metrics", ",".join(scatter_metrics)]
    args += ["--save", save_path]

    result = _run_gb2(args, timeout_seconds)
    result["plot_path"] = _existing_or_none(save_path)
    result["success"] = result["plot_path"] is not None
    return result


@mcp.tool()
def read_plot_image(path: str):
    """Return a previously generated plot PNG so it can be viewed inline.

    Args:
        path: Absolute path to a .png file produced by plot_timeseries or
            plot_scatter.
    """
    file_path = Path(path)
    if not file_path.is_file():
        raise FileNotFoundError(f"No such file: {path}")
    return Image(path=str(file_path), format="png")


def main() -> None:
    mcp.run()


if __name__ == "__main__":
    main()
