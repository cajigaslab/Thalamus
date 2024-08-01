"""
Implementation of the null task.  Does nothing, mostly used for testing
"""

import datetime
from . import task_context
from . import util
from ..config import *

from .widgets import Form
from .util import do_stimulation
from .. import task_controller_pb2
from ..qt import *

def create_widget(task_config: ObservableCollection) -> QWidget:
  """
  Returns a QWidget that will be used to edit the task configuration
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
  form = Form.build(task_config, ["Name:", "Value:"],
    #Stimulation parameters
    Form.Constant('Stim Start', 'stim_start', 1, 's'),
    Form.Constant('Intan Config', 'intan_cfg', 2),
    Form.Constant('Pulse Count', 'pulse_count', 1),
    Form.Constant('Pulse Frequency', 'pulse_frequency', 1, 'hz'),
    Form.Constant('Pulse Width', 'pulse_width', 0, 'ms'),
  )
  layout.addWidget(form)

  return result

SHOULD_FAIL = False

async def stamp_msg(context, msg):
  msg.header.stamp = context.ros_manager.node.node.get_clock().now().to_msg()
  #context.pulse_digital_channel()
  return msg

async def run(context: util.TaskContextProtocol) -> util.TaskResult:
  """
  Implementation of the task
  """

  #Read stimulation parameters
  stim_start = datetime.timedelta(seconds=context.task_config['stim_start'])
  intan_cfg = context.task_config['intan_cfg']
  pulse_count = int(context.task_config['pulse_count'])
  pulse_frequency = context.task_config['pulse_frequency']
  pulse_width = datetime.timedelta(milliseconds=context.task_config['pulse_width'])
  pulse_period = datetime.timedelta(seconds=1/pulse_frequency) #interpulse period

  await context.servicer.publish_state(task_controller_pb2.BehavState(state='start_on'))

  await do_stimulation(context, stim_start, intan_cfg, pulse_width, pulse_count, pulse_period)

  await context.servicer.publish_state(task_controller_pb2.BehavState(state='success'))

  return task_context.TaskResult(True)
