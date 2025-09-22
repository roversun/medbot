cmake_minimum_required(VERSION 3.16)

project(latcheck VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)

find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2 Network Positioning)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(applatcheck
    main.cpp
    networkmanager.cpp
    networkmanager.h
    configmanager.cpp
    configmanager.h
    latencychecker.cpp
    latencychecker.h
    logger.cpp
    logger.h
    locationservice.cpp
    locationservice.h
    message_protocol.cpp
)

qt_add_qml_module(applatcheck
    URI latcheck
    VERSION 1.0
    QML_FILES
        Main.qml
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(applatcheck PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.applatcheck
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(applatcheck
    PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Network Qt6::Positioning
)

# Windows specific libraries for ICMP and crypto
if(WIN32)
    target_link_libraries(applatcheck PRIVATE crypt32 advapi32 iphlpapi)
endif()

include(GNUInstallDirs)
install(TARGETS applatcheck
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
