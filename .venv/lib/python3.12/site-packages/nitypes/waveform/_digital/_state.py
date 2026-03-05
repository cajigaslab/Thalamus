from __future__ import annotations

from enum import IntEnum

from nitypes._exceptions import invalid_arg_value

_CHAR_TABLE = "01ZLHXTV"

_STATE_TEST_TABLE = [
    # 0  1  Z  L  H  X  T  V
    [1, 0, 0, 1, 0, 1, 0, 1],  # 0
    [0, 1, 0, 0, 1, 1, 0, 1],  # 1
    [0, 0, 1, 0, 0, 1, 1, 0],  # Z
    [1, 0, 0, 1, 0, 1, 0, 0],  # L
    [0, 1, 0, 0, 1, 1, 0, 0],  # H
    [1, 1, 1, 1, 1, 1, 1, 1],  # X
    [0, 0, 1, 0, 0, 1, 1, 0],  # T
    [1, 1, 0, 0, 0, 1, 0, 1],  # V
]


class DigitalState(IntEnum):
    """An IntEnum of the different digital states that a digital signal can represent.

    You can use :class:`DigitalState` in place of an :any:`int`:

    >>> DigitalState.FORCE_OFF
    <DigitalState.FORCE_OFF: 2>
    >>> DigitalState.FORCE_OFF == 2
    True

    Use :meth:`from_char` and :meth:`to_char` to convert between states and characters:

    >>> DigitalState.from_char("Z")
    <DigitalState.FORCE_OFF: 2>
    >>> DigitalState.to_char(2)
    'Z'

    Use :meth:`test` to compare actual vs. expected states, returning True on failure.

    >>> DigitalState.test(DigitalState.FORCE_DOWN, DigitalState.COMPARE_LOW)
    False
    >>> DigitalState.test(DigitalState.FORCE_UP, DigitalState.COMPARE_LOW)
    True
    """

    _value_: int

    FORCE_DOWN = 0
    """Force logic low (``0``). Drive to the low voltage level (VIL)."""

    FORCE_UP = 1
    """Force logic high (``1``). Drive to the high voltage level (VIH)."""

    FORCE_OFF = 2
    """Force logic high impedance (``Z``). Turn the driver off."""

    COMPARE_LOW = 3
    """Compare logic low (edge) (``L``). Compare for a voltage level lower than the low voltage
    threshold (VOL)."""

    COMPARE_HIGH = 4
    """Compare logic high (edge) (``H``). Compare for a voltage level higher than the high voltage
    threshold (VOH)."""

    COMPARE_UNKNOWN = 5
    """Compare logic unknown (``X``). Don't compare."""

    COMPARE_OFF = 6
    """Compare logic high impedance (edge) (``T``). Compare for a voltage level between the low
    voltage threshold (VOL) and the high voltage threshold (VOH)."""

    COMPARE_VALID = 7
    """Compare logic valid level (edge) (``V``). Compare for a voltage level either lower than the
    low voltage threshold (VOL) or higher than the high voltage threshold (VOH)."""

    @property
    def char(self) -> str:
        """The character representing the digital state."""
        return _CHAR_TABLE[self]

    @classmethod
    def from_char(cls, char: str) -> DigitalState:
        """Look up the digital state for the corresponding character."""
        try:
            return DigitalState(_CHAR_TABLE.index(char))
        except ValueError:
            raise KeyError(char)

    @classmethod
    def to_char(cls, state: DigitalState, errors: str = "strict") -> str:
        """Get a character representing the digital state.

        Args:
            state: The digital state.
            errors: Specifies how to handle errors.

                * "strict": raise ``KeyError``
                * "replace": return "?"

        Returns:
            A character representing the digital state.
        """
        if errors not in ("strict", "replace"):
            raise invalid_arg_value("errors argument", "supported value", errors)
        try:
            return DigitalState(state).char
        except ValueError:
            if errors == "strict":
                raise KeyError(state)
            elif errors == "replace":
                return "?"
            raise

    @staticmethod
    def test(state1: DigitalState, state2: DigitalState) -> bool:
        """Test two digital states and return True if the test failed."""
        return not _STATE_TEST_TABLE[DigitalState(state1)][DigitalState(state2)]
