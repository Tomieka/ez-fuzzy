cmake_minimum_required(VERSION 3.10)
project(ez-fuzzy LANGUAGES CXX VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include GNUInstallDirs for standard installation paths
include(GNUInstallDirs)

# Find Qt package
find_package(Qt5 COMPONENTS Core Gui Widgets Concurrent REQUIRED)

# Set up automatic handling of Qt resources, UI files, etc.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Define source files
set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/fuzzymatcher.cpp
)

# Define header files
set(HEADERS
    src/mainwindow.h
    src/fuzzymatcher.h
)

# Define UI files
set(UI_FILES
    src/mainwindow.ui
)

# Define resource files
set(RESOURCE_FILES
    resources.qrc
)

# Create the executable
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${UI_FILES} ${RESOURCE_FILES})

# Link Qt libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Concurrent
)

# Installation rules
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install desktop file
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/ez-fuzzy.desktop
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
)

# Install documentation
install(FILES 
    ${CMAKE_CURRENT_SOURCE_DIR}/README.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/${PROJECT_NAME}
)

# Install license
install(FILES 
    ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/licenses/${PROJECT_NAME}
) 