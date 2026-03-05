from __future__ import annotations

import operator
from typing import SupportsFloat, SupportsIndex

import numpy as np
import numpy.typing as npt

from nitypes._exceptions import (
    invalid_arg_type,
    invalid_arg_value,
    unsupported_arg,
    unsupported_dtype,
)
from nitypes._numpy import isdtype as _np_isdtype

# Some of these doctests use types introduced in NumPy 2.0 (np.long and np.ulong) or highlight
# formatting differences between NumPy 1.x and 2.x (e.g. dtype=int32, 1.23 vs. np.float64(1.23)).
__doctest_requires__ = {("arg_to_float", "is_dtype", "validate_dtype"): ["numpy>=2.0"]}


def arg_to_float(
    arg_description: str, value: SupportsFloat | None, default_value: float | None = None
) -> float:
    """Convert an argument to a float.

    >>> arg_to_float("xyz", 1.234)
    1.234
    >>> arg_to_float("xyz", 1234)
    1234.0
    >>> arg_to_float("xyz", np.float64(1.234))
    np.float64(1.234)
    >>> arg_to_float("xyz", np.float32(1.234))  # doctest: +ELLIPSIS
    1.233999...
    >>> arg_to_float("xyz", 1.234, 5.0)
    1.234
    >>> arg_to_float("xyz", None, 5.0)
    5.0
    >>> arg_to_float("xyz", None)
    Traceback (most recent call last):
    ...
    TypeError: The xyz must be a floating point number.
    <BLANKLINE>
    Provided value: None
    >>> arg_to_float("xyz", "1.234")
    Traceback (most recent call last):
    ...
    TypeError: The xyz must be a floating point number.
    <BLANKLINE>
    Provided value: '1.234'
    """
    if value is None:
        if default_value is None:
            raise invalid_arg_type(arg_description, "floating point number", value)
        value = default_value

    if not isinstance(value, float):
        try:
            # Use value.__float__() because float(value) also accepts strings.
            return value.__float__()
        except Exception:
            raise invalid_arg_type(arg_description, "floating point number", value) from None

    return value


def arg_to_int(
    arg_description: str, value: SupportsIndex | None, default_value: int | None = None
) -> int:
    """Convert an argument to a signed integer.

    >>> arg_to_int("xyz", 1234)
    1234
    >>> arg_to_int("xyz", 1234, -1)
    1234
    >>> arg_to_int("xyz", None, -1)
    -1
    >>> arg_to_int("xyz", None)
    Traceback (most recent call last):
    ...
    TypeError: The xyz must be an integer.
    <BLANKLINE>
    Provided value: None
    >>> arg_to_int("xyz", 1.234)
    Traceback (most recent call last):
    ...
    TypeError: The xyz must be an integer.
    <BLANKLINE>
    Provided value: 1.234
    >>> arg_to_int("xyz", "1234")
    Traceback (most recent call last):
    ...
    TypeError: The xyz must be an integer.
    <BLANKLINE>
    Provided value: '1234'
    """
    if value is None:
        if default_value is None:
            raise invalid_arg_type(arg_description, "integer", value)
        value = default_value

    if not isinstance(value, int):
        try:
            return operator.index(value)
        except Exception:
            raise invalid_arg_type(arg_description, "integer", value) from None

    return value


def arg_to_uint(
    arg_description: str, value: SupportsIndex | None, default_value: int | None = None
) -> int:
    """Convert an argument to an unsigned integer.

    >>> arg_to_uint("xyz", 1234)
    1234
    >>> arg_to_uint("xyz", 1234, 5000)
    1234
    >>> arg_to_uint("xyz", None, 5000)
    5000
    >>> arg_to_uint("xyz", -1234)
    Traceback (most recent call last):
    ...
    ValueError: The xyz must be a non-negative integer.
    <BLANKLINE>
    Provided value: -1234
    >>> arg_to_uint("xyz", "1234")
    Traceback (most recent call last):
    ...
    TypeError: The xyz must be an integer.
    <BLANKLINE>
    Provided value: '1234'
    """
    value = arg_to_int(arg_description, value, default_value)
    if value < 0:
        raise invalid_arg_value(arg_description, "non-negative integer", value)
    return value


def is_dtype(dtype: npt.DTypeLike, supported_dtypes: tuple[npt.DTypeLike, ...]) -> bool:
    """Check a dtype-like object against a tuple of supported dtype-like objects.

    Unlike :any:`numpy.isdtype`, this function supports structured data types.

    >>> is_dtype(np.float64, (np.float64, np.intc, np.long,))
    True
    >>> is_dtype("float64", (np.float64, np.intc, np.long,))
    True
    >>> is_dtype(np.float64, (np.byte, np.short, np.intc, np.int_, np.long, np.longlong))
    False
    >>> a_type = np.dtype([('a', np.int32)])
    >>> b_type = np.dtype([('b', np.int32)])
    >>> is_dtype(a_type, (np.float64, np.int32, a_type,))
    True
    >>> is_dtype(b_type, (np.float64, np.int32, a_type,))
    False
    >>> is_dtype("i2, i2", (np.float64, np.int32, a_type,))
    False
    >>> is_dtype("i4", (np.float64, np.int32, a_type,))
    True
    """
    if not isinstance(dtype, (type, np.dtype)):
        dtype = np.dtype(dtype)

    if isinstance(dtype, np.dtype) and dtype.fields:
        return dtype in supported_dtypes

    return _np_isdtype(dtype, supported_dtypes)


def validate_dtype(dtype: npt.DTypeLike, supported_dtypes: tuple[npt.DTypeLike, ...]) -> None:
    """Validate a dtype-like object against a tuple of supported dtype-like objects.

    >>> validate_dtype(np.float64, (np.float64, np.intc, np.long,))
    >>> validate_dtype("float64", (np.float64, np.intc, np.long,))
    >>> validate_dtype(np.float64, (np.byte, np.short, np.intc, np.int_, np.long, np.longlong))
    Traceback (most recent call last):
    ...
    TypeError: The requested data type is not supported.
    <BLANKLINE>
    Data type: float64
    Supported data types: int8, int16, int32, int64
    >>> a_type = np.dtype([('a', np.int32)])
    >>> b_type = np.dtype([('b', np.int32)])
    >>> validate_dtype(a_type, (np.float64, np.int32, a_type,))
    >>> validate_dtype(b_type, (np.float64, np.int32, a_type,))
    Traceback (most recent call last):
    ...
    TypeError: The requested data type is not supported.
    <BLANKLINE>
    Data type: [('b', '<i4')]
    Supported data types: float64, int32, void32
    """
    if not is_dtype(dtype, supported_dtypes):
        if not isinstance(dtype, (type, np.dtype)):
            dtype = np.dtype(dtype)
        raise unsupported_dtype("requested data type", dtype, supported_dtypes)


def validate_unsupported_arg(arg_description: str, value: object) -> None:
    """Validate that an unsupported argument is None."""
    if value is not None:
        raise unsupported_arg(arg_description, value)
