# Debian package (cpack -G DEB), see README "Paket bauen"
#   cmake -B build-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
#   cmake --build build-release -j
#   cd build-release && cpack -G DEB

set(AODIX_PACKAGE_MAINTAINER "Marcus Honnacker" CACHE STRING "Maintainer field of the Debian package (\"Name <mail>\")")

set(CPACK_PACKAGE_NAME "aodix")
set(CPACK_PACKAGE_VENDOR "Aodix Linux")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Tracker/sequencer hybrid with modular plugin routing (VST3, CLAP, LV2, LADSPA)")
set(CPACK_PACKAGE_DESCRIPTION "Aodix is a music sequencer by Arguru Software that combines a tracker style\nevent editor with a modular routing of plugin instances. This is the Linux\nversion: JACK audio and MIDI, VST3, CLAP, LV2 and LADSPA plugins, HiDPI\nscaling and multi-core processing. Projects stay compatible with Aodix 4.2.0.3.")
set(CPACK_PACKAGE_CONTACT "${AODIX_PACKAGE_MAINTAINER}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_STRIP_FILES ON)

set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)						# aodix_<version>_amd64.deb
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${AODIX_PACKAGE_MAINTAINER}")
set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/dubtissm/aodix-linux")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)						# libc, libstdc++, libx11, libxext, zlib from the binary
# loaded at runtime (dlopen), so not found by shlibdeps: jack (or pipewire-jack) is needed for audio, lilv for LV2
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "libjack-jackd2-0 | libjack0 | pipewire-jack, liblilv-0-0")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "zenity | kdialog, jackd2 | pipewire-jack, qjackctl")
# desktop entry, mime type and icon caches are updated by the dpkg triggers of
# desktop-file-utils, shared-mime-info and hicolor-icon-theme (no maintainer scripts needed)

include(CPack)
