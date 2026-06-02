# Thalamus

Thalamus is an open-source program designed for real-time, synchronized, closed-loop multimodal data capture, specifically tailored to meet the stringent demands of neurosurgical environments.

📖 **Full documentation:** https://cajigaslab.github.io/Thalamus/
📦 **Downloads / releases:** https://github.com/cajigaslab/Thalamus/releases
📰 **Published paper:** [*Thalamus: a real-time, closed-loop platform for synchronized multimodal data acquisition* (Communications Engineering, Nature)](https://www.nature.com/articles/s44172-026-00646-z)

# Overview
Thalamus facilitates the advancement of clinical applications of Brain-Computer Interface (BCI) technology by integrating behavioral and electrophysiological data streams. Thalamus prioritizes the following design requirements:
1. Requires minimal setup within an operating room, clinical and research environment and could be easily controlled and quickly modified by the experimenter​
2. Operated with high reliability with few crashes​
3. Fail-safe architecture that guarantees minimal data loss in the setting of a crash​
4. Allows for real-time computation to support visualizations of  research and clinical data streams​
5. Closed-loop control based on research and/or clinical data streams​
6. Acquires synchronous data from the available research and clinical sensors including relevant behavioral, physiologic, and neural sensors that could easily be scaled over time​
7. Supports a high-bandwidth, low latency, parallel distributed architecture for modular acquisition and computation that could easily be upgraded as technology continues to advance​
8. Open-source with source code available to support research use​
9. Embodies best practice in software engineering using unit tests and validation checks​
10. Supports advances in translational applications and, hence, also operates in research domains​

## How it works

Thalamus is built around a **pipeline of nodes**.  Each node is a small, configurable unit that either *generates* data (e.g. a hardware acquisition device or signal generator), *consumes* data (e.g. disk storage), *transforms* data (e.g. eye tracking, math expressions, coordinate mapping), or *controls* the pipeline (e.g. starting/stopping groups of nodes).  You assemble an experiment by adding nodes, configuring them, and subscribing consumers to the producers they care about.  See the [Node Reference](https://cajigaslab.github.io/Thalamus/nodes/index.html) for the full catalog of node types.

Recorded data is written to a compact `.tha` capture file and can be converted to analysis-friendly formats (HDF5, CSV, Parquet, etc.) with the bundled tooling.

# System Requirements
## Hardware Requirements
Thalamus requires only a standard computer with enough RAM to support the in-memory operations.
External hardware devices for data acquisition are dependent on the goals of individual projects.

## Software Requirements
Thalamus requires Python 3.10 or newer.  Drivers and runtimes for integration with third party devices (e.g. GenTL/GenICam cameras, National Instruments DAQs) must be installed separately.

### OS Requirements
We provide auto builds for **Linux** (manylinux), **Windows** (10+), and **macOS** (arm64).

### Python Dependencies
**requirements.txt** lists the dependencies needed when installing from source.  The published wheels bundle all required dependencies.

# Installation Guide
## Install from Build
Download the appropriate wheel for your platform from the [Releases](https://github.com/cajigaslab/Thalamus/releases) page (or the Actions tab).  The package is published as `thalamus_neuro`; the importable module remains `thalamus`.

We recommend installing into a virtual environment so the bundled `grpc` version is not disturbed:

```
python -m venv venv-thalamus
# Linux/macOS
source venv-thalamus/bin/activate
# Windows
call venv-thalamus/scripts/activate
```

Then install the wheel for your platform, for example:

```
# Linux
python -m pip install thalamus_neuro-1.0.15-py3-none-manylinux_2_39_x86_64.whl
# Windows
python -m pip install thalamus_neuro-1.0.15-py3-none-win_amd64.whl
# macOS (arm64)
python -m pip install thalamus_neuro-1.0.15-py3-none-macosx_12_0_arm64.whl
```

You should now be able to run any of the Thalamus tools:

```
python -m thalamus.pipeline          # Data pipeline (no task controller)
python -m thalamus.task_controller   # Data pipeline and task controller
python -m thalamus.hydrate FILE      # Convert a .tha capture file to HDF5
python -m thalamus.dataframe ...      # Export a node's data to CSV/Parquet/etc.
python -m thalamus.record_reader2 FILE  # Inspect the contents of a .tha file
```

Approximately 1 hour set-up time.

# Documentation
The code repository for Thalamus is hosted on GitHub at https://github.com/cajigaslab/thalamus. Detailed documentation lives at https://cajigaslab.github.io/Thalamus/:

- [Quick Start](https://cajigaslab.github.io/Thalamus/quickstart.html) — install, build a pipeline, record, and analyze your first dataset.
- [Concepts and Architecture](https://cajigaslab.github.io/Thalamus/concepts.html) — the node pipeline, data model, capture-file format, and tooling.
- [Examples](https://cajigaslab.github.io/Thalamus/examples/index.html) — runnable, copy-paste tutorials (including a hardware-free walkthrough).
- [Node Reference](https://cajigaslab.github.io/Thalamus/nodes/index.html) — catalog of every node type and its configuration.

Release history is recorded in [CHANGELOG.md](CHANGELOG.md).

Runnable example scripts also live in the [`examples/`](examples/) folder of this repository.  For the figures in our paper, refer to the [`SimpleUseCase`](SimpleUseCase/) folder.

# License
Thalamus is released under the GPL-3.0 license (see [LICENSE](LICENSE)). If you use Thalamus in your work, please cite our paper:

> *Thalamus: a real-time, closed-loop platform for synchronized multimodal data acquisition.* Communications Engineering (Nature). https://www.nature.com/articles/s44172-026-00646-z

# Contributing
Like all open-source projects, Thalamus will benefit from your involvement, suggestions and contributions. This platform is intended as a repository for extensions to the program based on your code contributions as well as for flagging and tracking open issues. Please use the **Issues** tab to report bugs and request features.

See [CONTRIBUTING.md](CONTRIBUTING.md) for the repository layout, how to set up a development environment, how to add a new node type, and the pull-request and release process.
