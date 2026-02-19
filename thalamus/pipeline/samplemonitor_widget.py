from ..qt import *
from ..config import ObservableDict
from .. import thalamus_pb2_grpc
from ..observable_item_models import FlatObservableCollectionModel, TreeObservableCollectionModel, TreeObservableCollectionDelegate

import logging

LOGGER = logging.getLogger(__name__)

class SampleMonitorWidget(QTabWidget):
  def __init__(self, config: ObservableDict, stub: thalamus_pb2_grpc.ThalamusStub):
    super().__init__()
    assert config.parent is not None
    self.allnodes = config.parent
    self.targets = None
    self.config = config

    if 'Nodes' not in self.config:
      self.config['Nodes'] = []

    layout = QVBoxLayout()
    self.qlist = QTreeView()

    add_button = QPushButton('Add')
    remove_button = QPushButton('Remove')

    layout.addWidget(self.qlist, 1)
    layout.addWidget(add_button)
    layout.addWidget(remove_button)
    self.setLayout(layout)

    def on_add():
      LOGGER.debug('on_add %s', self.nodes)
      if self.nodes is None:
        return
      self.nodes.append({
        'Name': '',
      })
    add_button.clicked.connect(on_add)

    def on_remove():
      if self.nodes is None:
        return
      rows = sorted(set(i.row() for i in self.qlist.selectedIndexes()), reverse=True)
      for row in rows:
        LOGGER.debug('%s %s', self.nodes, row)
        del self.nodes[row]
    remove_button.clicked.connect(on_remove)

    observer = self.__on_change
    config.add_recursive_observer(observer, lambda: isdeleted(self))
    config.recap(lambda *args: observer(config, *args))

  def __set_model(self, nodes):
    self.nodes = nodes
    def choices(item, key):
      if key == 'Name':
        return [n['name'] for n in self.allnodes]

    model = TreeObservableCollectionModel(self.nodes, key_column='#', columns=['Name'],
                                          show_extra_values=False,
                                          is_editable = lambda o, k: True)
    delegate = TreeObservableCollectionDelegate(model, 3, choices)
    self.qlist.setModel(model)
    self.qlist.setItemDelegate(delegate)

  def __on_change(self, source, action, key, value):
    LOGGER.debug('__on_change %s %s %s %s', source, action, key, value)
    if source is self.config:
      if key == 'Nodes':
        self.__set_model(value)
        assert self.nodes is not None
        self.nodes.recap(lambda *args: self.__on_change(self.config, *args))
