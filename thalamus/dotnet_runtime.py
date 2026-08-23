"""
Detects whether the .NET runtimes required to run thalamus/dotnet/dotnet.exe
(an ASP.NET Core / gRPC app, see dotnet/dotnet/dotnet.csproj) are installed.
"""

import os
import glob

REQUIRED_FRAMEWORKS = (
  ('Microsoft.NETCore.App', 8),
  ('Microsoft.AspNetCore.App', 8),
)

def is_available() -> bool:
  '''
  Checks the shared-framework folders directly rather than shelling out to
  `dotnet --list-runtimes`, since dotnet.exe here is our own apphost, not the
  global CLI muxer, so the CLI may not even be on PATH.
  '''
  root = os.environ.get('DOTNET_ROOT', r'C:\Program Files\dotnet')
  return all(
    glob.glob(os.path.join(root, 'shared', name, f'{major}.*'))
    for name, major in REQUIRED_FRAMEWORKS
  )
