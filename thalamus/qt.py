import sys

#if sys.version_info[1] >= 7:
if True:
  from PyQt6.QtGui import (QTransform, QPolygon, QOpenGLContext, QPainter, QImage, QMouseEvent, QColor, QFont, 
                           QContextMenuEvent, QAction, QPixmap, QPainterPath, QBrush, QKeyEvent, QStandardItemModel,
                           QSurfaceFormat, QOffscreenSurface, QOpenGLContext, QPen, QFontMetrics, QCloseEvent,
                           QMoveEvent, QResizeEvent, QMatrix4x4, QVector3D, QPaintEvent, QQuaternion, QWheelEvent,
                           QIcon)
  from PyQt6.QtWidgets import (QWidget, QProgressDialog, QSizePolicy, QAbstractScrollArea, QGridLayout, QSlider, 
                               QSpinBox, QLabel, QVBoxLayout, QHBoxLayout, QComboBox, QSpinBox, QCheckBox,
                               QPushButton, QDialog, QRadioButton, QTextEdit, QMainWindow, QFileDialog, QMenu,
                               QInputDialog, QLineEdit, QGroupBox, QFormLayout, QMessageBox, QTabWidget,
                               QDoubleSpinBox, QDockWidget, QApplication, QMenuBar, QItemDelegate, QSplitter,
                               QTreeWidget, QTreeWidgetItem, QListWidget, QTreeView, QTableView, QListView,
                               QTableWidget, QTableWidgetItem, QWizard, QWizardPage, QProgressBar, QAbstractItemView,
                               QStyleOptionViewItem)
  from PyQt6.QtOpenGLWidgets import (QOpenGLWidget)
  from PyQt6.QtMultimedia import QSoundEffect, QMediaPlayer
  from PyQt6.QtOpenGL import (QOpenGLFramebufferObjectFormat, QOpenGLFramebufferObject, QOpenGLBuffer, QOpenGLShader,
                              QOpenGLShaderProgram)
  from PyQt6.QtCore import (QPoint, QRect, QPointF, Qt, QSize, QDir, QModelIndex, QAbstractItemModel, QVariant,
                            QAbstractListModel, QAbstractTableModel, QLineF, QRectF, QItemSelection, QUrl,
                            PYQT_VERSION_STR)

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
      self.setSource(filename)

  def qt_screen_geometry() -> QRect:
    return QApplication.screens()[0].geometry()

else:
  from PyQt5.QtGui import (QTransform, QPolygon, QPolygonF, QOpenGLContext, QPainter, QImage, QMouseEvent, QColor,
                           QFont, QContextMenuEvent, QPixmap, QPainterPath, QBrush, QKeyEvent, QStandardItemModel,
                           QSurfaceFormat, QOffscreenSurface, QOpenGLContext, QOpenGLFramebufferObjectFormat,
                           QOpenGLFramebufferObject, QPen, QStandardItem, QTextCursor, QFontMetrics, QCloseEvent,
                           QMoveEvent, QResizeEvent, QMatrix4x4, QVector3D, QPaintEvent, QQuaternion, QWheelEvent,
                           QOpenGLBuffer, QOpenGLShaderProgram, QOpenGLShader, QIcon)
  from PyQt5.QtWidgets import (QWidget, QProgressDialog, QSizePolicy, QAbstractScrollArea, QGridLayout, QSlider, 
                               QSpinBox, QLabel, QVBoxLayout, QHBoxLayout, QComboBox, QSpinBox, QCheckBox, QPushButton,
                               QDialog, QRadioButton, QTextEdit, QMainWindow, QFileDialog, QMenu, QInputDialog, 
                               QLineEdit, QGroupBox, QFormLayout, QMessageBox, QTabWidget, QDoubleSpinBox, QDockWidget, 
                               QApplication, QOpenGLWidget, QAction, QAbstractItemView, QTreeView, QTableView,
                               QListView, QMenuBar, QItemDelegate, QSplitter, QTreeWidget, QTreeWidgetItem,
                               QListWidget, QTableWidget, QTableWidgetItem, QWizard, QWizardPage, QProgressBar,
                               QStyleOptionViewItem)
  from PyQt5.QtCore import (QPoint, QRect, QPointF, Qt, QSize, QDir, QItemSelectionModel, QModelIndex,
                            QAbstractListModel, QAbstractItemModel, QAbstractTableModel, QLineF, QRectF,
                            QItemSelection, QVariant, QUrl, PYQT_VERSION_STR)
  from PyQt5.QtMultimedia import QSound, QMediaPlayer, QAbstractVideoSurface, QVideoFrame, QMediaContent

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

  def qt_screen_geometry() -> QRect:
    return QApplication.desktop().screenGeometry()
