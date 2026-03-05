from __future__ import annotations

import numpy as np
from typing_extensions import TypeAlias

CVIAbsoluteTimeBase: TypeAlias = np.void
"""Type alias for the base type of :any:`CVIAbsoluteTimeDType`, which is :any:`numpy.void`."""

CVIAbsoluteTimeDType = np.dtype((CVIAbsoluteTimeBase, [("lsb", np.uint64), ("msb", np.int64)]))
"""NumPy structured data type for a ``CVIAbsoluteTime`` C struct."""

CVITimeIntervalBase: TypeAlias = np.void
"""Type alias for the base type of :any:`CVITimeIntervalDType`, which is :any:`numpy.void`."""

CVITimeIntervalDType = np.dtype((CVITimeIntervalBase, [("lsb", np.uint64), ("msb", np.int64)]))
"""NumPy structured data type for a ``CVITimeInterval`` C struct."""
