from __future__ import annotations

import numpy as np

from nitypes._numpy import bool as _np_bool

DIGITAL_PORT_DTYPES = (np.uint8, np.uint16, np.uint32)
"""Tuple of types corresponding to :any:`AnyDigitalPort`."""

DIGITAL_STATE_DTYPES = (_np_bool, np.int8, np.uint8)
"""Tuple of types corresponding to :any:`AnyDigitalState`."""
