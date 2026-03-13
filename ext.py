import pathlib
from thalamus.pipeline.thalamus_window import Factory, UserData, UserDataType
from thalamus.task_controller.task_context import TaskDescription
#import ext_task
import platform

def widgets():
  return {
    'EXT_DEMO': Factory(None, [
      UserData(UserDataType.CHECK_BOX, 'Running', False, []),
      UserData(UserDataType.DOUBLE_SPINBOX, 'Amplitude', 1.0, []),
      UserData(UserDataType.DOUBLE_SPINBOX, 'Frequency', 1.0, []),
    ]),
    'EXT2_DEMO': Factory(None, [
      UserData(UserDataType.CHECK_BOX, 'Running', False, []),
      UserData(UserDataType.DOUBLE_SPINBOX, 'Amplitude', 1.0, []),
      UserData(UserDataType.DOUBLE_SPINBOX, 'Frequency', 1.0, []),
    ])
  }

def library():
  #return [pathlib.Path.cwd() / 'ext.dll', pathlib.Path.cwd() / 'ext2.dll']
  if platform.system() == 'Windows':
    return pathlib.Path.cwd() / 'rust/target/debug/thalamus_rs.dll'
  else:
    return pathlib.Path.cwd() / 'rust/target/debug/libthalamus_rs.so'

def tasks():
  return [
    #TaskDescription('ext_task', 'Ext Task', ext_task.create_widget, ext_task.run)
  ]
