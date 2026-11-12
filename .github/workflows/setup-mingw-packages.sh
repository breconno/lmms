#!/bin/sh

# TODO: Stop using this PPA once all MinGW dependencies are available through vcpkg and work with CMake.

# Only these packages from tobydox's PPA are currently used:
#   libgig-mingw-w64    [vcpkg does not support MinGW yet]
#   qt5base-mingw-w64   [vcpkg takes too much memory to build this]
#   qt5svg-mingw-w64

# The rest of the MinGW dependencies come from vcpkg.
# suil, once added, would need to be built with Qt, due to its dependency on it.

keyring=/etc/apt/keyrings/tobydox-mingw-w64.gpg
install -d -m 0755 "$(dirname "$keyring")"
gpg --batch --yes --no-default-keyring --keyring "$keyring" \
  --keyserver hkps://keyserver.ubuntu.com \
  --recv-keys 9C0C72C11E0AF891 02FE5F12ADDE29B2
sh -c "echo 'deb [signed-by=$keyring] http://ppa.launchpad.net/tobydox/mingw-w64/ubuntu focal main' > \
  /etc/apt/sources.list.d/tobydox-mingw-w64.list"
apt-get update -y
