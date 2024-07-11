import re
import os
import io
import ssl
import sys
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

def download(url: str):
  print(f'Downloading {url}: 0%')
  def reporthook(block_num, block_size, total_size):
    progress = 100*block_num*block_size/total_size
    print(f'Downloading {url}: {progress:.2f}%')

  path = pathlib.Path(url)
  urllib.request.urlretrieve(url, path.name, reporthook)
  print(f'Downloading {url}: 100%')

def main():
  parser = argparse.ArgumentParser(description='Process some integers.')
  parser.add_argument('--home', default=str(pathlib.Path.home()), help='Use this folder as home')

  args = parser.parse_args()
  home_str = args.home
  home_path = pathlib.Path(home_str)
  reboot_required = False
  if sys.platform == 'win32':
    result = subprocess.run(['net', 'session'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if result.returncode != 0:
      print('Administrator permissions required', file=sys.stderr)
      sys.exit(1)

    old_path = os.environ['PATH']
    new_path = []
    #nasm
    if not shutil.which('nasm'):
      destination = os.environ['USERPROFILE'] + '\\nasm-2.15.05'
      new_path.append(destination)
      if not (pathlib.Path(destination) / 'nasm.exe').exists():
        download('https://www.nasm.us/pub/nasm/releasebuilds/2.15.05/win64/nasm-2.15.05-win64.zip')
        subprocess.check_call(['powershell', '-Command', 'Expand-Archive -DestinationPath ' + os.environ['USERPROFILE'] + ' nasm-2.15.05-win64.zip'])

    #cmake
    if not shutil.which('cmake'):
      destination = os.environ['USERPROFILE'] + '\\cmake-3.24.0-windows-x86_64\\bin'
      new_path.append(destination)
      if not (pathlib.Path(destination) / 'cmake.exe').exists():
        download('https://github.com/Kitware/CMake/releases/download/v3.24.0/cmake-3.24.0-windows-x86_64.zip')
        subprocess.check_call(['powershell', '-Command', 'Expand-Archive -DestinationPath ' + os.environ['USERPROFILE'] + ' cmake-3.24.0-windows-x86_64.zip'])
    
    #perl
    if not shutil.which('perl'):
      print('Installing perl')
      destination = 'C:\\Strawberry\\perl\\bin' 
      new_path.append(destination)
      if not (pathlib.Path(destination) / 'perl.exe').exists():
        download('https://strawberryperl.com/download/5.32.1.1/strawberry-perl-5.32.1.1-64bit.msi')
        subprocess.check_call(['msiexec', '/quiet', '/i', 'strawberry-perl-5.32.1.1-64bit.msi'])
    print(list(pathlib.Path('C:\\Strawberry').iterdir()))
    
    if new_path:
      if 'GITHUB_PATH' in os.environ:
        print('GITHUB_PATH', os.environ['GITHUB_PATH'])
        with open(os.environ['GITHUB_PATH'], 'a') as path_file:
          path_file.writelines(p + os.linesep for p in new_path)
      new_path.append(old_path)
      new_path = os.pathsep.join(new_path)
      print(new_path)
      subprocess.check_call(['setx', 'PATH', new_path])
      print('PATHSET')

    subprocess.check_call([sys.executable, '-m', 'pip', 'install', '-U', 'setuptools', 'ninja'], cwd=home_str)
    subprocess.check_call([sys.executable, '-m', 'pip', 'install', '-r', str(pathlib.Path.cwd()/'requirements.txt')], cwd=home_str)

    msys2_root, msys64_root = pathlib.Path('C:/MSYS2'), pathlib.Path('C:/MSYS64')
    #msys
    if not (msys2_root / 'msys2_shell.cmd').exists() and not (msys64_root / 'msys2_shell.cmd').exists():
      print('Installing msys2')
      download('https://github.com/msys2/msys2-installer/releases/download/2023-07-18/msys2-base-x86_64-20230718.sfx.exe')
      subprocess.check_call(['msys2-base-x86_64-20230718.sfx.exe', '-y', '-oC:\\'])

    msys2_root = msys2_root if msys2_root.exists() else msys64_root

    print('make exists before:', (msys2_root / 'usr/bin/make.exe').exists())
    if not (msys2_root / 'usr/bin/make.exe').exists():
      subprocess.check_call([str(msys2_root / 'msys2_shell.cmd'), '-here', '-use-full-path', '-no-start', '-defterm', '-c', 'pacman --noconfirm -S make diffutils'])
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
    new_path = os.environ['PATH']

    if not shutil.which('brew'):
      subprocess.check_call(['curl', '-L', '--output', 'brew_install.sh', 'https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh'])
      subprocess.check_call(['bash', 'brew_install.sh'])

    subprocess.check_call(['brew', 'install', 'autoconf', 'automake', 'libtool', 'pcre2'])

    #nasm
    if not shutil.which('nasm'):
      subprocess.check_call(['curl', '-L', '-o', 'nasm-2.15.05-macosx.zip', 'https://www.nasm.us/pub/nasm/releasebuilds/2.15.05/macosx/nasm-2.15.05-macosx.zip'])
      subprocess.check_call(['unzip', '-d', os.environ['HOME'], 'nasm-2.15.05-macosx.zip'])
      with open(str(home_path / '.thalamusrc'), 'a') as bashrc:
        bashrc.write(f'\nexport PATH={home_str}/nasm-2.15.05:$PATH\n')

    #cmake
    if not shutil.which('cmake'):
      subprocess.check_call(['curl', '-L', '-o', 'cmake-3.24.0-macos-universal.tar.gz', 'https://github.com/Kitware/CMake/releases/download/v3.24.0/cmake-3.24.0-macos-universal.tar.gz'])
      subprocess.check_call(['tar', '-xvzf', 'cmake-3.24.0-macos-universal.tar.gz', '-C', os.environ['HOME']])
      with open(str(home_path / '.thalamusrc'), 'a') as bashrc:
        bashrc.write(f'\nexport PATH={home_str}/cmake-3.24.0-macos-universal/CMake.app/Contents/bin:$PATH\n')

    subprocess.check_call([sys.executable, '-m', 'pip', 'install', '-U', 'setuptools', 'ninja'], cwd=home_str)

    with open(str(home_path / '.bash_profile'), 'r') as bash_profile:
      bash_profile_content = bash_profile.read()
    if "source ~/.thalamusrc" not in bash_profile_content:
      with open(str(home_path / '.bash_profile'), 'a') as bash_profile:
        bash_profile.write(f'\nsource ~/.thalamusrc\n')


  else:
    (home_path / '.thalamusrc').touch()
    subprocess.check_call(['sudo', 'apt', 'install', '-y', 
                          'python3-pip', 'git', 'wget', 'sudo', 'curl', 'ninja-build', 'lsb-release',
                          'libsm-dev', 'libice-dev', 'libudev-dev', 'libdbus-1-dev', 'libzstd-dev',
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
                          'liblzma-dev', 'libtool', 'flex',
                          'nasm', 'libpcre2-dev', 'bison',
                          'libbrotli-dev', 'autotools-dev', 'automake',
                          'swig', 'debconf-utils', 'libusb-1.0-0', 'ffmpeg'])
    subprocess.check_call(['python3', '-m', 'pip', 'install', '-U', 'setuptools'], cwd=home_str)
    subprocess.check_call([sys.executable, '-m', 'pip', 'install', '-r', str(pathlib.Path.cwd()/'requirements.txt')], cwd=home_str)
                          
    _, clang_is_current = is_up_to_date('clang++', r'clang version (\d+).(\d+).(\d+)', (10, 0, 0))
    if not clang_is_current:
      subprocess.check_call(['sudo', 'apt', 'install', '-y', 'clang'])
    clang_version, _ = is_up_to_date('clang++', r'clang version (\d+).(\d+).(\d+)', (10, 0, 0))
    subprocess.check_call(['sudo', 'apt', 'install', '-y', f'libclang-{clang_version[0]}-dev', ])

    _, cmake_is_current = is_up_to_date('cmake', r'cmake version (\d+).(\d+).(\d+)', (3, 16, 0))
    if not cmake_is_current:
      cmake_version = '3.23.1'
      subprocess.check_call(['wget', f'https://github.com/Kitware/CMake/releases/download/v{cmake_version}/cmake-{cmake_version}-linux-x86_64.sh'])
      (home_path / '.local').mkdir(exist_ok=True)
      subprocess.check_call(['sh', f'./cmake-{cmake_version}-linux-x86_64.sh', f'--prefix={home_str}/.local', '--skip-license', '--include-subdir'])
      with open(str(home_path / '.thalamusrc'), 'a') as bashrc:
        bashrc.write(f'\nexport PATH={home_str}/.local/cmake-{cmake_version}-linux-x86_64/bin:$PATH\n')
    
    _, meson_is_current = is_up_to_date('meson', r'(\d+).(\d+).(\d+)', (0, 55, 0))
    if not meson_is_current:
      subprocess.check_call(['python3', '-m', 'pip', 'install', 'meson'], cwd=home_str)
    with open(str(home_path / '.bashrc'), 'r') as bashrc:
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
