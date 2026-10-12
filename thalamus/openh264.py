"""Cisco's OpenH264 binary, which thalamus-contrib's MEDIA_CONVERTER uses to
encode H.264.

Cisco's binary is covered by Cisco's H.264 patent license only if it's
downloaded separately to the user's machine and the user can turn its use off
(see LICENSE_TEXT), so it isn't shipped with Thalamus: the pipeline and task
controller download it into ~/.thalamus at startup. thalamus-contrib's FFmpeg is
built against this version's headers and uses the binary whenever it's there.

Users opt out in Preferences > H264, which creates ~/.thalamus/.no_openh264.
While that file exists the binary is deleted at startup, before Thalamus loads
it, and isn't downloaded. Opting back in deletes the file and downloads the
binary.
"""
from __future__ import annotations

import bz2
import typing
import hashlib
import logging
import pathlib
import platform
import threading
import time
import urllib.request

LOGGER = logging.getLogger(__name__)

VERSION = '2.6.0'

# Cisco only serves its binaries over HTTP, so each download is checked against
# the SHA-256 of the file Cisco published (whose MD5s matched Cisco's
# .signed.md5.txt files when these were recorded).
BASE_URL = 'http://ciscobinary.openh264.org/'

# (platform.system(), platform.machine()) -> (library file name, SHA-256 of
# its .bz2 download)
BINARIES = {
  ('Windows', 'AMD64'): ('openh264-2.6.0-win64.dll', 'dab5f2a872777f9a58b69bfa9fbcf20d9f82f2d6ec91383fd70bff49bd34ac9f'),
  ('Linux', 'x86_64'): ('libopenh264-2.6.0-linux64.8.so', '27ab53323c110b76214c1c72222f459d17febbcd1e252136cadc292b0308d75b'),
  ('Linux', 'aarch64'): ('libopenh264-2.6.0-linux-arm64.8.so', 'a78aea7970150f46bcd3bb7994c9e6dd90bd7a9ea785920f5a73f6964e3fcda7'),
  ('Darwin', 'arm64'): ('libopenh264-2.6.0-mac-arm64.dylib', '6db362ee5abdab572311aeadb96d3f44b0617d9a4a4b9f4db4cb5ac4d968da71'),
  ('Darwin', 'x86_64'): ('libopenh264-2.6.0-mac-x64.dylib', '38b2ed6d1d45b6a3e408c734173f2d67ab44a10d0e154ff3489b89877cd60e7e'),
}

# Required where users control the binary's use (license condition 3).
ATTRIBUTION = 'OpenH264 Video Codec provided by Cisco Systems, Inc.'

# Cisco's BINARY_LICENSE.txt (v1.0), which has to be shown where licensing
# information is presented (license condition 4).
LICENSE_TEXT = """-------------------------------------------------------
About The Cisco-Provided Binary of OpenH264 Video Codec
-------------------------------------------------------

Cisco provides this program under the terms of the BSD license.  

Additionally, this binary is licensed under Cisco's AVC/H.264 Patent Portfolio License from MPEG LA, at no cost to you, provided that the requirements and conditions shown below in the AVC/H.264 Patent Portfolio sections are met.  

As with all AVC/H.264 codecs, you may also obtain your own patent license from MPEG LA or from the individual patent owners, or proceed at your own risk.  Your rights from Cisco under the BSD license are not affected by this choice.  

For more information on the OpenH264 binary licensing, please see the OpenH264 FAQ found at http://www.openh264.org/faq.html#binary 

A corresponding source code to this binary program is available under the same BSD terms, which can be found at http://www.openh264.org

-----------
BSD License
-----------

Copyright © 2014 Cisco Systems, Inc.

All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS” AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

-----------------------------------------
AVC/H.264 Patent Portfolio License Notice
-----------------------------------------

The binary form of this Software is distributed by Cisco under the AVC/H.264 Patent Portfolio License from MPEG LA, and is subject to the following requirements, which may or may not be applicable to your use of this software: 

THIS PRODUCT IS LICENSED UNDER THE AVC PATENT PORTFOLIO LICENSE FOR THE PERSONAL USE OF A CONSUMER OR OTHER USES IN WHICH IT DOES NOT RECEIVE REMUNERATION TO (i) ENCODE VIDEO IN COMPLIANCE WITH THE AVC STANDARD (“AVC VIDEO”) AND/OR (ii) DECODE AVC VIDEO THAT WAS ENCODED BY A CONSUMER ENGAGED IN A PERSONAL ACTIVITY AND/OR WAS OBTAINED FROM A VIDEO PROVIDER LICENSED TO PROVIDE AVC VIDEO.  NO LICENSE IS GRANTED OR SHALL BE IMPLIED FOR ANY OTHER USE.  ADDITIONAL INFORMATION MAY BE OBTAINED FROM MPEG LA, L.L.C. SEE HTTP://WWW.MPEGLA.COM

Accordingly, please be advised that content providers and broadcasters using AVC/H.264 in their service may be required to obtain a separate use license from MPEG LA, referred to as "(b) sublicenses" in the SUMMARY OF AVC/H.264 LICENSE TERMS from MPEG LA found at http://www.openh264.org/mpegla

---------------------------------------------
AVC/H.264 Patent Portfolio License Conditions
---------------------------------------------

In addition, the Cisco-provided binary of this Software is licensed under Cisco's license from MPEG LA only if the following conditions are met:

1. The Cisco-provided binary is separately downloaded to an end user's device, and not integrated into or combined with third party software prior to being downloaded to the end user’s device;

2. The end user must have the ability to control (e.g., to enable, disable, or re-enable) the use of the Cisco-provided binary;

3. Third party software, in the location where end users can control the use of the Cisco-provided binary, must display the following text:

       "OpenH264 Video Codec provided by Cisco Systems, Inc."

4.  Any third-party software that makes use of the Cisco-provided binary must reproduce all of the above text, as well as this last condition, in the EULA and/or in another location where licensing information is to be presented to the end user.  
 


                          v1.0
"""

DOWNLOAD_DIR = pathlib.Path.home() / '.thalamus'

# Exists while the user has opted out of OpenH264.
OPT_OUT_PATH = DOWNLOAD_DIR / '.no_openh264'

DOWNLOAD_ATTEMPTS = 5

_lock = threading.Lock()

def binary() -> typing.Optional[typing.Tuple[str, str]]:
  """The library file name and download hash for this platform, or None if
  Cisco publishes no binary for it."""
  return BINARIES.get((platform.system(), platform.machine()))

def library_path() -> typing.Optional[pathlib.Path]:
  found = binary()
  return DOWNLOAD_DIR / found[0] if found else None

def is_opted_out() -> bool:
  return OPT_OUT_PATH.exists()

def delete_library() -> None:
  """Deletes the downloaded binary, if any."""
  path = library_path()
  if path is None:
    return
  try:
    path.unlink(missing_ok=True)
  except OSError:
    LOGGER.warning('Couldn\'t delete %s', path, exc_info=True)

def opt_out() -> None:
  """Marks the opt out. The binary is deleted at the next startup; until
  then a Thalamus that's running keeps using it."""
  DOWNLOAD_DIR.mkdir(parents=True, exist_ok=True)
  OPT_OUT_PATH.touch()

def opt_in() -> None:
  """Reverses opt_out and starts downloading the binary."""
  OPT_OUT_PATH.unlink(missing_ok=True)
  download_in_background()

def download() -> typing.Optional[pathlib.Path]:
  """Downloads this platform's binary into ~/.thalamus unless it's already
  there. Returns its path, or None if Cisco publishes no binary for this
  platform. Raises if the download fails or doesn't match its hash."""
  found = binary()
  if found is None:
    return None
  name, sha256 = found
  path = DOWNLOAD_DIR / name
  with _lock:
    if path.exists():
      return path
    url = BASE_URL + name + '.bz2'
    # Cisco's server drops connections now and then.
    for attempt in range(DOWNLOAD_ATTEMPTS):
      LOGGER.info('Downloading %s', url)
      try:
        with urllib.request.urlopen(url, timeout=60) as response:
          compressed = response.read()
        break
      except OSError as error:
        if attempt + 1 == DOWNLOAD_ATTEMPTS:
          raise
        LOGGER.warning('Downloading %s failed (%s), retrying', url, error)
        time.sleep(attempt + 1)
    actual = hashlib.sha256(compressed).hexdigest()
    if actual != sha256:
      raise RuntimeError(f'{url} has SHA-256 {actual}, expected {sha256}')
    DOWNLOAD_DIR.mkdir(parents=True, exist_ok=True)
    partial = path.with_name(path.name + '.part')
    partial.write_bytes(bz2.decompress(compressed))
    partial.replace(path)
    LOGGER.info('Downloaded %s', path)
    return path

def _download_logged() -> None:
  try:
    download()
  except Exception:
    LOGGER.exception('Downloading OpenH264 failed; H.264 encoding is unavailable')

def download_in_background() -> None:
  """Starts downloading the binary, unless the user opted out, it's already
  downloaded or there's none for this platform. Doesn't wait for it. If the
  user opted out, deletes the binary instead; this runs at startup, before
  Thalamus loads it."""
  if is_opted_out():
    delete_library()
    return
  path = library_path()
  if path is None or path.exists():
    return
  threading.Thread(target=_download_logged, name='openh264-download', daemon=True).start()
