cmake_minimum_required(VERSION 3.19)
project(WsprMap VERSION 0.3.0 LANGUAGES CXX)   # WSPR Scheduler Map 3 (C# WSPR Scheduler Map 2 was 0.2.9)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Network)

# Non-window part (locators, paths, night, the databases, wspr.live, DBmapcredential) - shared by the
# program and the tests
add_library(mapcore STATIC
    mapcore.h mapcore.cpp
    credentials.h credentials.cpp
)
target_link_libraries(mapcore PUBLIC Qt6::Core Qt6::Sql Qt6::Network)
target_include_directories(mapcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set(WSPRMAP_SOURCES mainwindow.h mainwindow.cpp mainwindow.ui mapwidget.h mapwidget.cpp msg.h msg.cpp
    loaderror.h loaderror.cpp wsprmap.qrc)

if(WIN32)   # icon and version details for WsprMap.exe (Explorer, taskbar, installer)
    set(RC_ICON "${CMAKE_CURRENT_SOURCE_DIR}/wsprmap.ico")
    set(RC_NAME "WSPR Scheduler Map 3")
    set(RC_EXE WsprMap)
    configure_file(wsprmap.rc.in "${CMAKE_CURRENT_BINARY_DIR}/wsprmap.rc" @ONLY)
    set(WSPRMAP_RC "${CMAKE_CURRENT_BINARY_DIR}/wsprmap.rc")
endif()

add_executable(WsprMap WIN32 main.cpp ${WSPRMAP_SOURCES} ${WSPRMAP_RC})
target_link_libraries(WsprMap PRIVATE mapcore Qt6::Widgets)
target_compile_definitions(WsprMap PRIVATE WSPRMAP_VERSION="${PROJECT_VERSION}")

# Windows: copy the Qt DLLs and plugins next to the program after each build (windeployqt).
# The MySQL driver (sqldrivers/qsqlmysql.dll) and libmysql.dll are not part of Qt - they're copied from installer/thirdparty.
# Map tiles come over https: windeployqt adds Qt's TLS plugin (tls/qschannelbackend.dll) for that.
if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT6_INSTALL_PREFIX}/bin" "${Qt6_DIR}/../../../bin")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET WsprMap POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations --verbose 0 "$<TARGET_FILE:WsprMap>"
            COMMENT "windeployqt WsprMap")
    else()
        message(WARNING "windeployqt not found - run the program from Qt Creator or add the Qt bin folder to PATH")
    endif()
    # The MySQL driver and its DLLs, from installer/thirdparty (the same files the installer uses), so a
    # new build folder works straight away.
    set(_mysql_dir "${CMAKE_CURRENT_SOURCE_DIR}/installer/thirdparty")
    if(EXISTS "${_mysql_dir}/sqldrivers/qsqlmysql.dll" AND EXISTS "${_mysql_dir}/libmysql.dll")
        file(GLOB _mysql_dlls "${_mysql_dir}/*.dll")
        foreach(_t WsprMap)
            add_custom_command(TARGET ${_t} POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${_t}>/sqldrivers"
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_mysql_dir}/sqldrivers/qsqlmysql.dll" "$<TARGET_FILE_DIR:${_t}>/sqldrivers/"
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${_mysql_dlls} "$<TARGET_FILE_DIR:${_t}>/"
                COMMENT "MySQL driver for ${_t}")
        endforeach()
    else()
        message(WARNING "installer/thirdparty/sqldrivers/qsqlmysql.dll not found - copy the MySQL driver "
                        "(sqldrivers/qsqlmysql.dll, libmysql.dll ...) next to the program yourself")
    endif()
endif()

# Linux: "cmake --install" (linux/install.sh does it): the program, a menu entry and its icon
if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS WsprMap RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES linux/wspr-scheduler-map-3.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES linux/wspr-scheduler-map-3.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
endif()

option(WSPRMAP_TESTS "Build the tests (the database ones need a scratch MySQL, never the real one)" OFF)
if(WSPRMAP_TESTS)
    add_executable(map_test tests/map_test.cpp)   # core: locators, paths, night, lists; fake wspr.live; scratch MySQL
    target_link_libraries(map_test PRIVATE mapcore)
    add_executable(mapwindow_uitest tests/mapwindow_uitest.cpp ${WSPRMAP_SOURCES})   # the window; fake tiles; offscreen
    find_package(Qt6 REQUIRED COMPONENTS Test)
    set_target_properties(mapwindow_uitest PROPERTIES AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(mapwindow_uitest PRIVATE mapcore Qt6::Widgets Qt6::Test)
    target_compile_definitions(mapwindow_uitest PRIVATE WSPRMAP_VERSION="${PROJECT_VERSION}")
endif()
