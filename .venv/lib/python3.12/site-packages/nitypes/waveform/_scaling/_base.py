from __future__ import annotations

from abc import ABC, abstractmethod
from typing import TypeVar

import numpy as np
import numpy.typing as npt


_ScalarType = TypeVar("_ScalarType", bound=np.generic)


class ScaleMode(ABC):
    """An object that specifies how the waveform is scaled."""

    __slots__ = ()

    @abstractmethod
    def _transform_data(self, data: npt.NDArray[_ScalarType]) -> npt.NDArray[_ScalarType]:
        raise NotImplementedError
