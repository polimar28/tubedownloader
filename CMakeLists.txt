cmake_minimum_required(VERSION 3.21)
project(TubeDownloader VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
qt_standard_project_setup()

# Shared core (no GUI) used by the app and the self-test.
add_library(tdl_core STATIC
    src/Downloader.cpp src/Downloader.h
    src/FileUtils.cpp src/FileUtils.h
)
target_include_directories(tdl_core PUBLIC src)
target_link_libraries(tdl_core PUBLIC Qt6::Core)

# Windows resources (icon/version). Built separately without include directories,
# because windres cannot handle include paths containing spaces.
add_library(tdl_rc OBJECT resources/app.rc)
set_target_properties(tdl_rc PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

qt_add_executable(TubeDownloader WIN32
    src/main.cpp
    src/MainWindow.cpp src/MainWindow.h
    $<TARGET_OBJECTS:tdl_rc>
)
target_link_libraries(TubeDownloader PRIVATE tdl_core Qt6::Widgets)

if(MINGW)
    # Avoid depending on extra MinGW runtime DLLs where possible.
    target_link_options(TubeDownloader PRIVATE -static-libgcc -static-libstdc++)
endif()

# Copy tools/ next to the executable so it runs straight from the build folder.
add_custom_command(TARGET TubeDownloader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/tools $<TARGET_FILE_DIR:TubeDownloader>/tools
    COMMENT "Copying tools/ next to TubeDownloader.exe"
)

# Console self-test (not shipped).
qt_add_executable(tdl_selftest tests/selftest.cpp)
set_target_properties(tdl_selftest PROPERTIES WIN32_EXECUTABLE OFF)
target_link_libraries(tdl_selftest PRIVATE tdl_core)
