from ..config import ObservableCollection
from ..qt import *

class LuaWidget(QWidget):
  def __init__(self, config, stub):
    super().__init__()
    if "Equations" not in config:
      config["Equations"] = []

    config['Equations'].add_observer(self.on_equations_change, lambda: isdeleted(self))

    self.top_layout = QVBoxLayout()

    self.__layout = QGridLayout()
    self.__layout.setColumnStretch(0, 0)
    self.__layout.setColumnStretch(1, 1)

    self.top_layout.addLayout(self.__layout, 0)
    self.top_layout.addStretch(1)
    self.setLayout(self.top_layout)
    self.lines = []

    for k, v in enumerate(config['Equations']):
      self.on_equations_change(ObservableCollection.Action.SET, k, v)

  def on_equations_change(self, a, k, v):
    if a == ObservableCollection.Action.SET:
      label = QLabel()

      edit = QLineEdit()
      edit.editingFinished.connect(lambda: v.update({'Equation': edit.text()}))

      error_label = QLabel()
      error_label.setStyleSheet('color: red')
      self.lines.insert(k, [label, edit, error_label])
      self.__layout.addWidget(label, 1 + 2*k, 0)
      self.__layout.addWidget(edit, 1 + 2*k, 1)
      self.__layout.addWidget(error_label, 1 + 2*k+1, 0, 1, 2)
      self.__layout.setRowStretch(1 + 2*k, 0)
      self.__layout.setRowStretch(1 + 2*k+1, 0)
      v.add_observer(lambda *args: self.on_equation_change(label, edit, error_label, *args), lambda: isdeleted(self))
      for k2, v2 in v.items():
        self.on_equation_change(label, edit, error_label, ObservableCollection.Action.SET, k2, v2)
    elif a == ObservableCollection.Action.DELETE:
      for w in self.lines[k]:
        self.__layout.removeWidget(w)
        w.setParent(None)
        w.deleteLater()
      del self.lines[k]

  def on_equation_change(self, label: QLabel, edit, error_label, a, k, v):
    print(a, k, v)
    if k == 'Equation':
      if edit.text() != v:
        edit.setText(v)
    elif k == 'Name':
      label.setText(v)
    elif k == 'Error':
      error_label.setText(v)
      style = 'color: red' if v else 'color: black'
      print(style)
      edit.setStyleSheet(style)

  
