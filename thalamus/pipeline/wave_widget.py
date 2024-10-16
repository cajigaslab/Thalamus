from ..qt import *

import dataclasses
import inspect
import typing
from ..config import ObservableCollection
from ..observable_item_models import FlatObservableCollectionModel, TreeObservableCollectionModel, TreeObservableCollectionDelegate

WAVE_PROPERTIES = set(['Frequency',
        'Amplitude',
        'Shape',
        'Offset',
        'Duty Cycle',
        'Phase'])

class WaveWidget(QWidget):
  def __init__(self, config, stub):
    super().__init__()
    self.config = config
    self.stub = stub
    self.first_wave = None
    self.waves = None

    if 'Waves' not in self.config:
      self.config['Waves'] = []

    layout = QVBoxLayout()
    self.qlist = QTreeView()

    add_button = QPushButton('Add')
    remove_button = QPushButton('Remove')

    layout.addWidget(self.qlist, 1)
    layout.addWidget(add_button)
    layout.addWidget(remove_button)

    self.setLayout(layout)

    def on_add():
      print('on_add', self.waves)
      if self.waves is None:
        return
      self.waves.append({
        'Frequency': 1.0,
        'Amplitude': 1.0,
        'Shape': 'Sine',
        'Offset': 0.0,
        'Duty Cycle': .5,
        'Phase': 0.0,
      })
    add_button.clicked.connect(on_add)

    def on_remove():
      if self.waves is None:
        return
      rows = sorted(set(i.row() for i in self.qlist.selectedIndexes()), reverse=True)
      for row in rows:
        print(self.waves, row)
        del self.waves[row]
    remove_button.clicked.connect(on_remove)

    observer = self.__on_change
    config.add_recursive_observer(observer, lambda: isdeleted(self))
    config.recap(lambda *args: observer(config, *args))

  def __set_model(self, waves):
    self.waves = waves
    def choices(item, key):
      if key == 'Shape':
        return ['Sine', 'Square', 'Triangle', 'Random']

    model = TreeObservableCollectionModel(self.waves, key_column='', columns=['Frequency', 'Amplitude', 'Shape', 'Offset', 'Duty Cycle', 'Phase'],
                                          show_extra_values=False,
                                          is_editable = lambda o, k: True)
    delegate = TreeObservableCollectionDelegate(model, 3, choices)
    self.qlist.setModel(model)
    self.qlist.setItemDelegate(delegate)

  def __on_change(self, source, action, key, value):
    print('__on_change', source, action, key, value)
    if source is self.config:
      if key == 'Waves':
        self.__set_model(value)
        assert self.waves is not None
        self.waves.recap(lambda *args: self.__on_change(self.config, *args))
      if key in WAVE_PROPERTIES and self.first_wave is not None:
        self.first_wave[key] = value
    elif source is self.waves:
      assert self.waves is not None
      if key == 0:
        if action == ObservableCollection.Action.SET:
          self.first_wave = value
        elif len(self.waves) > 0:
          self.first_wave = self.waves[0]
        else:
          self.first_wave = None
          return
        for p in WAVE_PROPERTIES:
          if p in self.config:
            self.first_wave[p] = self.config[p]
    else:
      if source is self.first_wave:
        self.config[key] = value

