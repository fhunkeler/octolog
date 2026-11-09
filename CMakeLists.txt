cmake_minimum_required(VERSION 3.21)
project(OctoLog VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Sql Svg CorePrivate)

qt_add_executable(OctoLog WIN32
    src/main.cpp
    src/schema.h src/schema.cpp
    src/database.h src/database.cpp
    src/xlsx.h src/xlsx.cpp
    src/importer.h src/importer.cpp
    src/exporter.h src/exporter.cpp
    src/searchproxy.h src/searchproxy.cpp
    src/searchabletable.h src/searchabletable.cpp
    src/tablefill.h src/tablefill.cpp
    src/recorddialog.h src/recorddialog.cpp
    src/loandialogs.h src/loandialogs.cpp
    src/ui.h src/ui.cpp
    src/admin.h src/admin.cpp
    src/maintenance.h src/maintenance.cpp
    src/deposit.h src/deposit.cpp
    src/depositdialogs.h src/depositdialogs.cpp
    src/depositstab.h src/depositstab.cpp
    src/maintenancedialogs.h src/maintenancedialogs.cpp
    src/maintenancetab.h src/maintenancetab.cpp
    src/fichedialog.h src/fichedialog.cpp
    src/entitytab.h src/entitytab.cpp
    src/charts.h src/charts.cpp
    src/hometab.h src/hometab.cpp
    src/loanstab.h src/loanstab.cpp
    src/mainwindow.h src/mainwindow.cpp
    resources/resources.qrc
)

target_compile_definitions(OctoLog PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")
target_link_libraries(OctoLog PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Sql Qt6::Svg Qt6::CorePrivate)

if(WIN32)
    install(TARGETS OctoLog RUNTIME DESTINATION .)
else()
    include(GNUInstallDirs)
    install(TARGETS OctoLog RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    configure_file(resources/octolog.desktop.in octolog.desktop @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/octolog.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES resources/octolog.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()

option(OCTOLOG_TESTS "Compiler les tests" ON)
if(OCTOLOG_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(tst_octolog
        tests/tst_octolog.cpp
        src/schema.cpp src/database.cpp src/searchproxy.cpp src/searchabletable.cpp
        src/importer.cpp src/exporter.cpp src/xlsx.cpp src/ui.cpp src/maintenance.cpp src/deposit.cpp src/charts.cpp src/admin.cpp)
    target_include_directories(tst_octolog PRIVATE src)
    target_link_libraries(tst_octolog PRIVATE Qt6::Widgets Qt6::Sql Qt6::Test Qt6::CorePrivate)
    add_test(NAME tst_octolog COMMAND tst_octolog)
    set_tests_properties(tst_octolog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
