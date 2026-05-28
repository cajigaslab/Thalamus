from __future__ import annotations

import reprlib
import sys

import numpy as np
import numpy.typing as npt


def add_note(exception: Exception, note: str) -> None:
    """Add a note to an exception.

    >>> try:
    ...     raise ValueError("Oh no")
    ... except Exception as e:
    ...     add_note(e, "p.s. This is bad")
    ...     raise
    Traceback (most recent call last):
    ...
    ValueError: Oh no
    p.s. This is bad
    """
    if sys.version_info >= (3, 11):
        exception.add_note(note)
    else:
        message = exception.args[0] + "\n" + note
        exception.args = (message,) + exception.args[1:]


def invalid_arg_value(
    arg_description: str, valid_value_description: str, value: object
) -> ValueError:
    """Create a ValueError for an invalid argument value."""
    return ValueError(
        f"The {arg_description} must be {_a(valid_value_description)}.\n\n"
        f"Provided value: {reprlib.repr(value)}"
    )


def invalid_arg_type(arg_description: str, type_description: str, value: object) -> TypeError:
    """Create a TypeError for an invalid argument type."""
    return TypeError(
        f"The {arg_description} must be {_a(type_description)}.\n\n"
        f"Provided value: {reprlib.repr(value)}"
    )


def invalid_array_ndim(arg_description: str, valid_value_description: str, ndim: int) -> ValueError:
    """Create a ValueError for an array with an invalid number of dimensions."""
    return ValueError(
        f"The {arg_description} must be {_a(valid_value_description)}.\n\n"
        f"Number of dimensions: {ndim}"
    )


def invalid_requested_type(type_description: str, requested_type: type) -> TypeError:
    """Create a TypeError for an invalid requested type."""
    return TypeError(
        f"The requested type must be {_a(type_description)} type.\n\n"
        f"Requested type: {requested_type}"
    )


def unsupported_arg(arg_description: str, value: object) -> ValueError:
    """Create a ValueError for an unsupported argument."""
    return ValueError(
        f"The {arg_description} argument is not supported.\n\n"
        f"Provided value: {reprlib.repr(value)}"
    )


def unsupported_dtype(
    arg_description: str, dtype: npt.DTypeLike, supported_dtypes: tuple[npt.DTypeLike, ...]
) -> TypeError:
    """Create a TypeError for an unsupported dtype."""
    # Remove duplicate names because distinct types (e.g. int vs. long) may have the same name
    # ("int32").
    supported_dtype_names = {np.dtype(d).name: None for d in supported_dtypes}.keys()
    return TypeError(
        f"The {arg_description} is not supported.\n\n"
        f"Data type: {np.dtype(dtype)}\n"
        f"Supported data types: {', '.join(supported_dtype_names)}"
    )


def int_out_of_range(value: int, min: int, max: int) -> OverflowError:
    """Create an OverflowError when an int is out of the specified range."""
    raise OverflowError(
        "The input value is out of range.\n\n"
        f"Requested value: {value}\n"
        f"Minimum value: {min}\n",
        f"Maximum value: {max}",
    )


# English-specific hack. This is why we prefer "Key: value" for localizable errors. TODO: consider
# moving the full strings into a string table instead of building them out of English noun phrases.
def _a(noun: str) -> str:
    indefinite_article = "an" if noun[0] in "AEIOUaeiou" else "a"
    if noun.startswith("one-"):
        indefinite_article = "a"
    return f"{indefinite_article} {noun}"
