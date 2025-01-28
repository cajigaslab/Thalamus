try:
  import PyQt6
  HAS_QT6 = True
except ImportError:
  HAS_QT6 = False

if HAS_QT6:
  from PyQt6.QtGui import (QTransform, QPolygon, QOpenGLContext, QPainter, QImage, QMouseEvent, QColor, QFont, 
                           QContextMenuEvent, QAction, QPixmap, QPainterPath, QBrush, QKeyEvent, QStandardItemModel,
                           QSurfaceFormat, QOffscreenSurface, QOpenGLContext, QPen, QFontMetrics, QCloseEvent,
                           QMoveEvent, QResizeEvent, QMatrix4x4, QVector3D, QPaintEvent, QQuaternion, QWheelEvent,
                           QIcon, QFontDatabase, QRadialGradient)
  from PyQt6.QtWidgets import (QWidget, QProgressDialog, QSizePolicy, QAbstractScrollArea, QGridLayout, QSlider, 
                               QSpinBox, QLabel, QVBoxLayout, QHBoxLayout, QComboBox, QSpinBox, QCheckBox,
                               QPushButton, QDialog, QRadioButton, QTextEdit, QMainWindow, QFileDialog, QMenu,
                               QInputDialog, QLineEdit, QGroupBox, QFormLayout, QMessageBox, QTabWidget,
                               QDoubleSpinBox, QDockWidget, QApplication, QMenuBar, QItemDelegate, QSplitter,
                               QTreeWidget, QTreeWidgetItem, QListWidget, QTreeView, QTableView, QListView,
                               QTableWidget, QTableWidgetItem, QWizard, QWizardPage, QProgressBar, QAbstractItemView,
                               QStyleOptionViewItem, QScrollArea, QColorDialog, QFrame)
  from PyQt6.QtOpenGLWidgets import (QOpenGLWidget)
  from PyQt6.QtMultimedia import QSoundEffect, QMediaPlayer
  from PyQt6.QtOpenGL import (QOpenGLFramebufferObjectFormat, QOpenGLFramebufferObject, QOpenGLBuffer, QOpenGLShader,
                              QOpenGLShaderProgram)
  from PyQt6.QtCore import (QPoint, QRect, QPointF, Qt, QSize, QSizeF, QDir, QModelIndex, QAbstractItemModel, QVariant,
                            QBuffer, QAbstractListModel, QAbstractTableModel, QLineF, QRectF, QItemSelection, QUrl,
                            QTimer, QSortFilterProxyModel, PYQT_VERSION_STR)
  from matplotlib.backends.backend_qtagg import FigureCanvasQTAgg

  def qt_to_polygonf(polygon):
    return polygon.toPolygonF()

  def qt_get_x(e):
    return e.position().x()
  def qt_get_y(e):
    return e.position().y()

  try:
    import PyQt6.sip
    isdeleted = PyQt6.sip.isdeleted
    voidptr = PyQt6.sip.voidptr
  except ImportError:
    import sip
    isdeleted = sip.isdeleted
    voidptr = sip.voidptr

  class QSound(QSoundEffect):
    def __init__(self, filename):
      super().__init__()
      self.setSource(QUrl.fromLocalFile(filename))

  def qt_screen_geometry_mm() -> QSizeF:
    return QApplication.screens()[0].physicalSize()

  def qt_screen_geometry() -> QRect:
    return QApplication.screens()[0].geometry()

else:
  from PyQt5.QtGui import (QTransform, QPolygon, QPolygonF, QOpenGLContext, QPainter, QImage, QMouseEvent, QColor,
                           QFont, QContextMenuEvent, QPixmap, QPainterPath, QBrush, QKeyEvent, QStandardItemModel,
                           QSurfaceFormat, QOffscreenSurface, QOpenGLContext, QOpenGLFramebufferObjectFormat,
                           QOpenGLFramebufferObject, QPen, QStandardItem, QTextCursor, QFontMetrics, QCloseEvent,
                           QMoveEvent, QResizeEvent, QMatrix4x4, QVector3D, QPaintEvent, QQuaternion, QWheelEvent,
                           QOpenGLBuffer, QOpenGLShaderProgram, QOpenGLShader, QIcon, QFontDatabase, QRadialGradient)
  from PyQt5.QtWidgets import (QWidget, QProgressDialog, QSizePolicy, QAbstractScrollArea, QGridLayout, QSlider, 
                               QSpinBox, QLabel, QVBoxLayout, QHBoxLayout, QComboBox, QSpinBox, QCheckBox, QPushButton,
                               QDialog, QRadioButton, QTextEdit, QMainWindow, QFileDialog, QMenu, QInputDialog, 
                               QLineEdit, QGroupBox, QFormLayout, QMessageBox, QTabWidget, QDoubleSpinBox, QDockWidget, 
                               QApplication, QOpenGLWidget, QAction, QAbstractItemView, QTreeView, QTableView,
                               QListView, QMenuBar, QItemDelegate, QSplitter, QTreeWidget, QTreeWidgetItem,
                               QListWidget, QTableWidget, QTableWidgetItem, QWizard, QWizardPage, QProgressBar,
                               QStyleOptionViewItem, QScrollArea, QColorDialog, QFrame)
  from PyQt5.QtCore import (QPoint, QRect, QPointF, Qt, QSize, QSizeF, QDir, QItemSelectionModel, QModelIndex, QTimer,
                            QAbstractListModel, QAbstractItemModel, QAbstractTableModel, QLineF, QRectF, QBuffer,
                            QItemSelection, QVariant, QUrl, QSortFilterProxyModel, PYQT_VERSION_STR)
  from PyQt5.QtMultimedia import QSound, QMediaPlayer, QAbstractVideoSurface, QVideoFrame, QMediaContent
  from matplotlib.backends.backend_qt5agg import FigureCanvasQTAgg

  def qt_to_polygonf(polygon) -> QPolygonF:
    return QPolygonF(polygon)

  def qt_get_x(e):
    return e.x()
  def qt_get_y(e):
    return e.y()

  try:
    import PyQt5.sip
    isdeleted = PyQt5.sip.isdeleted
    voidptr = PyQt5.sip.voidptr
  except ImportError:
    import sip
    isdeleted = sip.isdeleted
    voidptr = sip.voidptr

  def qt_move_attr(source, destination, key):
    setattr(destination, key, getattr(source, key))

  qt_move_attr(Qt, Qt.DockWidgetArea, 'RightDockWidgetArea')
  qt_move_attr(Qt, Qt.DockWidgetArea, 'LeftDockWidgetArea')
  qt_move_attr(Qt, Qt.DockWidgetArea, 'TopDockWidgetArea')
  qt_move_attr(Qt, Qt.DockWidgetArea, 'BottomDockWidgetArea')
  qt_move_attr(Qt, Qt.DockWidgetArea, 'NoDockWidgetArea')
  qt_move_attr(QAbstractItemView, QAbstractItemView.SelectionMode, 'SingleSelection')
  qt_move_attr(QAbstractItemView, QAbstractItemView.SelectionMode, 'ExtendedSelection')
  qt_move_attr(QSizePolicy, QSizePolicy.Policy, 'Expanding')
  qt_move_attr(Qt, Qt.Orientation, 'Horizontal')
  qt_move_attr(Qt, Qt.Orientation, 'Vertical')
  qt_move_attr(Qt, Qt.ItemDataRole, 'DisplayRole')
  qt_move_attr(Qt, Qt.ItemDataRole, 'UserRole')
  qt_move_attr(Qt, Qt.ItemDataRole, 'CheckStateRole')
  qt_move_attr(Qt, Qt.ItemDataRole, 'EditRole')
  qt_move_attr(Qt, Qt.ItemFlag, 'ItemIsEditable')
  qt_move_attr(Qt, Qt.ItemFlag, 'ItemIsEnabled')
  qt_move_attr(Qt, Qt.ItemFlag, 'ItemIsUserCheckable')
  qt_move_attr(Qt, Qt.CheckState, 'Checked')
  qt_move_attr(Qt, Qt.CheckState, 'Unchecked')
  qt_move_attr(Qt, Qt.AlignmentFlag, 'AlignTop')
  qt_move_attr(Qt, Qt.AlignmentFlag, 'AlignLeft')
  qt_move_attr(Qt, Qt.AlignmentFlag, 'AlignRight')
  qt_move_attr(Qt, Qt.AlignmentFlag, 'AlignCenter')
  qt_move_attr(Qt, Qt.AlignmentFlag, 'AlignHCenter')
  qt_move_attr(Qt, Qt.AlignmentFlag, 'AlignJustify')
  qt_move_attr(Qt, Qt.CursorShape, 'SplitHCursor')
  qt_move_attr(Qt, Qt.CursorShape, 'SplitVCursor')
  qt_move_attr(Qt, Qt.FillRule, 'WindingFill')
  qt_move_attr(Qt, Qt.FocusPolicy, 'StrongFocus')
  qt_move_attr(Qt, Qt.WidgetAttribute, 'WA_TransparentForMouseEvents')
  qt_move_attr(QImage, QImage.Format, 'Format_Grayscale8')
  qt_move_attr(QImage, QImage.Format, 'Format_RGB888')
  qt_move_attr(QImage, QImage.Format, 'Format_Indexed8')
  qt_move_attr(QMessageBox, QMessageBox.StandardButton, 'Yes')
  qt_move_attr(QMessageBox, QMessageBox.StandardButton, 'No')

  for key in ['red', 'green', 'blue', 'black', 'white']:
    qt_move_attr(Qt, Qt.GlobalColor, key)

  for key in ['Key_0', 'Key_1', 'Key_2', 'Key_3', 'Key_4', 'Key_5', 'Key_6', 'Key_7', 'Key_8',
    'Key_9',
    'Key_A',
    'Key_B',
    'Key_C',
    'Key_D',
    'Key_E',
    'Key_F',
    'Key_G',
    'Key_H',
    'Key_I',
    'Key_J',
    'Key_K',
    'Key_L',
    'Key_M',
    'Key_N',
    'Key_O',
    'Key_P',
    'Key_Q',
    'Key_R',
    'Key_S',
    'Key_T',
    'Key_U',
    'Key_V',
    'Key_W',
    'Key_X',
    'Key_Y',
    'Key_Z']:
    qt_move_attr(Qt, Qt.Key, key)

  def qt_screen_geometry_mm() -> QSizeF:
    return QApplication.desktop().physicalSize()

  def qt_screen_geometry() -> QRect:
    return QApplication.desktop().screenGeometry()
