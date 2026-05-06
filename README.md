# Thalamus

Thalamus is an open-source Python program designed for real-time, synchronized, closed-loop multimodal data capture, specifically tailored to meet the stringent demands of neurosurgical environments.

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

# System Requirements
## Hardware Requirements
Thalamus requires only a standard computer with enough RAM to support the in-memory operations.
External hardware devices for data aquisition are dependent on the goals of individual projects.

## Software Requirements
Thalamus requires Python.

### OS Requirements
We provide auto builds for Linux (glibc 2.35) and Windows (10).

### Python Dependencies
**requirements.txt** includes required dependencies if installing from Github. However, all dependencies have been packaged into the auto builds.

# Installation Guide
## Install from Build
Download appropriate (Windows or Linux) build directly from actions tab or under Releases.

For Windows:

```python -m pip install thalamus-0.3.0-py3-none-win_amd64.whl```

For Linux:

```python -m pip install thalamus-0.3.0-py3-none-manylunux_2_27.whl```

You should now be able to run any of the Thalamus tools

```python -m thalamus.pipeline # Data pipeline, no task controller```

```python -m thalamus.task_controller # Data pipeline and task controller```

```python -m thalamus.hydrate # Convert capture files to sharable formats```

Approximately 1 hour set-up time


# Documentaton
The code respository for Thalamus is hosted on GitHub at https://github.com/cajigaslab/thalamus. For detailed documentation of Thalamus visit https://cajigaslab.github.io/Thalamus/.
For additional examples and generation of figures in our paper, refer to the **SimpleUseCase** folder in the repo.

# License
If you use Thalamus in your work, please remember to cite the repository in any publications.

# Issues
Like all open-source projects, Thalamus will benefit from your involvement, suggestions and contributions. This platform is intended as a repository for extensions to the program based on your code contributions as well as for flagging and tracking open issues. Please use the **Issues** tab as fit.
