import platform
import pkg_resources

EXE_SUFFIX = '.exe' if platform.system() == 'Windows' else ''
native_exe = pkg_resources.resource_filename(__name__, 'native' + EXE_SUFFIX)
