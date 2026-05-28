from __future__ import annotations

from typing import Any, TypeVar, cast, overload

import numpy as np
import numpy.typing as npt

from nitypes._arguments import validate_dtype
from nitypes._exceptions import unsupported_dtype
from nitypes.complex._dtypes import ComplexInt32DType

_Item_co = TypeVar("_Item_co", bound=Any)
_ScalarType = TypeVar("_ScalarType", bound=np.generic)
_Shape = TypeVar("_Shape", bound=tuple[int, ...])

_COMPLEX_DTYPES = (
    np.complex64,
    np.complex128,
    ComplexInt32DType,
)

_FIELD_DTYPE = {
    np.dtype(np.complex64): np.float32,
    np.dtype(np.complex128): np.float64,
    ComplexInt32DType: np.int16,
}


@overload
def convert_complex(
    requested_dtype: type[_ScalarType] | np.dtype[_ScalarType],
    value: np.ndarray[_Shape, Any],
) -> np.ndarray[_Shape, np.dtype[_ScalarType]]: ...


@overload
def convert_complex(
    requested_dtype: npt.DTypeLike, value: np.ndarray[_Shape, Any]
) -> np.ndarray[_Shape, Any]: ...


# https://numpy.org/doc/2.2/reference/typing.html#d-arrays
# "While thus not strictly correct, all operations are that can potentially perform a 0D-array ->
# scalar cast are currently annotated as exclusively returning an ndarray."
@overload
def convert_complex(
    requested_dtype: type[_ScalarType] | np.dtype[_ScalarType],
    value: np.generic[Any],
) -> np.ndarray[tuple[()], np.dtype[_ScalarType]]: ...


@overload
def convert_complex(
    requested_dtype: npt.DTypeLike,
    value: np.generic[Any],
) -> np.ndarray[tuple[()], Any]: ...


def convert_complex(
    requested_dtype: npt.DTypeLike, value: np.ndarray[_Shape, Any] | np.generic[Any]
) -> np.ndarray[_Shape, Any]:
    """Convert a NumPy array or scalar of complex numbers to the specified dtype.

    Args:
        requested_dtype: The NumPy data type to convert to. Supported data types:
            :any:`numpy.complex64`, :any:`numpy.complex128`, :any:`ComplexInt32DType`.
        value: The NumPy array or scalar to convert.

    Returns:
        The value converted to the specified dtype.
    """
    validate_dtype(requested_dtype, _COMPLEX_DTYPES)
    if requested_dtype == value.dtype:
        return cast(np.ndarray[_Shape, Any], value)
    elif requested_dtype == ComplexInt32DType or value.dtype == ComplexInt32DType:
        # ndarray.view on scalars requires the source and destination types to have the same size,
        # so reshape the scalar into an 1-element array before converting and index it afterwards.
        # shape == () means this is either a scalar (np.generic) or a 0-dimension array, but mypy
        # doesn't know that.
        if value.shape == ():
            return cast(
                np.ndarray[_Shape, Any],
                _convert_complexint32_array(requested_dtype, value.reshape(1))[0],
            )
        else:
            return _convert_complexint32_array(
                requested_dtype, cast(np.ndarray[_Shape, Any], value)
            )
    else:
        return value.astype(requested_dtype)


def _convert_complexint32_array(
    requested_dtype: npt.DTypeLike | type[_ScalarType] | np.dtype[_ScalarType],
    value: np.ndarray[_Shape, Any],
) -> np.ndarray[_Shape, np.dtype[_ScalarType]]:
    if not isinstance(requested_dtype, np.dtype):
        requested_dtype = np.dtype(requested_dtype)

    requested_field_dtype = _FIELD_DTYPE.get(requested_dtype)
    if requested_field_dtype is None:
        raise unsupported_dtype("requested data type", requested_dtype, _COMPLEX_DTYPES)

    value_field_dtype = _FIELD_DTYPE.get(value.dtype)
    if value_field_dtype is None:
        raise unsupported_dtype("array data type", value.dtype, _COMPLEX_DTYPES)

    return value.view(value_field_dtype).astype(requested_field_dtype).view(requested_dtype)
