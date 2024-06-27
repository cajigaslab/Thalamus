FROM mcr.microsoft.com/windows/servercore:ltsc2022

ARG DEBIAN_FRONTEND=noninteractive
#ARG USER_GROUP
#ARG USER_NAME
#ARG USER_ID
#
#RUN groupadd -g $USER_GROUP $USER_NAME
#RUN adduser $USER_NAME --uid $USER_ID --gid $USER_GROUP

#COPY .ssh /root/.ssh
#COPY .ssh /home/$USER_NAME/.ssh

#RUN pwsh -Command "Set-ExecutionPolicy Bypass -Scope Process -Force; [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072; iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))"
#RUN choco install -y msys2 --params "/InstallDir:C:\msys2" && del /f /s /q "C:\$Recycle.Bin"

SHELL ["powershell", "-Command", "$ErrorActionPreference = 'Stop'; $ProgressPreference = 'SilentlyContinue';"]

RUN Invoke-WebRequest 'https://github.com/git-for-windows/git/releases/download/v2.42.0.windows.2/MinGit-2.42.0.2-64-bit.zip' -OutFile MinGit.zip
RUN Expand-Archive c:\MinGit.zip -DestinationPath c:\MinGit; \
  $env:PATH = $env:PATH + ';C:\MinGit\cmd\;C:\MinGit\cmd'; \
  Set-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment\' -Name Path -Value $env:PATH

RUN [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; \
  Invoke-WebRequest -UseBasicParsing -uri "https://github.com/msys2/msys2-installer/releases/download/2023-07-18/msys2-base-x86_64-20230718.sfx.exe" -OutFile msys2.exe; \
  .\msys2.exe -y -oC:\; \
  Remove-Item msys2.exe ; \
  function msys() { C:\msys64\usr\bin\bash.exe @('-lc') + @Args; } \
  msys ' '; \
  msys 'pacman --noconfirm -Syuu'; \
  msys 'pacman --noconfirm -Syuu'; \
  msys 'pacman --noconfirm -Scc'; \
  Remove-Item -Recurse -Force  'C:\$Recycle.Bin'

RUN Set-ExecutionPolicy Bypass -Scope Process -Force; \
  [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072; \
  iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))

RUN choco install -y python310
RUN pip install virtualenv

RUN python -m virtualenv -p C:\Python310\python.exe py310

RUN choco install -y curl

RUN C:\ProgramData\chocolatey\bin\curl https://aka.ms/vs/17/release/vs_buildtools.exe -o vs_buildtools.exe -L
RUN Start-Process -Wait -PassThru -FilePath vs_buildtools.exe -ArgumentList --quiet,--wait,--norestart,--add,Microsoft.VisualStudio.Workload.VCTools,--includeRecommended
#RUN dir "C:\Program Files\Microsoft Visual Studio\2022\Community"

COPY setup.py setup.py
COPY prepare.py prepare.py
COPY dev-requirements.txt dev-requirements.txt
COPY requirements.txt requirements.txt

RUN C:\py310\Scripts\activate.ps1; \
  python setup.py prepare; \
  Remove-Item -Recurse -Force 'C:\$Recycle.Bin' -ErrorAction 'SilentlyContinue'; \
  echo DONE

RUN git config --global user.email jenkins@email.com
RUN git config --global user.name jenkins
RUN git config --global core.longpaths true

ENV MSYS=nonativeinnerlinks

RUN pip install twine
