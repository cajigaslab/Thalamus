import collections
import functools
import random
import bisect
import typing
import bisect
import pdb

from ..qt import *
from ..config import *

from ..observable_item_models import FlatObservableCollectionModel, TreeObservableCollectionModel

class SyncWidget(QWidget):
  def __init__(self, config: ObservableDict, stub):
    super().__init__()
    self.config = config
    self.stub = stub
    assert self.config.parent is not None

    if 'Sources' not in self.config:
      self.config['Sources'] = {}
    sources = self.config['Sources']
    nodes = self.config.parent

    layout = QVBoxLayout()
    combo = QComboBox()
    combo_model = FlatObservableCollectionModel(nodes, lambda c: c['name'])
    combo.setModel(combo_model)
    add_button = QPushButton('Add')
    qlist = QTreeView()
    model = TreeObservableCollectionModel(sources, columns=['Channel', 'Is Sync'], show_extra_values=False, is_editable = lambda o, k: k == 'Is Sync')
    qlist.setModel(model)
    #qlist.setItemDelegate(Delegate())
    remove_button = QPushButton('Remove')

    layout.addWidget(combo)
    layout.addWidget(add_button)
    layout.addWidget(qlist, 1)
    layout.addWidget(remove_button)

    self.setLayout(layout)

    def on_add():
      #new_node = combo.currentData()
      name = combo.currentText()
      for node in nodes:
        if node['name'] == name:
          sources[name] = [{'Channel': str(random.random()), 'Is Sync': False}, {'Channel': str(random.random()), 'Is Sync': False}]
      #if new_name in sources:
      #  return
      #sources[new_name] = []
    add_button.clicked.connect(on_add)

    def on_remove():
      for item in qlist.selectedIndexes():
        if item.parent().isValid():
          item = item.parent()
        del sources[item.data()]
    remove_button.clicked.connect(on_remove)

