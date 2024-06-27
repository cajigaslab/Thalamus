"""
Implementation of the null task.  Does nothing, mostly used for testing
"""
import datetime

import PyQt5.QtWidgets

from . import task_context
from ..config import *
from . import util

from .widgets import Form

def create_widget(task_config: ObservableCollection) -> PyQt5.QtWidgets.QWidget:
  """
  Returns a QWidget that will be used to edit the task configuration
  """
  form = Form.build(task_config, ["Name:", "Min:", "Max:"],
    Form.Uniform('Duration', 'duration', 5, 8, 's'),
  )
  return form

async def run(context: util.TaskContextProtocol) -> util.TaskResult:
  """
  Implementation of the task
  """
  await context.sleep(datetime.timedelta(seconds=10))
  return util.TaskResult(True)
