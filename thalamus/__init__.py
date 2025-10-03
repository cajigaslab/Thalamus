import platform
from .resources import get_path

EXE_SUFFIX = '.exe' if platform.system() == 'Windows' else ''
native_exe = get_path(__name__, 'native' + EXE_SUFFIX)
