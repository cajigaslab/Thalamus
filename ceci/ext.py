import pathlib
from thalamus.pipeline.thalamus_window import Factory, UserData, UserDataType
from thalamus.task_controller.task_context import TaskDescription
#import ext_task
import platform
import ceci_stim_task

def widgets():
  return {
    'EXT_CECI': Factory(None, [
      UserData(UserDataType.DEFAULT, 'Device 1', 'PXI1Slot4', []),
      UserData(UserDataType.DEFAULT, 'Device 2', 'PXI1Slot5', []),
    ])
  }

def library():
  #return [pathlib.Path.cwd() / 'ext.dll', pathlib.Path.cwd() / 'ext2.dll']
  if platform.system() == 'Windows':
    #return pathlib.Path.cwd() / 'rust/target/debug/thalamus_rs.dll'
    return pathlib.Path(__file__).parent / 'ceci.dll'
  else:
    return pathlib.Path(__file__).parent / 'libext.so'

def tasks():
  return [
    TaskDescription('ext_ceci_task', 'Ext Ceci Stim Task', ceci_stim_task.create_widget, ceci_stim_task.run)
  ]
