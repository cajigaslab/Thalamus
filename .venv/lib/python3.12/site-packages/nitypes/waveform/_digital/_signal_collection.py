from __future__ import annotations

from collections.abc import Sequence
from typing import TYPE_CHECKING, Generic, overload

from nitypes._exceptions import invalid_arg_type
from nitypes.waveform.typing import TDigitalState

if TYPE_CHECKING:
    # Import from the public package so the docs don't reference private submodules.
    from nitypes.waveform import DigitalWaveform, DigitalWaveformSignal
else:
    # DigitalWaveform is a circular import.
    from nitypes.waveform._digital._signal import DigitalWaveformSignal


class DigitalWaveformSignalCollection(
    Generic[TDigitalState], Sequence[DigitalWaveformSignal[TDigitalState]]
):
    """A collection of digital waveform signals.

    To construct this object, use the :any:`DigitalWaveform.signals` property.
    """

    __slots__ = ["_owner", "_signals", "__weakref__"]

    _owner: DigitalWaveform[TDigitalState]
    _signals: list[DigitalWaveformSignal[TDigitalState] | None]

    def __init__(self, owner: DigitalWaveform[TDigitalState]) -> None:
        """Initialize a new DigitalWaveformSignalCollection."""
        self._owner = owner
        self._signals = [None] * owner.signal_count

    def __len__(self) -> int:
        """Return len(self)."""
        return len(self._signals)

    @overload
    def __getitem__(  # noqa: D105 - missing docstring in magic method
        self, index: int | str
    ) -> DigitalWaveformSignal[TDigitalState]: ...
    @overload
    def __getitem__(  # noqa: D105 - missing docstring in magic method
        self, index: slice
    ) -> Sequence[DigitalWaveformSignal[TDigitalState]]: ...

    def __getitem__(
        self, index: int | str | slice
    ) -> DigitalWaveformSignal[TDigitalState] | Sequence[DigitalWaveformSignal[TDigitalState]]:
        """Get self[index]."""
        if isinstance(index, int):  # index is the signal index
            if index < 0:
                index += len(self._signals)
            value = self._signals[index]
            if value is None:
                column_index = self._owner._reverse_index(index)
                value = self._signals[index] = DigitalWaveformSignal(
                    self._owner, index, column_index
                )
            return value
        elif isinstance(index, str):  # index is the line name
            line_names = self._owner._get_line_names()
            try:
                column_index = line_names.index(index)
            except ValueError:
                raise IndexError(index)
            signal_index = self._owner._reverse_index(column_index)
            return self[signal_index]
        elif isinstance(index, slice):  # index is a slice of signal indices
            return [self[i] for i in range(*index.indices(len(self)))]
        else:
            raise invalid_arg_type("index", "int or str", index)
