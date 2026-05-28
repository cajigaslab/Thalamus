"""NumPy 1.x compatibility shim implementations."""

from __future__ import annotations

import builtins
import sys
from typing import Any

import numpy as np
import numpy.typing as npt
from numpy import bool_ as bool

# In NumPy 2.x, np.long and np.ulong are equivalent to long and unsigned long in C, following
# the platform's data model.
#
# In NumPy 1.x, np.long is an alias for int and np.ulong does not exist.
# https://numpy.org/doc/1.22/release/1.20.0-notes.html#using-the-aliases-of-builtin-types-like-np-int-is-deprecated
if sys.platform == "win32":
    # 32-bit Windows has an ILP32 data model and 64-bit Windows has an LLP64 data model, so
    # long is 32-bit.
    from numpy import (  # type: ignore[assignment,unused-ignore]
        int32 as long,
        uint32 as ulong,
    )
else:
    # Assume other 32-bit platforms have an ILP32 data model and other 64-bit platforms have an
    # LP64 data model, so long is pointer-sized.
    from numpy import intp as long, uintp as ulong


__all__ = ["asarray", "bool", "isdtype", "long", "ulong"]


def asarray(  # noqa: D103 - missing docstring in public function
    a: npt.ArrayLike, dtype: npt.DTypeLike = None, *, copy: builtins.bool | None = None
) -> npt.NDArray[Any]:
    b = np.asarray(a, dtype)
    made_copy = b is not a and b.base is None
    if copy is True and not made_copy:
        b = np.copy(b)
    if copy is False and made_copy:
        raise ValueError("Unable to avoid copy while creating an array as requested.")
    return b


def isdtype(  # noqa: D103 - missing docstring in public function
    dtype: type[Any] | np.dtype[Any], kind: npt.DTypeLike | tuple[npt.DTypeLike, ...]
) -> builtins.bool:
    if isinstance(kind, tuple):
        return any(dtype == k for k in kind)
    else:
        return dtype == kind
