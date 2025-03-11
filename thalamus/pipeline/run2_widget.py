from ..qt import *
from ..config import ObservableDict
from .. import thalamus_pb2_grpc
from ..observable_item_models import FlatObservableCollectionModel, TreeObservableCollectionModel, TreeObservableCollectionDelegate

class Run2Widget(QTabWidget):
  def __init__(self, config: ObservableDict, stub: thalamus_pb2_grpc.ThalamusStub):
    super().__init__()
    assert config.parent is not None
    self.nodes = config.parent
    self.targets = None
    self.config = config

    if 'Running' not in self.config:
      self.config['Running'] = False
    if 'Targets' not in self.config:
      self.config['Targets'] = []
    if 'tab' not in self.config:
      self.config['tab'] = 0

    layout = QVBoxLayout()
    self.qlist = QTreeView()

    add_button = QPushButton('Add')
    remove_button = QPushButton('Remove')

    layout.addWidget(self.qlist, 1)
    layout.addWidget(add_button)
    layout.addWidget(remove_button)

    edit_widget = QWidget()
    edit_widget.setLayout(layout)

    self.toggle_button = QPushButton('')
    self.toggle_button.clicked.connect(lambda: config.update({'Running': not config['Running']}))

    self.addTab(edit_widget, "Edit")
    self.addTab(self.toggle_button, "Button")
    self.currentChanged.connect(lambda v: config.update({'tab': v}))

    def on_add():
      print('on_add', self.targets)
      if self.targets is None:
        return
      self.targets.append({
        'Name': '',
        'Address': '',
      })
    add_button.clicked.connect(on_add)

    def on_remove():
      if self.targets is None:
        return
      rows = sorted(set(i.row() for i in self.qlist.selectedIndexes()), reverse=True)
      for row in rows:
        print(self.targets, row)
        del self.targets[row]
    remove_button.clicked.connect(on_remove)

    observer = self.__on_change
    config.add_recursive_observer(observer, lambda: isdeleted(self))
    config.recap(lambda *args: observer(config, *args))

  def __set_model(self, targets):
    self.targets = targets
    def choices(item, key):
      if key == 'Shape':
        return [n['name'] for n in self.nodes]

    model = TreeObservableCollectionModel(self.targets, key_column='#', columns=['Name', 'Address'],
                                          show_extra_values=False,
                                          is_editable = lambda o, k: True)
    delegate = TreeObservableCollectionDelegate(model, 3, choices)
    self.qlist.setModel(model)
    self.qlist.setItemDelegate(delegate)

  def __on_change(self, source, action, key, value):
    print('__on_change', source, action, key, value)
    if source is self.config:
      if key == 'Targets':
        self.__set_model(value)
        assert self.targets is not None
        self.targets.recap(lambda *args: self.__on_change(self.config, *args))
      elif key == 'Running':
        if not value:
          self.toggle_button.setText('START')
          self.toggle_button.setStyleSheet('QPushButton {background-color: green;}')
        else:
          self.toggle_button.setText('STOP')
          self.toggle_button.setStyleSheet('QPushButton {background-color: red;}')
      elif key == 'tab':
        self.setCurrentIndex(value)

