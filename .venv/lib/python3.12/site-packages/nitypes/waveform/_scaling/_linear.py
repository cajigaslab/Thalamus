from __future__ import annotations

from typing import TYPE_CHECKING, SupportsFloat

import numpy.typing as npt

from nitypes._arguments import arg_to_float
from nitypes.waveform._scaling._base import _ScalarType

if TYPE_CHECKING:
    # Import from the public package so the docs don't reference private submodules.
    from nitypes.waveform import ScaleMode
else:
    from nitypes.waveform._scaling._base import ScaleMode


class LinearScaleMode(ScaleMode):
    """A scale mode that scales data linearly."""

    __slots__ = ["_gain", "_offset", "__weakref__"]

    _gain: float
    _offset: float

    def __init__(self, gain: SupportsFloat, offset: SupportsFloat) -> None:
        """Initialize a new scale mode object that scales data linearly.

        Args:
            gain: The gain of the linear scale.
            offset: The offset of the linear scale.

        Returns:
            A scale mode that scales data linearly.
        """
        self._gain = arg_to_float("gain", gain)
        self._offset = arg_to_float("offset", offset)

    @property
    def gain(self) -> float:
        """The gain of the linear scale."""
        return self._gain

    @property
    def offset(self) -> float:
        """The offset of the linear scale."""
        return self._offset

    def _transform_data(self, data: npt.NDArray[_ScalarType]) -> npt.NDArray[_ScalarType]:
        # https://github.com/numpy/numpy/issues/28805 - TYP: mypy infers that adding/multiplying a
        # npt.NDArray[np.float32] with a float promotes dtype to Any or np.float64
        return data * self._gain + self._offset  # type: ignore[operator,no-any-return]

    def __eq__(self, value: object, /) -> bool:
        """Return self==value."""
        if not isinstance(value, self.__class__):
            return NotImplemented
        return self._gain == value._gain and self._offset == value._offset

    def __repr__(
        self,
    ) -> str:
        """Return repr(self)."""
        return f"{self.__class__.__module__}.{self.__class__.__name__}({self.gain}, {self.offset})"
