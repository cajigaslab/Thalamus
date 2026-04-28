import platform
from .resources import get_path

EXE_SUFFIX = '.exe' if platform.system() == 'Windows' else ''
native_exe = get_path('native' + EXE_SUFFIX)
