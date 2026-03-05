"""Vector data type for NI Python APIs.

Vector Data Type
=================
:class:`Vector`: A vector data object represents an array of scalar values with units information.
Valid types for the scalar value are :any:`bool`, :any:`int`, :any:`float`, and :any:`str`.
"""

from __future__ import annotations

from collections.abc import Iterable, MutableSequence
from typing import TYPE_CHECKING, overload, Any, Union

from typing_extensions import TypeVar, final, override

from nitypes._exceptions import invalid_arg_type
from nitypes.waveform._extended_properties import UNIT_DESCRIPTION

if TYPE_CHECKING:
    # Import from the public package so the docs don't reference private submodules.
    from nitypes.waveform import ExtendedPropertyDictionary
else:
    from nitypes.waveform._extended_properties import ExtendedPropertyDictionary

TScalar = TypeVar("TScalar", bound=Union[bool, int, float, str])


@final
class Vector(MutableSequence[TScalar]):
    """A sequence of scalar values with units information.

    Constructing
    ^^^^^^^^^^^^

    To construct a vector data object, use the :class:`Vector` class:

    >>> Vector([False, True])
    nitypes.vector.Vector(values=[False, True], units='')
    >>> Vector([0, 1, 2])
    nitypes.vector.Vector(values=[0, 1, 2], units='')
    >>> Vector([5.0, 6.0], 'volts')
    nitypes.vector.Vector(values=[5.0, 6.0], units='volts')
    >>> Vector(["one", "two"], "volts")
    nitypes.vector.Vector(values=['one', 'two'], units='volts')
    """

    __slots__ = [
        "_values",
        "_value_type",
        "_extended_properties",
    ]

    _values: list[TScalar]
    _value_type: type[TScalar]
    _extended_properties: ExtendedPropertyDictionary

    def __init__(
        self,
        values: Iterable[TScalar],
        units: str = "",
        *,
        value_type: type[TScalar] | None = None,
    ) -> None:
        """Initialize a new vector.

        Args:
            values: The scalar values to store in this object.
            units: The units string associated with this data.
            value_type: The type of values that will be added to this Vector.
                This parameter should only be used when creating a Vector with
                an empty Iterable.

        Returns:
            A vector data object.
        """
        if not values:
            if not value_type:
                raise TypeError("You must specify values as non-empty or specify value_type.")
            self._value_type = value_type
        else:
            # Validate the values input
            for index, value in enumerate(values):
                # Only set _value_type once.
                if not index:
                    self._value_type = type(value)

                if not isinstance(value, (bool, int, float, str)):
                    raise invalid_arg_type("vector input data", "bool, int, float, or str", values)

                if not isinstance(value, self._value_type):
                    raise TypeError("All values in the values input must be of the same type.")

        if not isinstance(units, str):
            raise invalid_arg_type("units", "str", units)

        self._values = list(values)
        self._extended_properties = ExtendedPropertyDictionary()
        self._extended_properties[UNIT_DESCRIPTION] = units

    @property
    def units(self) -> str:
        """The unit of measurement, such as volts, of the vector."""
        value = self._extended_properties.get(UNIT_DESCRIPTION, "")
        assert isinstance(value, str)
        return value

    @units.setter
    def units(self, value: str) -> None:
        if not isinstance(value, str):
            raise invalid_arg_type("units", "str", value)
        self._extended_properties[UNIT_DESCRIPTION] = value

    @property
    def extended_properties(self) -> ExtendedPropertyDictionary:
        """The extended properties for the vector.

        .. note::
            Data stored in the extended properties dictionary may not be encrypted when you send it
            over the network or write it to a TDMS file.
        """
        return self._extended_properties

    @overload
    def __getitem__(  # noqa: D105 - missing docstring in magic method
        self, index: int
    ) -> TScalar: ...

    @overload
    def __getitem__(  # noqa: D105 - missing docstring in magic method
        self, index: slice
    ) -> MutableSequence[TScalar]: ...

    @override
    def __getitem__(self, index: int | slice) -> TScalar | MutableSequence[TScalar]:
        """Return the TimeDelta at the specified location."""
        return self._values[index]

    @overload
    def __setitem__(  # noqa: D105 - missing docstring in magic method
        self, index: int, value: TScalar
    ) -> None: ...

    @overload
    def __setitem__(  # noqa: D105 - missing docstring in magic method
        self, index: slice, value: Iterable[TScalar]
    ) -> None: ...

    @override
    def __setitem__(self, index: int | slice, value: TScalar | Iterable[TScalar]) -> None:
        """Set value(s) at the specified location."""
        if isinstance(index, int):
            if isinstance(value, Iterable) and not isinstance(value, str):
                raise TypeError("You cannot assign an Iterable to a vector index.")
            elif not isinstance(value, self._value_type):
                raise self._create_value_mismatch_exception(value)

            self._values[index] = value
        else:  # slice
            if not isinstance(value, Iterable):
                raise TypeError("You must assign an Iterable to a Vector slice.")
            elif isinstance(value, str):  # Narrow the type to exclude string.
                raise TypeError("You cannot assign a string to Vector slice.")
            else:
                # Assigning an empty Iterable to a slice is valid, so we don't check for empty.
                # If an empty Iterable is assigned to a slice, that slice is deleted.
                for subval in value:
                    if not isinstance(subval, self._value_type):
                        raise self._create_value_mismatch_exception(subval)

            self._values[index] = value

    def __delitem__(self, index: int | slice) -> None:
        """Delete item(s) from the specified location."""
        del self._values[index]

    def __len__(self) -> int:
        """Return the length of the Vector."""
        return len(self._values)

    def insert(self, index: int, value: TScalar) -> None:
        """Insert a value at the specified location."""
        if not isinstance(value, self._value_type):
            raise self._create_value_mismatch_exception(value)
        self._values.insert(index, value)

    def __eq__(self, value: object, /) -> bool:
        """Return self==value."""
        if not isinstance(value, self.__class__):
            return NotImplemented
        return self._values == value._values and self.units == value.units

    def __reduce__(self) -> tuple[Any, ...]:
        """Return object state for pickling."""
        return (self.__class__, (self._values, self.units))

    def __repr__(self) -> str:
        """Return repr(self)."""
        args = [f"values={self._values!r}", f"units={self.units!r}"]
        return f"{self.__class__.__module__}.{self.__class__.__name__}({', '.join(args)})"

    def __str__(self) -> str:
        """Return str(self)."""
        if self.units:
            values_with_units = [f"{value} {self.units}" for value in self._values]
            comma_delimited_str = ", ".join(values_with_units)
        else:
            values = [f"{value}" for value in self._values]
            comma_delimited_str = ", ".join(values)

        return f"[{comma_delimited_str}]"

    def _create_value_mismatch_exception(self, value: object) -> TypeError:
        return TypeError(
            f"Input type does not match existing type. Input Type: {type(value)} "
            f"Existing Type: {self._value_type}"
        )
