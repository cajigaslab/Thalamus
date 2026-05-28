from __future__ import annotations

from typing import TYPE_CHECKING, Any, Generic, SupportsIndex

import numpy as np
import numpy.typing as npt

from nitypes._arguments import arg_to_uint
from nitypes.waveform.typing import TDigitalState

if TYPE_CHECKING:
    # Import from the public package so the docs don't reference private submodules.
    from nitypes.waveform import DigitalWaveform
else:
    # DigitalWaveform is a circular import.
    pass


class DigitalWaveformSignal(Generic[TDigitalState]):
    """A signal of a digital waveform.

    To construct this object, use the :any:`DigitalWaveform.signals` property and index the returned
    collection, e.g. ``waveform.signals[0]`` or ``waveform.signals["Dev1/port0/line0"]``.
    """

    __slots__ = ["_owner", "_signal_index", "_column_index", "__weakref__"]

    _owner: DigitalWaveform[TDigitalState]
    _column_index: int
    _signal_index: int

    def __init__(
        self,
        owner: DigitalWaveform[TDigitalState],
        signal_index: SupportsIndex,
        column_index: SupportsIndex | None = None,
    ) -> None:
        """Initialize a new digital waveform signal."""
        if column_index is None:
            # when unpickling an old version, column_index may not be provided
            column_index = signal_index

        self._owner = owner
        self._signal_index = arg_to_uint("signal index", signal_index)
        self._column_index = arg_to_uint("column index", column_index)

    @property
    def owner(self) -> DigitalWaveform[TDigitalState]:
        """The waveform that owns this signal."""
        return self._owner

    @property
    def signal_index(self) -> int:
        """The signal's position in the DigitalWaveform.signals collection (0-based)."""
        return self._signal_index

    @property
    def column_index(self) -> int:
        """The signal's position in the DigitalWaveform.data array's second dimension (0-based).

        This index is used to access the signal's data within the waveform's data array:
        `waveform.data[:, column_index]`.

        Note: The column_index is reversed compared to the signal_index. column_index 0 (the
        leftmost column) corresponds to the highest signal_index and highest line number. The
        highest column_index (the rightmost column) corresponds to signal_index 0 and line 0. This
        matches industry conventions where line 0 is the LSB and appears as the rightmost bit.
        """
        return self._column_index

    @property
    def data(self) -> npt.NDArray[TDigitalState]:
        """The signal data, indexed by sample."""
        return self._owner.data[:, self._column_index]

    @property
    def name(self) -> str:
        """The signal name."""
        return self._owner._get_line_name(self._column_index)

    @name.setter
    def name(self, value: str) -> None:
        self._owner._set_line_name(self._column_index, value)

    def __eq__(self, value: object, /) -> bool:
        """Return self==value."""
        if not isinstance(value, self.__class__):
            return NotImplemented
        # Do not compare the index or name.
        return np.array_equal(self.data, value.data)

    def __reduce__(self) -> tuple[Any, ...]:
        """Return object state for pickling."""
        ctor_args = (self._owner, self._signal_index, self._column_index)
        return (self.__class__, ctor_args)

    def __repr__(self) -> str:
        """Return repr(self)."""
        # This is not the same as the constructor arguments.
        args = []
        if self.name:
            args.append(f"name={self.name!r}")
        if self._owner._sample_count > 0:
            args.append(f"data={self.data!r}")
        return f"{self.__class__.__module__}.{self.__class__.__name__}({', '.join(args)})"
