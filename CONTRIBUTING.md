# Contributing to Thalamus

Thanks for your interest in improving Thalamus! This guide explains how the
project is organized, how to set up a development environment, and how to get
your changes merged.

Thalamus is open-source under the [GPL-3.0 license](LICENSE). By contributing you
agree that your contributions are licensed under the same terms.

## Ways to contribute

- **Report bugs and request features** via the [Issues](https://github.com/cajigaslab/Thalamus/issues) tab.
- **Improve the documentation** (the [`docs/`](docs/) site, the [`README`](README.md), or the runnable [`examples/`](examples/)).
- **Contribute code** — new node types, bug fixes, performance work, or new analysis tooling.

If you are planning a substantial change, please open an issue first to discuss
the approach before investing significant effort.

## Repository layout

| Path | Contents |
| --- | --- |
| [`thalamus/`](thalamus/) | The Python package (importable as `thalamus`). CLI entry points: `pipeline`, `task_controller`, `hydrate`, `dataframe`, `record_reader2`. |
| [`thalamus/pipeline/`](thalamus/pipeline/) | The Qt UI: the main window and the per-node configuration widgets (`*_widget.py`). |
| [`src/thalamus/`](src/thalamus/) | The native C++ implementation of the nodes (one `*_node.cpp` per node type). |
| [`proto/`](proto/) | Protocol Buffer / gRPC definitions shared by the C++ and Python sides. |
| [`docs/`](docs/) | The Sphinx documentation source. |
| [`examples/`](examples/) | Runnable, hardware-free example scripts. |
| [`SimpleUseCase/`](SimpleUseCase/) | Worked use cases and the analyses behind the paper's figures. |

## Setting up a development environment

How much you need to build depends on what you are changing.

### Documentation- or pure-Python-only changes

You do **not** need to build the native extension. Install a released wheel into a
virtual environment and work against it:

```
python -m venv venv-thalamus
source venv-thalamus/bin/activate        # Windows: call venv-thalamus/scripts/activate
python -m pip install thalamus_neuro
```

To build the documentation locally:

```
python -m pip install sphinx sphinxcontrib-video
python -m sphinx -b html docs/source docs/_build
```

### Building from source (native changes)

Building the native extension requires a C++ toolchain, CMake (≥ 3.16), Ninja, and
NASM, plus several third-party libraries. The `prepare.py` script bootstraps these
dependencies for Linux, Windows, and macOS:

```
python -m venv pyenv
source pyenv/bin/activate
python prepare.py            # installs/bootstraps the build toolchain and deps
# On Linux/macOS prepare.py writes environment setup to ~/.thalamusrc:
source ~/.thalamusrc
python -m build -n -w -Crelease
python -m pip install dist/thalamus_neuro-*.whl
```

The build uses the custom `thalamus.build` backend declared in
[`pyproject.toml`](pyproject.toml); protobuf/gRPC stubs are generated automatically
during the build from the files in `proto/`. Use `-Crelease` for an optimized build.

## Running Thalamus

```
python -m thalamus.pipeline          # data pipeline (no task controller)
python -m thalamus.task_controller   # data pipeline and task controller
```

See the [Quick Start](https://cajigaslab.github.io/Thalamus/quickstart.html) for a
guided walkthrough.

## Adding a new node type

Nodes are the unit of extension. A new node generally involves:

1. **C++ node** in `src/thalamus/`: implement the node and give it a unique
   `type_name()` (the string shown in the node-type dropdown). Register it with the
   node graph so it can be instantiated.
2. **Python widget** (optional) in `thalamus/pipeline/`: a `*_widget.py` that
   renders the node's configuration UI, and an entry in the `FACTORY` map in
   `thalamus/pipeline/thalamus_window.py` mapping the type name to the widget and
   its inline property columns.
3. **Documentation**: add a page under `docs/source/nodes/` and link it from
   `docs/source/nodes/index.rst` and the catalog (`docs/source/nodes/catalog.rst`).

Keep configuration field names stable. If a change to a node's config fields would
break existing saved configurations, prefer creating a new numbered node (e.g.
`STORAGE` → `STORAGE2`) rather than breaking the old one.

## Changing the data format / protocol

The wire and file formats are defined in `proto/`. If you modify a `.proto`,
remember that both the C++ and Python sides consume it, and that the `.tha` capture
format must stay backward compatible so existing recordings remain readable.

## Documentation contributions

- Keep documentation **congruent with the code**. Property names and behavior
  described on a node page should match the node's widget and C++ source.
- Example scripts in `examples/` should actually run; please execute them before
  submitting.
- Build the docs locally (see above) and confirm there are no Sphinx warnings for
  the pages you touched.

## Pull requests

1. Fork the repository and create a topic branch for your change.
2. Keep each PR focused on a single concern; include a clear description of what
   changed and why.
3. Open the PR against the repository's default branch (unless a maintainer directs
   otherwise).
4. **Do not bump the version or create release tags** in your PR — releases are
   produced automatically (see below).

## Versioning and releases

- The version lives in [`pyproject.toml`](pyproject.toml) and follows semantic
  versioning. `bump.py {major,minor,patch}` increments it and creates the matching
  `vX.Y.Z` git tag.
- Continuous integration (GitHub Actions workflows in
  [`.github/workflows/`](.github/workflows/)) builds the Linux, Windows, and macOS
  wheels and publishes them to the [Releases](https://github.com/cajigaslab/Thalamus/releases)
  page. Version bumping is performed by CI, so contributors should not do it by hand.

## Citation

If you use Thalamus in your research, please cite our paper:

> *Thalamus: a real-time, closed-loop platform for synchronized multimodal data acquisition.* Communications Engineering (Nature). https://www.nature.com/articles/s44172-026-00646-z
