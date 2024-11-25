"""
Defines the controller UI
"""

import typing
import asyncio
import functools

from ..qt import *

import numpy

from .task_context import TaskContext, TaskDescription
from .util import remove_by_is
from ..config import *

from .tasks import DESCRIPTIONS as TASK_DESCRIPTIONS

from .operator_view import Window as OperatorWindow
from .window import Window as SubjectWindow
from .reward_schedule import RewardSchedule
from .util import create_task_with_exc_handling, isdeleted
from ..qt import *
from .touch_dialog import TouchDialog

from ..orchestration import OrchestrationDialog

class TaskWidget(QWidget):
  """
  Implements the UI for editing a task
  """
  def __init__(self, config: ObservableCollection, queue: ObservableCollection,
               on_task_name_changed: typing.Callable[[ObservableCollection], None],
               task_descriptions: typing.List[TaskDescription]) -> None:
    super().__init__(None)
    self.config = config
    self.on_task_name_changed = on_task_name_changed
    self.task_descriptions = sorted(task_descriptions, key=lambda t: t.display_name)
    self.current_widget: typing.Optional[QWidget] = None

    self.combo_box = QComboBox()
    self.combo_box.setObjectName('task_type')
    for task in self.task_descriptions:
      self.combo_box.addItem(task.display_name, task)

    for i, desc in enumerate(self.task_descriptions):
      if desc.code == self.config['task_type']:
        self.combo_box.setCurrentIndex(i)
        break

    self.combo_box.currentIndexChanged.connect(self.on_task_type_selected)

    spinner = QSpinBox()
    spinner.setObjectName('goal')
    spinner.setValue(self.config['goal'])
    spinner.valueChanged.connect(self.on_goal_changed)

    layout = QHBoxLayout()
    layout.addWidget(QLabel("Task Name:"), 0)
    self.name_edit = QLineEdit(self.config['name'])
    self.name_edit.setObjectName('name')
    self.name_edit.textChanged.connect(self.on_name_changed)
    layout.addWidget(self.name_edit, 0)
    layout.addWidget(QLabel("Task Type:"), 0)
    layout.addWidget(self.combo_box, 0)
    layout.addWidget(QLabel("Goal:"), 0)
    layout.addWidget(spinner, 0)
    enqueue_button = QPushButton("Enqueue Task")
    enqueue_button.setObjectName('enqueue')
    enqueue_button.clicked.connect(lambda: self.on_enqueue(queue))
    layout.addWidget(enqueue_button)
    copy_task_button = QPushButton("Copy Task")
    copy_task_button.setObjectName('copy')
    copy_task_button.clicked.connect(self.on_copy_task)
    layout.addWidget(copy_task_button)
    layout.addStretch(1)

    self.top_layout = QVBoxLayout()
    self.top_layout.addLayout(layout, 0)

    self.setLayout(self.top_layout)

    self.config.add_observer(functools.partial(self.on_task_changed, spinner),
                             functools.partial(isdeleted, self))

    self.set_task_type(config['task_type'])

  def on_copy_task(self) -> None:
    """
    Copies this task and adds it to the parent task cluster
    """
    if self.config.parent:
      self.config.parent.append(self.config.copy())

  def on_enqueue(self, queue: ObservableCollection) -> None:
    """
    Copies this task and adds it to the queue
    """
    task = self.config.copy()
    assert self.config.parent is not None, 'self.config.parent is None'
    assert self.config.parent.parent is not None, 'self.config.parent.parent is None'
    task['task_cluster_name'] = self.config.parent.parent['name']
    queue.append(task)

  def on_name_changed(self) -> None:
    """
    Updates the task name in response to UI events
    """
    new_name = self.name_edit.text()
    self.config['name'] = new_name

  def set_task_type(self, task_type: str) -> None:
    """
    Updates the widget specific UI as the task_type changes
    """
    if self.current_widget is not None:
      self.top_layout.removeWidget(self.current_widget)
      self.current_widget.setParent(None) # type: ignore
      self.current_widget.deleteLater()

    task_description = None
    index, desc = [(i, desc) for i, desc in enumerate(self.task_descriptions) if desc.code == task_type][0]
    task_description = desc
    self.combo_box.setCurrentIndex(index)

    self.current_widget = QScrollArea()
    self.current_widget.setWidgetResizable(True)
    task_widget = task_description.create_widget(self.config)
    task_widget.setObjectName('task_widget')
    self.current_widget.setWidget(task_widget)

    self.top_layout.addWidget(self.current_widget, 1)


  def on_task_changed(self, spinner: QSpinBox,
                      action: ObservableCollection.Action,
                      key: typing.Any, value: typing.Any) -> None:
    """
    Updates the UI as the config changes
    """
    if action == ObservableCollection.Action.SET:
      if key == 'task_type':
        self.set_task_type(value)
      elif key == 'goal':
        spinner.setValue(value)
        self.on_task_name_changed(self.config)
      elif key == 'name':
        self.name_edit.setText(value)
        self.on_task_name_changed(self.config)

  def on_task_type_selected(self, index: int) -> None:
    """
    Updates the config's task_type in response to UI events
    """
    if self.config['task_type'] != self.task_descriptions[index].code:
      self.config['task_type'] = self.task_descriptions[index].code

  def on_goal_changed(self, value: int) -> None:
    """
    Updates the goal in response to UI events
    """
    self.config['goal'] = value

class TaskClusterWidget(QWidget):
  """
  Implements the UI for editing a task cluster
  """
  def __init__(self, config: ObservableCollection, queue: ObservableCollection,
               name_callback: typing.Callable[[ObservableCollection], None]) -> None:
    super().__init__(None)
    self.config = config
    self.config.add_observer(self.on_task_cluster_changed, functools.partial(isdeleted, self))
    self.config['tasks'].add_observer(self.on_tasks_changed, functools.partial(isdeleted, self))
    self.queue = queue
    self.name_callback = name_callback
    self.spinner = QSpinBox()
    self.spinner.setObjectName('weight')
    self.spinner.setValue(int(self.config['weight']))
    self.spinner.valueChanged.connect(self.on_weight_changed)

    layout = QHBoxLayout()
    layout.addWidget(QLabel("Name:"), 0)
    self.name_edit = QLineEdit(self.config['name'])
    self.name_edit.setObjectName('cluster_name')
    self.name_edit.textChanged.connect(self.on_name_changed)
    layout.addWidget(self.name_edit, 0)
    layout.addWidget(QLabel("Weight:"), 0)
    layout.addWidget(self.spinner, 0)
    create_task_button = QPushButton("Add Task")
    create_task_button.setObjectName('add_task')
    create_task_button.clicked.connect(self.on_create_task)
    layout.addWidget(create_task_button)
    copy_task_cluster_button = QPushButton("Copy Task Cluster")
    copy_task_cluster_button.setObjectName('copy_task_cluster')
    copy_task_cluster_button.clicked.connect(self.on_copy_task_cluster)
    layout.addWidget(copy_task_cluster_button)
    enqueue_button = QPushButton("Enqueue Task Cluster")
    enqueue_button.setObjectName('enqueue_task_cluster')
    enqueue_button.clicked.connect(self.on_enqueue)
    layout.addWidget(enqueue_button)
    layout.addStretch(1)

    self.task_tabs = QTabWidget()
    self.task_tabs.setObjectName('task_tabs')
    self.task_tabs.setTabsClosable(True)
    self.task_tabs.setMovable(True)
    self.task_tabs.tabCloseRequested.connect(self.on_delete_task)

    top_layout = QVBoxLayout(self)
    top_layout.addLayout(layout, 0)
    top_layout.addWidget(self.task_tabs, 1)
    self.setLayout(top_layout)

    for i, task in enumerate(self.config['tasks']):
      self.on_tasks_changed(ObservableCollection.Action.SET, i, task)

  def on_copy_task_cluster(self) -> None:
    """
    Copies this task cluster and adds it to the config
    """
    if self.config.parent:
      self.config.parent.append(self.config.copy())

  def on_delete_task(self, index: int) -> None:
    """
    Deletes a task fro mthis task cluster
    """
    config = self.task_tabs.widget(index).config
    #mypy doesn't appears to interpret the union below as a typing.Union[StandardButtons, StandardButton]
    confirm = QMessageBox.question(self, "Delete Task", "Delete Task " + config['name'] + "?",
                                   QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
    if confirm == QMessageBox.StandardButton.No:
      return
    remove_by_is(config.parent, config)

  def on_enqueue(self) -> None:
    """
    Copies this task config and appends it to the queue
    """
    self.queue.append(self.config.copy())

  def on_name_changed(self) -> None:
    """
    Updates the name in response to the UI
    """
    new_name = self.name_edit.text()
    self.config['name'] = new_name

  def on_create_task(self) -> None:
    """
    Creates a new task when the user clicks the Add Task button
    """
    self.config['tasks'].append({
      'task_type': 'simple',
      'type': 'task',
      'goal': 0,
      'name': 'Untitled'
    })

  def on_weight_changed(self, value: int) -> None:
    """
    Updates the weight in response to the UI
    """
    self.config['weight'] = value

  def on_task_cluster_changed(self, action: ObservableCollection.Action,
                              key: typing.Any, value: typing.Any) -> None:
    """
    Updates the task cluster UI in response to config changes
    """
    if action == ObservableCollection.Action.SET:
      if key == 'weight':
        self.spinner.setValue(value)
        self.name_callback(self.config)
      elif key == 'name':
        self.name_edit.setText(value)
        self.name_callback(self.config)

  def on_tasks_changed(self, action: ObservableCollection.Action, _: typing.Any, value: typing.Any) -> None:
    """
    Adds or removes task tabs in response to changes in the config
    """
    if action == ObservableCollection.Action.SET:
      widget = TaskWidget(value, self.queue, self.on_task_name_changed, TASK_DESCRIPTIONS)
      self.task_tabs.addTab(widget, f"{value['name']}: {value['goal']}")
    elif action == ObservableCollection.Action.DELETE:
      for i in range(self.task_tabs.count()):
        if self.task_tabs.widget(i).config is value:
          self.task_tabs.removeTab(i)
          break

  def on_task_name_changed(self, config: ObservableCollection) -> None:
    """
    Update the task's tab text when it's name or weight changes
    """
    for i in range(self.task_tabs.count()):
      if self.task_tabs.widget(i).config is config:
        self.task_tabs.setTabText(i, f"{config['name']}: {config['goal']}")
        break

#Suppress the following pylint error.  The following is how QT expects us to extend QTreeWidgetItem
class QueueTreeWidgetItem(QTreeWidgetItem): # pylint: disable=too-few-public-methods
  """
  A QTreeWidgetItem with a config object representing a task or task_cluster
  """
  def __init__(self, config: ObservableCollection, *args: typing.Any) -> None:
    super().__init__(*args)
    self.config = config

def add_action(menu: QMenu, text: str, callback: typing.Callable[[], None]) -> None:
  """
  Add an action to a menu
  """
  action = QAction(text, menu)
  action.triggered.connect(callback)
  menu.addAction(action)

class ConfigData(typing.NamedTuple):
  '''
  Fields related to the current config
  '''
  user_config: typing.Dict[typing.Any, typing.Any]
  file_name: str

class ControlWindow(QMainWindow):
  """
  Implements the controller UI that manages the config and TaskContext
  """
  def __init__(self, subject_window: typing.Optional[SubjectWindow], task_context: TaskContext,
               config_data: ConfigData,
               done_future: asyncio.Future) -> None:
    super().__init__()
    self.task_context = task_context
    self.config_data = config_data
    self.done_future = done_future
    self.valid_queue_items: typing.List[QTreeWidgetItem] = []
    self.operator_window: typing.Optional[OperatorWindow] = None

    file_menu = self.menuBar().addMenu("&File")
    add_action(file_menu, 'Start/Stop', self.on_start_stop)
    add_action(file_menu, 'Cancel Current Task', self.task_context.cancel)
    add_action(file_menu, 'Create Task Cluster', self.on_create_task_cluster)
    add_action(file_menu, 'Save Config', self.on_save_config)
    add_action(file_menu, 'Save Config As', self.on_save_as_config)
    add_action(file_menu, 'Load Config', self.on_load_config)
    add_action(file_menu, 'Load Reward Schedule', self.on_load_reward_schedule)
    add_action(file_menu, 'Reset Trial History', self.on_reset_trial_history)

    view_menu = self.menuBar().addMenu("&View")
    add_action(view_menu, 'Operator View', lambda: self.on_operator_view(subject_window))

    settings_menu = self.menuBar().addMenu("&Settings")
    add_action(settings_menu, 'Touch Screen', lambda: self.on_touch_screen())
    add_action(settings_menu, 'Orchestration', lambda: self.on_orchestration())

    self.task_cluster_tabs = QTabWidget()
    self.setObjectName('task_cluster_tabs')
    self.task_cluster_tabs.setTabsClosable(True)
    self.task_cluster_tabs.setMovable(True)
    self.task_cluster_tabs.tabCloseRequested.connect(self.on_delete_task_cluster)

    queue_layout = QVBoxLayout()
    self.queue_tree = QTreeWidget()
    self.queue_tree.setObjectName('queue_tree')
    self.queue_tree.setHeaderLabel("Queue")
    self.queue_tree.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection)
    self.queue_tree.doubleClicked.connect(self.on_queue_item_selected)
    queue_layout.addWidget(self.queue_tree)
    button = QPushButton("Delete Selected")
    button.setObjectName('delete_selected')
    button.clicked.connect(self.on_delete_from_queue)
    queue_layout.addWidget(button)

    queue_widget = QWidget()
    queue_widget.setLayout(queue_layout)

    self.setCentralWidget(self.task_cluster_tabs)

    dock = QDockWidget('Queue', self)
    dock.setWidget(queue_widget)
    self.addDockWidget(Qt.DockWidgetArea.RightDockWidgetArea, dock)

    plot = RewardSchedule(self.task_context.config['reward_schedule'])
    dock = QDockWidget('Reward Schedule', self)
    dock.setWidget(plot)
    self.addDockWidget(Qt.DockWidgetArea.BottomDockWidgetArea, dock)

    self.init()
    self.task_context.config['queue'].add_observer(self.on_queue_changed)

    self.setWindowTitle(f'Task Controller: {self.config_data.file_name}')

    self.__prepare_status()

  def __prepare_status(self) -> None:
    if 'status' not in self.task_context.config:
      self.task_context.config['status'] = ''
    status_widget = QLabel()
    self.statusBar().addWidget(status_widget)
    self.task_context.config.add_observer(functools.partial(self.__on_status_change, status_widget))

  def __on_status_change(self, widget: QLabel,
                         _: ObservableCollection.Action, key: typing.Any, value: typing.Any) -> None:
    if key == 'status':
      widget.setText(value)

  def on_queue_item_selected(self, index: QModelIndex) -> None:
    '''
    Display window to view an edit a task or cluster in the queue.
    '''
    item = typing.cast(QueueTreeWidgetItem, self.queue_tree.itemFromIndex(index))
    window = QMainWindow(self)
    widget: QWidget
    if item.config['type'] == 'task':
      window.setWindowTitle('Queue Task: ' + item.config['name'])
      widget = TaskWidget(item.config, self.task_context.config['queue'], lambda a: None, TASK_DESCRIPTIONS)
    else:
      window.setWindowTitle('Queue Task Cluster: ' + item.config['name'])
      widget = TaskClusterWidget(item.config, self.task_context.config['queue'], lambda a: None)
    window.setCentralWidget(widget)
    window.move(self.x() + 100, self.y() + 100)
    window.show()

  def on_touch_screen(self) -> None:
    touch_dialog = TouchDialog(self.task_context.config, self.task_context.stub)
    touch_dialog.resize(self.width()//2, self.height()//2)
    touch_dialog.show()
    touch_dialog.activateWindow()

  def on_orchestration(self) -> None:
    if 'Orchestration' not in self.task_context.config:
      self.task_context.config['Orchestration'] = {}

    dialog = OrchestrationDialog(self.task_context.config['Orchestration'])
    dialog.resize(self.width()//2, self.height()//2)
    dialog.show()
    dialog.activateWindow()

  def on_operator_view(self, subject_window: typing.Optional[SubjectWindow]) -> None:
    """
    Opens a new operator view or brings an existing operator view to the front
    """
    if not subject_window:
      QMessageBox.critical(self, "Unsupported Feature",
                                           "Operator View is not supported in remote executor mode")
      return

    if not self.operator_window or self.operator_window.closed:
      self.operator_window = OperatorWindow(subject_window, self.task_context.config)
    self.operator_window.resize(self.width()//2, self.height()//2)
    self.operator_window.show()
    self.operator_window.activateWindow()

  def closeEvent(self, event: QCloseEvent) -> None: # pylint: disable=invalid-name
    """
    Stop the ROS loop when the user exists
    """
    self.done_future.set_result(None)
    super().closeEvent(event)

  def init(self) -> None:
    """
    Attach an observer to the task_clusters list and update the UI to reflect the new config
    """
    self.task_context.config['task_clusters'].add_observer(self.on_task_clusters_changed)
    self.task_cluster_tabs.clear()
    for i, cluster in enumerate(self.task_context.config['task_clusters']):
      self.on_task_clusters_changed(ObservableCollection.Action.SET, i, cluster)

  def on_reset_trial_history(self) -> None:
    '''
    Resets the trial history
    '''

  def on_load_reward_schedule(self) -> None:
    '''
    Loads a reward schedule from CSV
    '''
    file_name = QFileDialog.getOpenFileName(self, "Load Reward Schedule", "", "*.csv")
    if file_name and file_name[0]:
      numpy.loadtxt(file_name[0], delimiter=',')
      schedules = numpy.loadtxt(file_name[0], delimiter=',', unpack=True).tolist()
      self.task_context.config['reward_schedule']['schedules'] = schedules
      self.task_context.config['reward_schedule']['index'] = 0

  def on_task_clusters_changed(self, action: ObservableCollection.Action,
                               _: typing.Any, value: typing.Any) -> None:
    """
    Adds or removes task cluster tabs in response to changes in the config
    """
    if action == ObservableCollection.Action.DELETE:
      for i in range(self.task_cluster_tabs.count()):
        if self.task_cluster_tabs.widget(i).config is value:
          self.task_cluster_tabs.removeTab(i)
          break
    elif action == ObservableCollection.Action.SET:
      widget = TaskClusterWidget(value, self.task_context.config['queue'], self.on_task_cluster_name_changed)
      self.task_cluster_tabs.addTab(widget, f"{value['name']}: {value['weight']}")

  def on_task_cluster_name_changed(self, config: ObservableCollection) -> None:
    """
    Update the task cluster's tab text when it's name or weight changes
    """
    for i in range(self.task_cluster_tabs.count()):
      if self.task_cluster_tabs.widget(i).config is config:
        self.task_cluster_tabs.setTabText(i, f"{config['name']}: {config['weight']}")
        break

  def on_delete_task_cluster(self, index: int) -> None:
    """
    Delete a task cluster
    """
    tab = self.task_cluster_tabs.widget(index)
    config = tab.config
    #mypy doesn't appears to interpret the union below as a typing.Union[StandardButtons, StandardButton]
    confirm = QMessageBox.question(self, "Delete Task Cluster",
                                   f'Delete Task Cluster {config["name"]}?',
                                   QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
    if confirm == QMessageBox.StandardButton.No:
      return
    remove_by_is(config.parent, config)

  def on_create_task_cluster(self) -> None:
    """
    Create a new task cluster
    """
    self.task_context.config['task_clusters'].append({
      'type': 'task_cluster',
      'name': 'Untitled',
      'weight': 0,
      'tasks': []
    })

  def on_delete_from_queue(self) -> None:
    """
    Delete all selected tasks from the queue
    """
    #pdb.set_trace()
    for selected in (typing.cast(QueueTreeWidgetItem, item) for item in self.queue_tree.selectedItems()):
      remove_by_is(selected.config.parent, selected.config)

  def on_start_stop(self) -> None:
    """
    Toggle task execution on and off
    """
    if self.task_context.running:
      asyncio.get_event_loop().call_soon(self.task_context.stop)
    else:
      self.task_context.start()

  def on_save_config(self) -> None:
    """
    Save the current config to it's original file
    """
    if self.config_data.file_name is None:
      self.on_save_as_config()
      return

    save(self.config_data.file_name, self.task_context.config)

  def on_save_as_config(self) -> None:
    """
    Save the current config to a new file
    """
    file_name = QFileDialog.getSaveFileName(self, "Save Config", "", "*.json *.mat")
    if file_name:
      save(file_name[0], self.task_context.config)
      self.config_data = ConfigData(self.config_data.user_config, file_name[0])
      self.setWindowTitle(f'Task Controller: {self.config_data.file_name}')

  def on_load_config(self) -> None:
    """
    Load a config
    """
    file_name = QFileDialog.getOpenFileName(self, "Load Config", "", "*.json *.mat")
    if file_name and file_name[0]:
      async def task() -> None:
        new_config = load(file_name[0])
        new_config.merge(self.config_data.user_config)
        if 'queue' in new_config:
          del new_config['queue']
        for node in new_config.get('nodes', []):
          if 'Running' in node:
            node['Running'] = False
        del self.task_context.config['task_clusters']
        self.task_context.config.merge(dict((k, v) for k, v in new_config.items() if k != 'queue'))
        self.init()
        self.config_data = ConfigData(self.config_data.user_config, file_name[0])
        self.setWindowTitle(f'Task Controller: {self.config_data.file_name}')
      create_task_with_exc_handling(task())

  def on_queue_changed(self, action: ObservableCollection.Action,
                       _: typing.Any, value: typing.Any) -> None:
    """
    Updates the queue widget to reflect the state of the config's queue
    """
    if action == ObservableCollection.Action.SET:
      if value['type'] == 'task':
        task_item = QueueTreeWidgetItem(value, [f"{value['name']}: {value['goal']}"])
        value.add_observer(functools.partial(self.on_queue_task_changed, task_item))
        self.valid_queue_items.append(task_item)

        for j in range(self.queue_tree.topLevelItemCount()):
          top_level_item = typing.cast(QueueTreeWidgetItem, self.queue_tree.topLevelItem(j))
          if top_level_item.config.get('tasks', None) is value.parent:
            top_level_item.addChild(task_item)
            return

        self.queue_tree.addTopLevelItem(task_item)
      else:
        cluster_item = QueueTreeWidgetItem(value, [value['name']])
        self.valid_queue_items.append(cluster_item)
        self.queue_tree.addTopLevelItem(cluster_item)
        value['tasks'].add_observer(self.on_queue_changed)
        for task in value['tasks']:
          #pdb.set_trace()
          task_item = QueueTreeWidgetItem(task, [f"{task['name']}: {task['goal']}"])
          self.valid_queue_items.append(task_item)
          cluster_item.addChild(task_item)
          task.add_observer(functools.partial(self.on_queue_task_changed, task_item))
    elif action == ObservableCollection.Action.DELETE:
      for i in range(self.queue_tree.topLevelItemCount()):
        root = typing.cast(QueueTreeWidgetItem, self.queue_tree.topLevelItem(i))
        if root.config is value:
          self.queue_tree.takeTopLevelItem(i)
          self.valid_queue_items.remove(root)
          for j in range(root.childCount()):
            child = root.child(j)
            self.valid_queue_items.remove(child)
          break
        for j in range(root.childCount()-1, -1, -1):
          child = typing.cast(QueueTreeWidgetItem, root.child(j))
          if child.config is value:
            root.removeChild(child)
            self.valid_queue_items.remove(child)

  def on_queue_task_changed(self, item: QueueTreeWidgetItem, _action: ObservableCollection.Action,
                            key: typing.Any, _value: typing.Any) -> None:
    """
    Callback used to update queue text as the task's goal count changes
    """
    if key in ('name', 'goal') and item in self.valid_queue_items:
      item.setText(0, f"{item.config['name']}: {item.config['goal']}")
