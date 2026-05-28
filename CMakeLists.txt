cmake_minimum_required(VERSION 3.9)
project(MacDown C)

# Single source of truth for the version number is /VERSION at the
# project root. We parse it into MAJOR/MINOR/PATCH so we can also bake
# the BCD bytes the 'vers' resource needs, then configure_file two
# artifacts:
#   - ${CMAKE_BINARY_DIR}/version.r — included by resources/MacDown.r
#   - MACDOWN_VERSION_STR compile-time #define — used by main.c
# Bump the VERSION file and rebuild; everything else follows.
file(READ "${CMAKE_SOURCE_DIR}/VERSION" _MACDOWN_VERSION_RAW)
string(STRIP "${_MACDOWN_VERSION_RAW}" MACDOWN_VERSION)
if(NOT MACDOWN_VERSION MATCHES "^([0-9])\\.([0-9])\\.([0-9])")
    message(FATAL_ERROR
        "VERSION must be MAJOR.MINOR.PATCH with single-digit components "
        "(BCD packing in 'vers' resource); got: '${MACDOWN_VERSION}'")
endif()
set(MACDOWN_VER_MAJOR ${CMAKE_MATCH_1})
set(MACDOWN_VER_MINOR ${CMAKE_MATCH_2})
set(MACDOWN_VER_PATCH ${CMAKE_MATCH_3})
configure_file(
    ${CMAKE_SOURCE_DIR}/resources/version.r.in
    ${CMAKE_BINARY_DIR}/version.r
    @ONLY)
list(APPEND REZ_INCLUDE_PATH ${CMAKE_BINARY_DIR})
add_compile_definitions(MACDOWN_VERSION_STR="${MACDOWN_VERSION}")
message(STATUS "MacDown version: ${MACDOWN_VERSION}")

# Regenerate src/toolbar_icons.h whenever an assets/icons PNG changes.
# The header holds 15x15 1-bit QuickDraw bitmaps for the browser
# toolbar buttons; browser.c #includes it directly.
set(_TOOLBAR_PNGS
    ${CMAKE_SOURCE_DIR}/assets/icons/folder-up-15px.png
    ${CMAKE_SOURCE_DIR}/assets/icons/folder-open-15px.png
    ${CMAKE_SOURCE_DIR}/assets/icons/folder-new-15px.png
    ${CMAKE_SOURCE_DIR}/assets/icons/trash-15px.png)
add_custom_command(
    OUTPUT ${CMAKE_SOURCE_DIR}/src/toolbar_icons.h
    COMMAND python3
        ${CMAKE_SOURCE_DIR}/scripts/png-to-toolbar-icons.py
        ${CMAKE_SOURCE_DIR}/src/toolbar_icons.h
        ${_TOOLBAR_PNGS}
    DEPENDS
        ${CMAKE_SOURCE_DIR}/scripts/png-to-toolbar-icons.py
        ${_TOOLBAR_PNGS}
    COMMENT "Generating src/toolbar_icons.h from assets/icons/*.png"
    VERBATIM
)
add_custom_target(toolbar_icons DEPENDS ${CMAKE_SOURCE_DIR}/src/toolbar_icons.h)

add_application(MacDown
    src/main.c
    src/document.c
    src/file_io.c
    src/browser.c
    src/markdown.c
    src/pstr.c
    src/apple_events.c
    resources/MacDown.r
    CREATOR MDED
)
add_dependencies(MacDown toolbar_icons)

# Retro68's add_application macro hardcodes the .APPL extension on the
# data-fork output. Mac apps don't have extensions, so rename it to
# plain "MacDown". The %MacDown.ad AppleDouble sidecar already pairs by
# basename, so the pairing stays intact. The empty MacDown.ad artifact
# is also discarded.
add_custom_command(TARGET MacDown_APPL POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rename
        ${CMAKE_BINARY_DIR}/MacDown.APPL ${CMAKE_BINARY_DIR}/MacDown
    COMMAND ${CMAKE_COMMAND} -E remove -f
        ${CMAKE_BINARY_DIR}/MacDown.ad
    COMMENT "Dropping .APPL extension"
    VERBATIM
)

# Retro68 leaves Finder flags at 0; without 'has bundle', real System 6
# Finders won't pick up our BNDL/ICN# resources during a desktop rebuild
# and the app shows the generic APPL icon. We do NOT patch this into
# the .bin -- decoders (Apple's macbinary, Basilisk II's ExtFS) strip
# byte 73 entirely, so the bit never reaches the decoded file, and
# rewriting byte 73 invalidates the CRC and breaks the file in those
# decoders. Patch the AppleDouble sidecar instead; that side travels
# via host-side metadata and Basilisk II's ExtFS does propagate it.
# The MacDown data fork gets the bit set via SetFile -a B below.
add_custom_command(TARGET MacDown_APPL POST_BUILD
    COMMAND python3
        ${CMAKE_SOURCE_DIR}/scripts/set-bundle-bit.py
        ${CMAKE_BINARY_DIR}/%MacDown.ad
    COMMENT "Setting 'has bundle' Finder bit on AppleDouble sidecar"
    VERBATIM
)

# Tag MacDown.bin itself as a MacBinary document so the receiving Mac's
# Finder hands it off to StuffIt / MacBinary II+ instead of treating it
# as an unknown file. The codes propagate over any HFS-aware copy
# (shared folder, AppleTalk, hfsutils). Skipped silently on non-macOS
# build hosts where SetFile isn't present.
find_program(SETFILE_EXECUTABLE SetFile)
if(SETFILE_EXECUTABLE)
    # Set type=APPL creator=MDED + bundle bit on the renamed APPL data
    # fork (`MacDown`). When Basilisk II's ExtFS exposes the host
    # folder, it reads these xattrs and presents the file as a proper
    # Mac app with the right Finder info. The .bin itself is left as
    # bare Retro68 output so ExtFS can unpack its MacBinary container
    # cleanly -- adding host-side type/creator there confuses ExtFS's
    # MacBinary detection. (For StuffIt on real System 6, set the
    # .bin's type to 'BINA' from the Mac side via ResEdit.)
    add_custom_command(TARGET MacDown_APPL POST_BUILD
        COMMAND ${SETFILE_EXECUTABLE} -t APPL -c MDED -a B ${CMAKE_BINARY_DIR}/MacDown
        COMMENT "Setting type/creator/bundle bit on MacDown"
        VERBATIM
    )
endif()

# Real-Mac disk mounters (MountImage, Disk Copy 4.2, ShrinkWrap) expect
# the Disk Copy 4.2 header + type=dCp4 / creator=dCpy. Mini vMac and
# Basilisk II accept the raw .dsk, so keep that one too; this just adds
# a second image alongside it for transfer to System 6 hardware.
add_custom_command(TARGET MacDown_APPL POST_BUILD
    COMMAND python3
        ${CMAKE_SOURCE_DIR}/scripts/raw-to-dc42.py
        ${CMAKE_BINARY_DIR}/MacDown.dsk
        ${CMAKE_BINARY_DIR}/MacDown.img
        MacDown
    COMMENT "Wrapping .dsk as Disk Copy 4.2 (.img) for real hardware"
    VERBATIM
)
if(SETFILE_EXECUTABLE)
    add_custom_command(TARGET MacDown_APPL POST_BUILD
        COMMAND ${SETFILE_EXECUTABLE} -t dCp4 -c dCpy ${CMAKE_BINARY_DIR}/MacDown.img
        COMMENT "Tagging MacDown.img as type=dCp4 creator=dCpy"
        VERBATIM
    )
endif()

# BinHex 4.0 (.hqx): text-safe, self-describing, universally accepted
# by StuffIt Expander on classic Mac. Survives transport that strips
# type/creator or even resource forks entirely.
find_program(BINHEX_EXECUTABLE binhex)
find_program(MACBINARY_EXECUTABLE macbinary)
if(BINHEX_EXECUTABLE AND MACBINARY_EXECUTABLE)
    add_custom_command(TARGET MacDown_APPL POST_BUILD
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/make-hqx.sh
            ${CMAKE_BINARY_DIR}/MacDown.bin
            ${CMAKE_BINARY_DIR}/MacDown.hqx
        COMMENT "Encoding MacDown.hqx (BinHex 4.0)"
        VERBATIM
    )
endif()
