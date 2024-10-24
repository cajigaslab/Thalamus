'''
Reward delivery implementation
'''
import typing
import asyncio
import pathlib
import datetime
import platform
import functools
import subprocess
from .config import *

from .qt import *

Executable = typing.Callable[[], None]

class MeteredUpdater:
  def __init__(self, config: ObservableCollection, interval: datetime.timedelta, stop_when: typing.Callable[[], bool]):
    self.config = config
    self.interval = interval
    self.stop_when = stop_when
    self.task = asyncio.get_event_loop().create_task(self.__loop())
    self.updates = []

  async def __loop(self):
    while not self.stop_when():
      for k, v in self.updates:
        print(k)
        v()
      self.updates = []
      await asyncio.sleep(self.interval.total_seconds())

  def __setitem__(self, key, value):
    for i, kv in enumerate(self.updates):
      k, v = kv
      if k == key:
        del self.updates[i]
        break

    def update():
      self.config[key] = value
    self.updates.append((key, update))

def open_preferred_app(path: pathlib.Path):
  system = platform.system()
  if system == 'Windows':
    subprocess.check_call(('start', str(path)), shell=True)
  elif system == 'Darwin':
    subprocess.check_call(('open', str(path)), shell=True)
  else:
    subprocess.check_call(('xdg-open', str(path)), shell=True)

class IgnorableError(Exception):
  '''
  Exception that will not be forwarded to exception handler
  '''

class IterableQueue:
  def __init__(self):
    self.queue = asyncio.Queue()
    self.sentinel = object()

  def put(self, item):
    return self.queue.put(item)
  
  def close(self):
    return self.queue.put(self.sentinel)

  def join(self):
    return self.queue.join()

  def __aiter__(self):
    return self

  async def __anext__(self):
    try:
      item = await self.queue.get()
    except asyncio.CancelledError:
      raise StopAsyncIteration

    self.queue.task_done()
    if item is self.sentinel:
      raise StopAsyncIteration
    return item
  
  async def __aenter__(self):
    return self

  async def __aexit__(self, *exc):
    await self.close()
    await self.join()
    return False

class NodeSelector(QWidget):
  def __init__(self, node: ObservableDict, selector_key: str, multi_select: bool = True):
    super().__init__()
    print('create_run_widget')
    print(node)
    layout = QVBoxLayout()
    combo = QComboBox()
    add_button = QPushButton('Add')
    qlist = QListWidget()
    remove_button = QPushButton('Remove')

    layout.addWidget(combo)
    if multi_select:
      layout.addWidget(add_button)
      layout.addWidget(qlist, 1)
      layout.addWidget(remove_button)
    layout.addStretch()

    self.setLayout(layout)

    def on_name_change(target_node, action, key, value):
      if key != 'name':
        return
      
      for i in range(combo.count()):
        if combo.itemData() is target_node:
          current_text = combo.itemText(i)
          combo.setItemText(i, value)
          if multi_select:
            filtered = set(t.strip() for t in node[selector_key].split(','))
            filtered.remove(current_text)
            filtered.add(value)
            node[selector_key] = ','.join(sorted(filtered))
          else:
            if node[selector_key] == current_text:
              node[selector_key] = value
          break

      

    nodes = node.parent
    assert nodes is not None, "nodes list not found"
    combo.addItem('', None)
    for target_node in sorted(nodes, key=lambda n: n['name']):
      if target_node is node:
        continue
      name = target_node['name']
      combo.addItem(name, target_node)

      target_node.add_observer(lambda *args: on_name_change(target_node, *args), lambda: isdeleted(self))

    if not multi_select:
      def text_changed(text):
        print('text_changed', text)
        node[selector_key] = text
      combo.currentTextChanged.connect(text_changed)

    def on_add():
      print('on_add')
      print(node)
      current = [t.strip() for t in node[selector_key].split(',')]
      new_node = combo.currentData()
      if new_node is None:
        return
      new_name = new_node['name']
      if new_name in current:
        return
      current.append(new_name)
      current = sorted(c for c in current if c)
      node[selector_key] = ','.join(current)
    add_button.clicked.connect(on_add)

    def on_remove():
      current = [t.strip() for t in node[selector_key].split(',')]
      for item in qlist.selectedItems():
        name = item.text()
        try:
          current.remove(name)
        except ValueError:
          pass
      node[selector_key] = ','.join(c for c in current if c)
    remove_button.clicked.connect(on_remove)
    
    def on_change(action: ObservableCollection.Action, key: typing.Any, value: typing.Any):
      if selector_key != key:
        return

      if multi_select:
        qlist.clear()
        new_targets = sorted(t.strip() for t in node[key].split(','))
        print('new_targets', new_targets)
        qlist.addItems(new_targets)
      else:
        combo.setCurrentText(value)

    node.add_observer(on_change, functools.partial(isdeleted, self))
    on_change(ObservableCollection.Action.SET, selector_key, node[selector_key])
