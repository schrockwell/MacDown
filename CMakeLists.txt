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
