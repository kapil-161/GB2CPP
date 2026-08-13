# GB2 regression smoke-test

A tiny automated check that GB2's headless output still produces the right
answers — so an accidental change to plotting/metrics is caught by the computer
instead of by eyeballing plots.

## Run it

Double-click **`run_smoke_test.bat`**, or:

```
python smoke_test.py
```

It renders a few known cases headlessly and compares the **fit-metrics CSV**
(RMSE / d-stat per treatment) against the saved baselines in `baselines/`. It
prints `PASS`/`FAIL` per case, shows an exact diff on any change, and exits
non-zero if anything failed.

## When output *should* change

If you intentionally change how a metric is computed (or the underlying data),
the baselines need refreshing. Review the diff first, then:

```
python smoke_test.py --update
```

Commit the updated `baselines/*.csv` alongside the code change so the new
numbers become the known-good answer.

## What's covered

| Case | Check | Guards against |
|---|---|---|
| `wheat_timeseries` | metrics CSV | time-series RMSE/d, multi-variable |
| `soybean_dismo_sev` | metrics CSV | observed-data matching for disease/pest module outputs (the `EXPERIMENT : …SBX` extension bug) |
| `wheat_scatter` | PNG produced | scatter render doesn't crash |
| `wheat_experiment_grid` | PNG produced | experiment × variable grid renders |

## Requirements

- A built `GB2.exe` (`build_win/bin/` or `manual_deployment/`, or set
  `GB2_BINARY`).
- DSSAT data under `C:/DSSAT48` (override with `GB2_DSSAT_BASE`).
- Python 3 (standard library only — no `pip install`).

Baselines are tied to this dev setup's data and the current GB2 build; they are
a developer regression guard, not a cross-machine conformance suite.
