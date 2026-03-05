from __future__ import annotations

import numpy as np
from typing_extensions import TypeAlias

ComplexInt32Base: TypeAlias = np.void
"""Type alias for the base type of :any:`ComplexInt32DType`, which is :any:`numpy.void`."""

ComplexInt32DType = np.dtype((ComplexInt32Base, [("real", np.int16), ("imag", np.int16)]))
"""NumPy structured data type for a complex integer with 16-bit ``real`` and ``imag`` fields."""
