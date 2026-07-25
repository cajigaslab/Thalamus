import typing
import pathlib
import platform
DIR = pathlib.Path(__file__).resolve().parent

try:
  #New resource import
  import importlib
  import importlib.resources

  def get_path(arg1: typing.Any, arg2: str = None) -> str:
    if arg2 is None:
      return str(DIR / arg1)

    try:
      with importlib.resources.path(arg1, arg2) as result:
        return str(result)
    except FileNotFoundError as e:
      return e.filename
        
  def read_text(arg1: typing.Any, arg2: str = None) -> str:
    if arg2 is None:
      return (DIR / arg1).read_text()

    if isinstance(arg1, str):
      arg1 = importlib.__import__(arg1)
    return (importlib.resources.files(arg1) / arg2).read_text()

except ImportError:
  #Old resource import
  import pkg_resources

  def get_path(arg1: typing.Any, arg2: str = None) -> str:
    if arg2 is None:
      arg2 = arg1
      arg1 = __name__

    return pkg_resources.resource_filename(arg1, arg2)

  def read_text(arg1: typing.Any, arg2: str = None) -> str:
    if arg2 is None:
      arg2 = arg1
      arg1 = __name__

    return pkg_resources.resource_string(arg1, arg2)

EXE_SUFFIX = '.exe' if platform.system() == 'Windows' else ''
native_exe = get_path('native' + EXE_SUFFIX)
