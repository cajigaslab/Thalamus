import re
import os
import io
import ssl
import sys
import time
import shutil
import tarfile
import zipfile
import pathlib
import asyncio
import argparse
import subprocess
import urllib.request

if sys.platform == 'win32':
  import winreg

def is_up_to_date(command, regex, required_version):
  if shutil.which(command) is None:
    return '', False

  output = subprocess.check_output([command, '--version'], encoding='utf8')
  version_match = re.search(regex, output)
  version = int(version_match.group(1)), int(version_match.group(2)), int(version_match.group(3))
  return version, version >= required_version

UNHANDLED_EXCEPTION = None

async def grab_exception(routine):
  global UNHANDLED_EXCEPTION
  try:
    await routine
  except Exception as exc:
    UNHANDLED_EXCEPTION = exc

DOWNLOAD_SSL_CONTEXT = ssl.SSLContext(ssl.PROTOCOL_TLSv1_2)
ssl._create_default_https_context = lambda: DOWNLOAD_SSL_CONTEXT 

log_handler=urllib.request.HTTPSHandler(debuglevel=1)

password_mgr = urllib.request.HTTPPasswordMgrWithPriorAuth()

auth_handler = urllib.request.HTTPBasicAuthHandler(password_mgr)

opener = urllib.request.build_opener(auth_handler, log_handler)
urllib.request.install_opener(opener)

CMAKE_VERSION = '3.30.2'

def download(url: str):
  print(f'Downloading {url}: 0%')
  last_print = time.time()
  def reporthook(block_num, block_size, total_size):
    nonlocal last_print
    now = time.time()
    if now - last_print > 1:
      progress = 100*block_num*block_size/total_size
      print(f'Downloading {url}: {progress:.2f}%')
      last_print = now

  path = pathlib.Path(url)
  urllib.request.urlretrieve(url, path.name, reporthook)
  print(f'Downloading {url}: 100%')

def main():
  parser = argparse.ArgumentParser(description='Process some integers.')
  parser.add_argument('--home', default=str(pathlib.Path.home()), help='Use this folder as home')
  parser.add_argument('--ci', action='store_true', help='Use reduced dependencies for CI build')

  args = parser.parse_args()
  home_str = args.home
  home_path = pathlib.Path(home_str)
  reboot_required = False
  if sys.platform == 'win32':
    result = subprocess.run(['net', 'session'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if result.returncode != 0:
      print('Administrator permissions required', file=sys.stderr)
      sys.exit(1)

    new_path = []
    with winreg.OpenKeyEx(winreg.HKEY_CURRENT_USER,
                          'Environment', 
                          0, 
                          winreg.KEY_READ | winreg.KEY_SET_VALUE | winreg.KEY_WOW64_64KEY) as key:
      old_path = winreg.QueryValueEx(key, 'Path')[0]

    #depot_tools
    #if not shutil.which('gclient'):
    #  destination = home_path / 'depot_tools'
    #  subprocess.check_call(['git', 'clone', 'https://chromium.googlesource.com/chromium/tools/depot_tools.git', destination])

    subprocess.check_call([sys.executable, '-m', 'pip', 'install', '-U', 'setuptools'], cwd=home_str)
    subprocess.check_call([sys.executable, '-m', 'pip', 'install', '-r', str(pathlib.Path.cwd()/'requirements.txt')], cwd=home_str)

    #nasm
    if not shutil.which('nasm'):
      destination = os.environ['USERPROFILE'] + '\\nasm-2.15.05'
      new_path.append(destination)
      if not (pathlib.Path(destination) / 'nasm.exe').exists():
        download('https://www.nasm.us/pub/nasm/releasebuilds/2.15.05/win64/nasm-2.15.05-win64.zip')
        subprocess.check_call(['powershell', '-Command', 'Expand-Archive -DestinationPath ' + os.environ['USERPROFILE'] + ' nasm-2.15.05-win64.zip'])

    #clang
    clang_which = shutil.which('clang')
    print('Current clang:', clang_which)
    if not clang_which or args.ci:
      destination = 'C:\\Program Files\\LLVM\\bin'
      new_path.append(destination)
      expected_clang = pathlib.Path(destination) / 'clang.exe'
      print(f'{expected_clang} exists: {expected_clang.exists()}')
      if not expected_clang.exists():
        download('https://github.com/llvm/llvm-project/releases/download/llvmorg-19.1.0/LLVM-19.1.0-win64.exe')
        subprocess.check_call(['LLVM-19.1.0-win64.exe', '/S'])
        #waiting = True
        #while waiting:
        #  print('waiting for LLVM')
        #  time.sleep(1)
        #  waiting = False
        #  for p in psutil.process_iter():
        #    if 'LLVM' in p.name():
        #      waiting = True

    #pkg-config
    pkg_config_which = shutil.which('pkg-config')
    if not pkg_config_which or 'strawberry' in pkg_config_which.lower():
      destination = os.environ['USERPROFILE'] + '\\pkg-config-lite-0.28-1\\bin'
      new_path.append(destination)
      if not (pathlib.Path(destination) / 'pkg-config.exe').exists():
        download('https://zenlayer.dl.sourceforge.net/project/pkgconfiglite/0.28-1/pkg-config-lite-0.28-1_bin-win32.zip')
        subprocess.check_call(['powershell', '-Command', 'Expand-Archive -DestinationPath ' + os.environ['USERPROFILE'] + ' pkg-config-lite-0.28-1_bin-win32.zip'])

    #cmake
    if not shutil.which('cmake'):
      destination = os.environ['USERPROFILE'] + f'\\cmake-{CMAKE_VERSION}-windows-x86_64\\bin'
      new_path.append(destination)
      if not (pathlib.Path(destination) / 'cmake.exe').exists():
        download(f'https://github.com/Kitware/CMake/releases/download/v{CMAKE_VERSION}/cmake-{CMAKE_VERSION}-windows-x86_64.zip')
        subprocess.check_call(['powershell', '-Command', 'Expand-Archive -DestinationPath ' + os.environ['USERPROFILE'] + f' cmake-{CMAKE_VERSION}-windows-x86_64.zip'])
    
    if new_path:
      if 'GITHUB_PATH' in os.environ:
        print('GITHUB_PATH', os.environ['GITHUB_PATH'])
        with open(os.environ['GITHUB_PATH'], 'a') as path_file:
          path_file.writelines(str(p) + os.linesep for p in new_path)
      new_path.append(old_path)
      new_path = os.pathsep.join(str(p) for p in new_path)
      print(new_path)

      with winreg.OpenKeyEx(winreg.HKEY_CURRENT_USER,
                            'Environment', 
                            0, 
                            winreg.KEY_READ | winreg.KEY_SET_VALUE | winreg.KEY_WOW64_64KEY) as key:
        winreg.SetValueEx(key, 'Path', 0, winreg.REG_SZ, new_path)
      print('PATHSET')

    msys2_root, msys64_root = pathlib.Path('C:/MSYS2'), pathlib.Path('C:/MSYS64')
    #msys
    if not (msys2_root / 'msys2_shell.cmd').exists() and not (msys64_root / 'msys2_shell.cmd').exists():
      print('Installing msys2')
      download('https://github.com/msys2/msys2-installer/releases/download/2023-07-18/msys2-base-x86_64-20230718.sfx.exe')
      subprocess.check_call(['msys2-base-x86_64-20230718.sfx.exe', '-y', '-oC:\\'])

    msys2_root = msys2_root if msys2_root.exists() else msys64_root

    print('make exists before:', (msys2_root / 'usr/bin/make.exe').exists())
    if not (msys2_root / 'usr/bin/make.exe').exists():
      subprocess.check_call([str(msys2_root / 'msys2_shell.cmd'), '-here', '-use-full-path', '-no-start', '-defterm', '-c', 'pacman -Syu'])
      subprocess.check_call([str(msys2_root / 'msys2_shell.cmd'), '-here', '-use-full-path', '-no-start', '-defterm', '-c', 'pacman --noconfirm -S make diffutils binutils gcc'])
    print('make exists:', (msys2_root / 'usr/bin/make.exe').exists())

    if not pathlib.Path('C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe').exists():
      download('https://aka.ms/vs/17/release/vs_community.exe')
      subprocess.check_call(['start', '/w', 'vs_community.exe', '--quiet', '--wait', '--norestart', '--add', 'Microsoft.VisualStudio.Workload.NativeDesktop', '--add', 'Microsoft.VisualStudio.Workload.NativeGame', '--includeRecommended'], shell=True)

    with winreg.OpenKeyEx(winreg.HKEY_LOCAL_MACHINE,
                          r'SYSTEM\CurrentControlSet\Control\FileSystem', 
                          0, 
                          winreg.KEY_READ | winreg.KEY_SET_VALUE | winreg.KEY_WOW64_64KEY) as key:
      value, value_type = winreg.QueryValueEx(key, 'LongPathsEnabled')
      if value == 0:
        winreg.SetValueEx(key, 'LongPathsEnabled', 0, winreg.REG_DWORD, 1)
        reboot_required = True

  elif sys.platform == 'darwin':
    if not shutil.which('brew'):
      subprocess.check_call(['curl', '-L', '--output', 'brew_install.sh', 'https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh'])
      subprocess.check_call(['bash', 'brew_install.sh'])

    subprocess.check_call(['brew', 'install', 'autoconf', 'automake', 'libtool', 'pcre2', 'pkg-config'])
    subprocess.check_call(['touch', str(home_path / '.thalamusrc')])

    #depot_tools
    if not shutil.which('gclient'):
      destination = home_path / 'depot_tools'
      subprocess.check_call(['git', 'clone', 'https://chromium.googlesource.com/chromium/tools/depot_tools.git', destination])

    #nasm
    if not shutil.which('nasm'):
      subprocess.check_call(['curl', '-L', '-o', 'nasm-2.15.05-macosx.zip', 'https://www.nasm.us/pub/nasm/releasebuilds/2.15.05/macosx/nasm-2.15.05-macosx.zip'])
      subprocess.check_call(['unzip', '-d', os.environ['HOME'], 'nasm-2.15.05-macosx.zip'])
      with open(str(home_path / '.thalamusrc'), 'a') as bashrc:
        bashrc.write(f'\nexport PATH={home_str}/nasm-2.15.05:$PATH\n')

    #cmake
    if not shutil.which('cmake'):
      subprocess.check_call(['curl', '-L', '-o', f'cmake-{CMAKE_VERSION}-macos-universal.tar.gz', 'https://github.com/Kitware/CMake/releases/download/v{CMAKE_VERSION}/cmake-{CMAKE_VERSION}-macos-universal.tar.gz'])
      subprocess.check_call(['tar', '-xvzf', f'cmake-{CMAKE_VERSION}-macos-universal.tar.gz', '-C', os.environ['HOME']])
      with open(str(home_path / '.thalamusrc'), 'a') as bashrc:
        bashrc.write(f'\nexport PATH={home_str}/cmake-{CMAKE_VERSION}-macos-universal/CMake.app/Contents/bin:$PATH\n')

    subprocess.check_call([sys.executable, '-m', 'pip', 'install', '-U', 'setuptools'], cwd=home_str)
    subprocess.check_call([sys.executable, '-m', 'pip', 'install', '-r', str(pathlib.Path.cwd()/'requirements.txt')], cwd=home_str)

    with open(str(home_path / '.bash_profile'), 'r') as bash_profile:
      bash_profile_content = bash_profile.read()
    if "source ~/.thalamusrc" not in bash_profile_content:
      with open(str(home_path / '.bash_profile'), 'a') as bash_profile:
        bash_profile.write(f'\nsource ~/.thalamusrc\n')


  else:
    (home_path / '.thalamusrc').touch()
    subprocess.check_call(['sudo', 'apt', 'install', '-y', 
                          'python3-pip', 'git', 'wget', 'sudo', 'curl', 'ninja-build', 'lsb-release',
                          'libsm-dev', 'libice-dev', 'libudev-dev', 'libdbus-1-dev', 'libzstd-dev', 'libbz2-dev',
                          'libgles2-mesa-dev',
                          'libfontconfig1-dev',
                          'libfreetype6-dev',
                          'libx11-dev',
                          'libx11-xcb-dev',
                          'libxext-dev',
                          'libxfixes-dev',
                          'libxi-dev',
                          'libdrm-dev',
                          'libxrender-dev',
                          'libxcb1-dev',
                          'libxcb-glx0-dev',
                          'libxcb-keysyms1-dev',
                          'libxcb-image0-dev',
                          'libxcb-shm0-dev',
                          'libxcb-icccm4-dev',
                          'libxcb-sync0-dev',
                          'libxcb-xfixes0-dev',
                          'libxcb-shape0-dev',
                          'libxcb-randr0-dev',
                          'libxcb-render-util0-dev',
                          'libxcb-xinerama0-dev',
                          'libxkbcommon-dev',
                          'libxkbcommon-x11-dev',
                          'libxcb-util-dev',
                          'libxcb-xinput-dev',
                          'libffi-dev',
                          'libkrb5-dev',
                          'libva-dev',
                          'libvdpau-dev',
                          'libasound2-dev',
                          'libxv-dev',
                          'liblzma-dev', 'libtool', 'flex',
                          'nasm', 'libpcre2-dev', 'bison',
                          'libbrotli-dev', 'autotools-dev', 'automake',
                          'swig', 'debconf-utils', 'libusb-1.0-0', 'ffmpeg'])
    subprocess.check_call([sys.executable, '-m', 'pip', 'install', '-U', 'setuptools'], cwd=home_str)
    if args.ci:
      subprocess.check_call([sys.executable, '-m', 'pip', 'install', '-r', str(pathlib.Path.cwd()/'requirements-ci.txt')], cwd=home_str)
    else:
      subprocess.check_call([sys.executable, '-m', 'pip', 'install', '-r', str(pathlib.Path.cwd()/'requirements.txt')], cwd=home_str)
                          
    _, clang_is_current = is_up_to_date('clang++', r'clang version (\d+).(\d+).(\d+)', (10, 0, 0))
    if not clang_is_current:
      subprocess.check_call(['sudo', 'apt', 'install', '-y', 'clang'])
    clang_version, _ = is_up_to_date('clang++', r'clang version (\d+).(\d+).(\d+)', (10, 0, 0))
    subprocess.check_call(['sudo', 'apt', 'install', '-y', f'libclang-{clang_version[0]}-dev', ])

    _, cmake_is_current = is_up_to_date('cmake', r'cmake version (\d+).(\d+).(\d+)', (3, 16, 0))
    if not cmake_is_current:
      subprocess.check_call(['wget', f'https://github.com/Kitware/CMake/releases/download/v{CMAKE_VERSION}/cmake-{CMAKE_VERSION}-linux-x86_64.sh'])
      (home_path / '.local').mkdir(exist_ok=True)
      subprocess.check_call(['sh', f'./cmake-{CMAKE_VERSION}-linux-x86_64.sh', f'--prefix={home_str}/.local', '--skip-license', '--include-subdir'])
      with open(str(home_path / '.thalamusrc'), 'a') as bashrc:
        bashrc.write(f'\nexport PATH={home_str}/.local/cmake-{CMAKE_VERSION}-linux-x86_64/bin:$PATH\n')

    #depot_tools
    if not shutil.which('gclient'):
      destination = home_path / 'depot_tools'
      subprocess.check_call(['git', 'clone', 'https://chromium.googlesource.com/chromium/tools/depot_tools.git', destination])

    bashrc_path = home_path / '.bashrc'
    bashrc_path.touch()
    with open(str(bashrc_path), 'r') as bashrc:
      bashrc_content = bashrc.read()
    if "source ~/.thalamusrc" not in bashrc_content:
      with open(str(home_path / '.bashrc'), 'a') as bashrc:
        bashrc.write(f'\nsource ~/.thalamusrc\n')
        
  if reboot_required:
    print("""
  _____      _                 _          
 |  __ \    | |               | |         
 | |__) |___| |__   ___   ___ | |_        
 |  _  // _ \ '_ \ / _ \ / _ \| __|       
 | | \ \  __/ |_) | (_) | (_) | |_        
 |_|__\_\___|_.__/ \___/ \___/ \__|     _ 
 |  __ \                (_)            | |
 | |__) |___  __ _ _   _ _ _ __ ___  __| |
 |  _  // _ \/ _` | | | | | '__/ _ \/ _` |
 | | \ \  __/ (_| | |_| | | | |  __/ (_| |
 |_|  \_\___|\__, |\__,_|_|_|  \___|\__,_|
                | |                       
                |_|                      
""")

if __name__ == '__main__':
  main()
