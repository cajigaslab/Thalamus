from .qt import *
from .observable_item_models import TreeObservableCollectionModel
from .config import ObservableDict

import os
import oslex
import shutil
import typing
import asyncio
import logging
import platform
import subprocess
from .task_controller.util import create_task_with_exc_handling

LOGGER = logging.getLogger(__name__)

class OrchestrationDialog(QWidget):
  def __init__(self, config: ObservableDict):
    super().__init__()
    self.config = config
    if 'Processes' not in config:
      config['Processes'] = []
    if 'Remote Executor' not in config:
      config['Remote Executor'] = False
    self.processes = config.get('Processes', None)

    self.tree = QTableView()
    self.tree.verticalHeader()

    def on_add():
      if self.processes is None:
        return
      self.processes.append({
        'Command': '',
        'Critial (Crash on Failure)': False
      })
    self.add_button = QPushButton('Add')
    self.add_button.clicked.connect(on_add)

    def on_remove():
      if self.processes is None:
        return
      rows = sorted(set(i.row() for i in self.tree.selectedIndexes()), reverse=True)
      for row in rows:
        del self.waves[row]
    self.remove_button = QPushButton('Remove')
    self.remove_button.clicked.connect(on_remove)

    self.remote_executor_checkbox = QCheckBox('Remote Executor')
    self.remote_executor_checkbox.toggled.connect(lambda v: config.update({'Remote Executor': v}))

    layout = QGridLayout()
    layout.addWidget(self.remote_executor_checkbox, 0, 0, 1, 2)
    layout.addWidget(QLabel('These programs will be spawned when you restart'), 1, 0, 1, 2)
    layout.addWidget(self.tree, 2, 0, 1, 2)
    layout.addWidget(self.add_button, 3, 0, 1, 1)
    layout.addWidget(self.remove_button, 3, 1, 1, 1)

    self.setLayout(layout)

    config.add_observer(self.on_change, lambda: isdeleted(self), True)

  def on_change(self, action, key, value):
    if key == 'Processes':
      model = TreeObservableCollectionModel(value, key_column="#", columns=['Command', 'Critial (Crash on Failure)'], show_extra_values=False, is_editable=lambda c, k: True)
      self.tree.setModel(model)
    elif key == 'Remote Executor':
      if self.remote_executor_checkbox.isChecked() != value:
        self.remote_executor_checkbox.setChecked(value)


class Orchestrator:
  def __init__(self):
    self.tasks: typing.List[asyncio.Task] = []
    self.procs: typing.List[asyncio.subprocess.Process] = []
    self.running = False

  async def start(self, processes):
    self.running = True
    for process in processes:
      required = process['Critial (Crash on Failure)']
      command = process['Command']
      tokens = oslex.split(command)
      if not tokens:
        continue
      tokens[0] = shutil.which(tokens[0])
      LOGGER.info('starting ' + str(tokens))

      env = dict(os.environ)
      env['PYTHONUNBUFFERED'] = '1'

      proc = await asyncio.create_subprocess_exec(*tokens, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT, env=env)
      LOGGER.info('started ' + command)
      self.procs.append(proc)
      self.tasks.append(create_task_with_exc_handling(self.__proc_manager(proc, command, required)))

  async def stop(self):
    self.running = False
    if not self.procs:
      return

    LOGGER.info('Killing procs')
    for proc in self.procs:
      if proc.returncode is None:
        proc.kill()

    LOGGER.info('Awaiting tasks')
    await asyncio.wait(self.tasks)
    LOGGER.info('ochestration terminated')


  async def __proc_manager(self, proc: asyncio.subprocess.Process, command: str, required: bool):
    assert proc.stdout is not None
    while True:
      data = await proc.stdout.readline()
      if not data:
        print('cleanup')
        LOGGER.info('Process stdout reached EOF: %s', command)
        await proc.wait()
        LOGGER.info('Process ended with code=%d: %s', proc.returncode, command)
        if self.running and required:
          raise RuntimeError(f'Critial process ended: {command}')
        return
      text = data.decode().rstrip()
      LOGGER.info(text)

