# Thalamus examples

Runnable, copy-paste examples that accompany the
[documentation](https://cajigaslab.github.io/Thalamus/examples/index.html).

All scripts assume Thalamus is installed (`pip install thalamus_neuro`).

| Script | What it does |
| --- | --- |
| [`synthetic_recording.py`](synthetic_recording.py) | Generate a synthetic `.tha` capture file (a 2 Hz sine and a 1 Hz square pulse) with no acquisition hardware. |
| [`analyze_recording.py`](analyze_recording.py) | Read a `.tha` file with `thalamus.record_reader2` and plot its analog channels. |

## Quick start (no hardware required)

```bash
# 1. Create a synthetic recording
python synthetic_recording.py -o demo.tha

# 2. Plot the channels straight from the .tha file
python analyze_recording.py demo.tha -n wave -o analysis.png

# 3. Export a node's data to CSV (or parquet, etc.)
python -m thalamus.dataframe -n wave -i demo.tha -f csv -o demo.csv

# 4. Or hydrate the whole capture into an HDF5 file for analysis
python -m thalamus.hydrate demo.tha
```

See the [Examples page](https://cajigaslab.github.io/Thalamus/examples/index.html)
in the documentation for a full walkthrough.
