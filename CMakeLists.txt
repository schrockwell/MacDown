cmake_minimum_required(VERSION 3.9)
project(MdEdit C)

add_application(MdEdit
    src/main.c
    src/document.c
    src/file_io.c
    src/markdown.c
    src/apple_events.c
    resources/MdEdit.r
    CREATOR MDED
)

# Retro68's add_application macro hardcodes the .APPL extension on the
# data-fork output. Mac apps don't have extensions, so rename it to
# plain "MdEdit". The %MdEdit.ad AppleDouble sidecar already pairs by
# basename, so the pairing stays intact. The empty MdEdit.ad artifact
# is also discarded.
add_custom_command(TARGET MdEdit_APPL POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rename
        ${CMAKE_BINARY_DIR}/MdEdit.APPL ${CMAKE_BINARY_DIR}/MdEdit
    COMMAND ${CMAKE_COMMAND} -E remove -f
        ${CMAKE_BINARY_DIR}/MdEdit.ad
    COMMENT "Dropping .APPL extension"
    VERBATIM
)

# Retro68 leaves Finder flags at 0; without 'has bundle', real System 6
# Finders won't pick up our BNDL/ICN# resources during a desktop rebuild
# and the app shows the generic APPL icon. Patch the bit onto the
# MacBinary file (and the AppleDouble sidecar) after each link.
add_custom_command(TARGET MdEdit_APPL POST_BUILD
    COMMAND python3
        ${CMAKE_SOURCE_DIR}/scripts/set-bundle-bit.py
        ${CMAKE_BINARY_DIR}/MdEdit.bin
        ${CMAKE_BINARY_DIR}/%MdEdit.ad
    COMMENT "Setting 'has bundle' Finder bit"
    VERBATIM
)

# Tag MdEdit.bin itself as a MacBinary document so the receiving Mac's
# Finder hands it off to StuffIt / MacBinary II+ instead of treating it
# as an unknown file. The codes propagate over any HFS-aware copy
# (shared folder, AppleTalk, hfsutils). Skipped silently on non-macOS
# build hosts where SetFile isn't present.
find_program(SETFILE_EXECUTABLE SetFile)
if(SETFILE_EXECUTABLE)
    # creator=???? is the canonical "any registered handler" code. The
    # Finder will route a double-click to whatever app is registered
    # for type=BINA (StuffIt Expander, StuffIt Deluxe, MacBinary II+).
    # Avoid specific creators like 'mBIN' — they cause "can't find the
    # application" on Macs that don't happen to have that one installed.
    add_custom_command(TARGET MdEdit_APPL POST_BUILD
        COMMAND ${SETFILE_EXECUTABLE} -t BINA -c "????" ${CMAKE_BINARY_DIR}/MdEdit.bin
        COMMENT "Tagging MdEdit.bin as type=BINA creator=????"
        VERBATIM
    )
endif()

# Real-Mac disk mounters (MountImage, Disk Copy 4.2, ShrinkWrap) expect
# the Disk Copy 4.2 header + type=dCp4 / creator=dCpy. Mini vMac and
# Basilisk II accept the raw .dsk, so keep that one too; this just adds
# a second image alongside it for transfer to System 6 hardware.
add_custom_command(TARGET MdEdit_APPL POST_BUILD
    COMMAND python3
        ${CMAKE_SOURCE_DIR}/scripts/raw-to-dc42.py
        ${CMAKE_BINARY_DIR}/MdEdit.dsk
        ${CMAKE_BINARY_DIR}/MdEdit.img
        MdEdit
    COMMENT "Wrapping .dsk as Disk Copy 4.2 (.img) for real hardware"
    VERBATIM
)
if(SETFILE_EXECUTABLE)
    add_custom_command(TARGET MdEdit_APPL POST_BUILD
        COMMAND ${SETFILE_EXECUTABLE} -t dCp4 -c dCpy ${CMAKE_BINARY_DIR}/MdEdit.img
        COMMENT "Tagging MdEdit.img as type=dCp4 creator=dCpy"
        VERBATIM
    )
endif()

# BinHex 4.0 (.hqx): text-safe, self-describing, universally accepted
# by StuffIt Expander on classic Mac. Survives transport that strips
# type/creator or even resource forks entirely.
find_program(BINHEX_EXECUTABLE binhex)
find_program(MACBINARY_EXECUTABLE macbinary)
if(BINHEX_EXECUTABLE AND MACBINARY_EXECUTABLE)
    add_custom_command(TARGET MdEdit_APPL POST_BUILD
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/make-hqx.sh
            ${CMAKE_BINARY_DIR}/MdEdit.bin
            ${CMAKE_BINARY_DIR}/MdEdit.hqx
        COMMENT "Encoding MdEdit.hqx (BinHex 4.0)"
        VERBATIM
    )
endif()
