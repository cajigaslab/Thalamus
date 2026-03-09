import pathlib
from thalamus.pipeline.thalamus_window import Factory, UserData, UserDataType
from thalamus.task_controller.task_context import TaskDescription
import ext_task

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
  return pathlib.Path.cwd() / 'ext_rust/target/debug/ext.dll'

def tasks():
  return [
    TaskDescription('ext_task', 'Ext Task', ext_task.create_widget, ext_task.run)
  ]
