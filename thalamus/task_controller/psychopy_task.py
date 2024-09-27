"""
Implementation of the simple task
"""
import typing
import logging
import datetime

from ..qt import *

from . import task_context
from .widgets import Form, ListAsTabsWidget
from .util import wait_for, wait_for_hold, TaskResult, TaskContextProtocol, CanvasPainterProtocol
from .. import task_controller_pb2
from ..config import *

LOGGER = logging.getLogger(__name__)

def create_widget(task_config: ObservableCollection) -> QWidget:
  """
  Creates a widget for configuring the simple task
  """
  result = QWidget()
  layout = QVBoxLayout()
  result.setLayout(layout)

  """
  Below: We're building a Form (widgets.py) object that will use task_config to initialize
  the parameters of this task. Values are taken from the provided "task_config" argument, and
  if the key (e.g. intertrial_timeout) is not found in the task_config, the parameters will
  default to the values provided below. The build function also wires up all the
  listeners to update the task_config when changes are made.
  """
  form = Form.build(task_config, ["Name:", "Min:", "Max:"],
    Form.Constant('width', 'width', 500, 'px'),
    Form.Constant('height', 'height', 500, 'px'),
    Form.Constant('center_x', 'center_x', 500, 'px'),
    Form.Constant('center_y', 'center_y', 500, 'px'),
    Form.Uniform('blink_timeout', 'blink_timeout', 2, 3, 's'),
    Form.Uniform('decision_timeout', 'decision_timeout', 2, 3, 's'),
    Form.Uniform('fix1_timeout', 'fix1_timeout', 2, 3, 's'),
    Form.Uniform('fix2_timeout', 'fix2_timeout', 2, 3, 's'),
    Form.Bool('is_height_locked', 'is_height_locked', False),
    Form.Color('Color', 'target_color', QColor(255, 255, 255))
  )
  layout.addWidget(form)

  return result


async def run(context: TaskContextProtocol) -> TaskResult: #pylint: disable=too-many-statements
  pass