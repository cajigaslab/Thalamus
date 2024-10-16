from ..qt import *

import dataclasses
import inspect
import typing
from ..config import ObservableCollection
from ..observable_item_models import FlatObservableCollectionModel, TreeObservableCollectionModel, TreeObservableCollectionDelegate

class WaveWidget(QWidget):
  def __init__(self, config, stub):
    super().__init__()
    self.config = config
    self.stub = stub

    if 'Waves' not in self.config:
      self.config['Waves'] = []
    waves = self.config['Waves']

    layout = QVBoxLayout()
    qlist = QTreeView()
    model = TreeObservableCollectionModel(waves, key_column='', columns=['Frequency', 'Amplitude', 'Shape', 'Offset', 'Duty Cycle', 'Phase'],
                                          show_extra_values=False,
                                          is_editable = lambda o, k: True)

    def choices(item, key):
      if key == 'Shape':
        return ['Sine', 'Square', 'Triangle', 'Random']

    delegate = TreeObservableCollectionDelegate(model, 3, choices)
    qlist.setModel(model)
    qlist.setItemDelegate(delegate)

    add_button = QPushButton('Add')
    remove_button = QPushButton('Remove')

    layout.addWidget(qlist, 1)
    layout.addWidget(add_button)
    layout.addWidget(remove_button)

    self.setLayout(layout)

    def on_add():
      waves.append({
        'Frequency': 1.0,
        'Amplitude': 1.0,
        'Shape': 'Sine',
        'Offset': 0.0,
        'Duty Cycle': .5,
        'Phase': 0.0,
      })
    add_button.clicked.connect(on_add)

    def on_remove():
      rows = sorted([i.row() for i in qlist.selectedIndexes()], reverse=True)
      for row in rows:
        del waves[row]
    remove_button.clicked.connect(on_remove)

