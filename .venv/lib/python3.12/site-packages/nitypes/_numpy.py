"""NumPy 1.x compatibility shims."""

from __future__ import annotations

import numpy as np

from nitypes._version import parse_version

numpy_version_info = parse_version(np.__version__)
"""The NumPy version as a tuple."""

if numpy_version_info >= (2, 0, 0):
    from numpy import asarray, bool, isdtype, long, ulong
else:
    # mypy warns about this when checking with --platform win32 on Linux, but not on Windows.
    from nitypes._numpy1x import (  # type: ignore[assignment,no-redef,unused-ignore]
        asarray,
        bool,
        isdtype,
        long,
        ulong,
    )


__all__ = [
    "asarray",
    "bool",
    "isdtype",
    "long",
    "numpy_version_info",
    "ulong",
]
