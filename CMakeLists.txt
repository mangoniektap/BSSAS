# TOP CMakeLists.txt for BSSASApp
cmake_minimum_required(VERSION 3.21.1)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/BSSASHelpers.cmake")

set(BSSAS_CONSTANTS_QML "${CMAKE_CURRENT_SOURCE_DIR}/BSSAS/Constants.qml")
set(BSSAS_INNO_SCRIPT_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/Setup.iss")
set(BSSAS_APP_DISPLAY_NAME "BSSAS")
set(BSSAS_ORGANIZATION_NAME "MANGONIEKTAP")
set(BSSAS_INNO_OUTPUT_DIR "D:/Downloads")
set(BSSAS_UPDATE_MANIFEST_URL "http://106.12.144.22:8081/bssas/version.json")
set(BSSAS_UPDATER_EXECUTABLE_NAME "Updater")
set(BSSAS_UPDATER_EXECUTABLE_FILENAME "${BSSAS_UPDATER_EXECUTABLE_NAME}.exe")

bssas_require_existing_path("${BSSAS_CONSTANTS_QML}" "Constants.qml")
bssas_require_existing_path("${BSSAS_INNO_SCRIPT_TEMPLATE}" "Inno Setup script")

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${BSSAS_CONSTANTS_QML}")

file(
    STRINGS
    "${BSSAS_CONSTANTS_QML}"
    BSSAS_VERSION_DECLARATIONS
    REGEX "readonly property string version:[ \t]*\"[^\"]+\""
)
list(LENGTH BSSAS_VERSION_DECLARATIONS BSSAS_VERSION_DECLARATION_COUNT)
if (NOT BSSAS_VERSION_DECLARATION_COUNT EQUAL 1)
    message(
        FATAL_ERROR
        "Expected exactly one version declaration in ${BSSAS_CONSTANTS_QML}"
    )
endif()

list(GET BSSAS_VERSION_DECLARATIONS 0 BSSAS_VERSION_DECLARATION)
string(
    REGEX REPLACE
    ".*readonly property string version:[ \t]*\"([^\"]+)\".*"
    "\\1"
    BSSAS_APP_VERSION
    "${BSSAS_VERSION_DECLARATION}"
)

if (NOT BSSAS_APP_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+(\\.[0-9]+)?$")
    message(
        FATAL_ERROR
        "Constants.version must use a numeric dotted format compatible with "
        "Windows/Inno Setup version metadata, for example 26.04.04"
    )
endif()

string(REPLACE "." ";" BSSAS_APP_VERSION_COMPONENTS "${BSSAS_APP_VERSION}")
set(BSSAS_APP_VERSION_RC_COMPONENTS "")
foreach(BSSAS_APP_VERSION_COMPONENT IN LISTS BSSAS_APP_VERSION_COMPONENTS)
    if (BSSAS_APP_VERSION_COMPONENT MATCHES "^0+$")
        set(BSSAS_APP_VERSION_COMPONENT_NORMALIZED "0")
    else()
        string(
            REGEX REPLACE
            "^0+([1-9][0-9]*)$"
            "\\1"
            BSSAS_APP_VERSION_COMPONENT_NORMALIZED
            "${BSSAS_APP_VERSION_COMPONENT}"
        )
    endif()

    list(APPEND BSSAS_APP_VERSION_RC_COMPONENTS "${BSSAS_APP_VERSION_COMPONENT_NORMALIZED}")
endforeach()

list(LENGTH BSSAS_APP_VERSION_RC_COMPONENTS BSSAS_APP_VERSION_RC_COMPONENT_COUNT)
while(BSSAS_APP_VERSION_RC_COMPONENT_COUNT LESS 4)
    list(APPEND BSSAS_APP_VERSION_RC_COMPONENTS "0")
    math(EXPR BSSAS_APP_VERSION_RC_COMPONENT_COUNT "${BSSAS_APP_VERSION_RC_COMPONENT_COUNT} + 1")
endwhile()

list(SUBLIST BSSAS_APP_VERSION_RC_COMPONENTS 0 4 BSSAS_APP_VERSION_RC_COMPONENTS)
string(REPLACE ";" "," BSSAS_APP_VERSION_RC "${BSSAS_APP_VERSION_RC_COMPONENTS}")

option(LINK_INSIGHT "Link Qt Insight Tracker library" ON)

project(BSSASApp VERSION "${BSSAS_APP_VERSION}" LANGUAGES CXX)

option(
    BSSAS_REQUIRE_EMBEDDED_PYTHON
    "Require bundled embedded Python runtime in PythonModules/Runtime"
    ON
)

set(PYTHON_MODULES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/PythonModules")
set(PYTHON_SCRIPTS_DIR "${PYTHON_MODULES_DIR}/Scripts")
set(PYTHON_RUNTIME_DIR "${PYTHON_MODULES_DIR}/Runtime")
set(BSSAS_REQUIRED_PYTHON_SCRIPT_NAMES
    WAVExport.py
    WAVImport.py
    PDFExport.py
    shared_paths.py
)
set(PYTHON_EMBEDDED_RECOMMENDED_VERSION "3.14.3")
set(GENERATE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Generate")
set(GENERATE_DATA_CHUNK_DIR "${GENERATE_DIR}/DataChunk")
set(GENERATE_REPORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Generate/report")
set(BSSAS_REQUIRED_DATA_CHUNK_FILENAMES
    AFDROIS.json
    ISAR.json
)
set(BSSAS_REQUIRED_REPORT_FILENAMES
    AFDROIS.html
    ISAR.html
)
set(GENERATE_REPORT_TEMPLATE_RELATIVE "Generate/report/AFDROIS.html")
set(GENERATE_REPORT_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/${GENERATE_REPORT_TEMPLATE_RELATIVE}")
set(BSSAS_ABDOMEN_MODEL_RELATIVE "3D_Resources/3D model of abdomen.glb")
set(BSSAS_ABDOMEN_MODEL "${CMAKE_CURRENT_SOURCE_DIR}/${BSSAS_ABDOMEN_MODEL_RELATIVE}")
set(BSSAS_DISTRIBUTION_DIR_NAME "distribution")
set(BSSAS_APP_ICON_ICO "${CMAKE_CURRENT_SOURCE_DIR}/AppIcon.ico")
set(BSSAS_EXECUTABLE_NAME "${CMAKE_PROJECT_NAME}")
set(BSSAS_EXECUTABLE_FILENAME "${BSSAS_EXECUTABLE_NAME}.exe")

file(TO_NATIVE_PATH "${BSSAS_APP_ICON_ICO}" BSSAS_APP_ICON_ICO_NATIVE)
file(TO_NATIVE_PATH "${BSSAS_INNO_OUTPUT_DIR}" BSSAS_INNO_OUTPUT_DIR_NATIVE)
file(
    TO_NATIVE_PATH
    "${CMAKE_BINARY_DIR}/${BSSAS_DISTRIBUTION_DIR_NAME}"
    BSSAS_INNO_DISTRIBUTION_DIR
)

bssas_require_existing_path("${PYTHON_MODULES_DIR}" "PythonModules directory")
bssas_require_existing_path("${PYTHON_SCRIPTS_DIR}" "Python scripts directory")
bssas_collect_existing_paths(
    BSSAS_REQUIRED_PYTHON_SCRIPT_FILES
    "${PYTHON_SCRIPTS_DIR}"
    "Python script"
    ${BSSAS_REQUIRED_PYTHON_SCRIPT_NAMES}
)

set(PYTHON_EMBEDDED_EXECUTABLE "")
set(PYTHON_EMBEDDED_VERSION "")
set(PYTHON_EMBEDDED_RUNTIME_DIR "")
file(GLOB PYTHON_EMBEDDED_RUNTIME_CANDIDATES
    "${PYTHON_RUNTIME_DIR}/Python-*-embed-*/python.exe"
)

foreach(PYTHON_EMBEDDED_CANDIDATE IN LISTS PYTHON_EMBEDDED_RUNTIME_CANDIDATES)
    get_filename_component(
        PYTHON_EMBEDDED_CANDIDATE_DIR
        "${PYTHON_EMBEDDED_CANDIDATE}"
        DIRECTORY
    )
    get_filename_component(
        PYTHON_EMBEDDED_CANDIDATE_NAME
        "${PYTHON_EMBEDDED_CANDIDATE_DIR}"
        NAME
    )
    string(
        REGEX MATCH
        "^Python-([0-9]+\\.[0-9]+\\.[0-9]+)-embed-.*$"
        PYTHON_EMBEDDED_CANDIDATE_MATCHED
        "${PYTHON_EMBEDDED_CANDIDATE_NAME}"
    )
    if (NOT PYTHON_EMBEDDED_CANDIDATE_MATCHED)
        continue()
    endif()

    string(
        REGEX REPLACE
        "^Python-([0-9]+\\.[0-9]+\\.[0-9]+)-embed-.*$"
        "\\1"
        PYTHON_EMBEDDED_CANDIDATE_VERSION
        "${PYTHON_EMBEDDED_CANDIDATE_NAME}"
    )
    if (NOT PYTHON_EMBEDDED_EXECUTABLE OR
        PYTHON_EMBEDDED_CANDIDATE_VERSION VERSION_GREATER PYTHON_EMBEDDED_VERSION)
        set(PYTHON_EMBEDDED_EXECUTABLE "${PYTHON_EMBEDDED_CANDIDATE}")
        set(PYTHON_EMBEDDED_VERSION "${PYTHON_EMBEDDED_CANDIDATE_VERSION}")
        set(PYTHON_EMBEDDED_RUNTIME_DIR "${PYTHON_EMBEDDED_CANDIDATE_DIR}")
    endif()
endforeach()

if (BSSAS_REQUIRE_EMBEDDED_PYTHON)
    if (NOT PYTHON_EMBEDDED_EXECUTABLE)
        message(
            FATAL_ERROR
            "Embedded Python runtime is missing in ${PYTHON_RUNTIME_DIR}.\n"
            "Please place Python ${PYTHON_EMBEDDED_RECOMMENDED_VERSION} (or newer) "
            "embeddable runtime under PythonModules/Runtime before packaging."
        )
    endif()
elseif (NOT PYTHON_EMBEDDED_EXECUTABLE)
    message(
        WARNING
        "Embedded Python runtime was not found under ${PYTHON_RUNTIME_DIR}. "
        "Release package may require system Python."
    )
endif()

if (PYTHON_EMBEDDED_EXECUTABLE)
    message(
        STATUS
        "Embedded Python runtime detected: ${PYTHON_EMBEDDED_VERSION} "
        "(${PYTHON_EMBEDDED_EXECUTABLE})"
    )
endif()

bssas_require_existing_path("${GENERATE_REPORT_DIR}" "Generate report directory")
bssas_require_existing_path("${GENERATE_DATA_CHUNK_DIR}" "Generate data chunk directory")
bssas_require_existing_path("${GENERATE_REPORT_TEMPLATE}" "Report template")
bssas_collect_existing_paths(
    BSSAS_REQUIRED_DATA_CHUNK_FILES
    "${GENERATE_DATA_CHUNK_DIR}"
    "data chunk file"
    ${BSSAS_REQUIRED_DATA_CHUNK_FILENAMES}
)
bssas_collect_existing_paths(
    BSSAS_REQUIRED_REPORT_FILES
    "${GENERATE_REPORT_DIR}"
    "report template"
    ${BSSAS_REQUIRED_REPORT_FILENAMES}
)
bssas_require_existing_path("${BSSAS_ABDOMEN_MODEL}" "3D abdomen model")

configure_file(
    "${BSSAS_INNO_SCRIPT_TEMPLATE}"
    "${CMAKE_BINARY_DIR}/Setup.iss"
    @ONLY
)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")
set(QML_IMPORT_PATH "${QT_QML_OUTPUT_DIRECTORY}"
    CACHE STRING "Import paths for local QML modules"
    FORCE
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(KFR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/kfr-windows-x86_64/lib/cmake/kfr")
find_package(KFR CONFIG REQUIRED)

find_package(Qt6 6.10.1 REQUIRED COMPONENTS
    Core
    Gui
    Multimedia
    Network
    Sql
    Qml
    Quick
    Quick3D
    Graphs
    QuickTimeline
    ShaderTools
)

qt_standard_project_setup()
qt_policy(SET QTP0004 NEW)

add_subdirectory(BackendCore)
add_subdirectory(BSSAS)
add_subdirectory(BSSASContent)

set(BSSAS_APP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/App/DebugTerminalManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/App/DebugTerminalManager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/App/main.cpp
)

if (WIN32)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/App/app_icon.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc"
        @ONLY
    )
    list(APPEND BSSAS_APP_SOURCES
        "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc"
    )
endif()

qt_add_executable(${CMAKE_PROJECT_NAME} WIN32
    ${BSSAS_APP_SOURCES}
)

add_executable(${BSSAS_UPDATER_EXECUTABLE_NAME} WIN32
    ${CMAKE_CURRENT_SOURCE_DIR}/App/updater_main.cpp
)

set_target_properties(${BSSAS_UPDATER_EXECUTABLE_NAME} PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    BSSAS_APP_VERSION_STR="${BSSAS_APP_VERSION}"
)

target_compile_definitions(BackendCore PRIVATE
    BSSAS_UPDATE_MANIFEST_URL="${BSSAS_UPDATE_MANIFEST_URL}"
)

qt_add_resources(${CMAKE_PROJECT_NAME} "configuration"
    PREFIX "/"
    FILES
        qtquickcontrols2.conf
)

qt_add_resources(${CMAKE_PROJECT_NAME} "app_icon"
    PREFIX "/"
    BASE ${CMAKE_CURRENT_SOURCE_DIR}
    FILES
        ${BSSAS_APP_ICON_ICO}
)

set_source_files_properties("${BSSAS_ABDOMEN_MODEL}"
    PROPERTIES QT_RESOURCE_ALIAS "abdomen.glb"
)

qt_add_resources(${CMAKE_PROJECT_NAME} "three_d_resources"
    PREFIX "/3D_Resources"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/3D_Resources"
    FILES
        "${BSSAS_ABDOMEN_MODEL}"
)

qt_add_resources(${CMAKE_PROJECT_NAME} "generate_templates"
    PREFIX "/"
    FILES
        ${GENERATE_REPORT_TEMPLATE_RELATIVE}
)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Multimedia
    Qt6::Sql
    Qt6::Qml
    Qt6::Quick
    Qt6::Quick3D
    Qt6::Graphs
    Qt6::QuickTimeline
    Qt6::ShaderTools
    BackendCore
    BSSAS
    BSSASplugin
    BSSASContent
    BSSASContentplugin
    Component
    Componentplugin
    BSSASSettingsStorage
    BSSASSettingsStorageplugin
)

if (WIN32)
    target_link_libraries(${BSSAS_UPDATER_EXECUTABLE_NAME} PRIVATE
        shell32
        user32
    )
endif()

if (MSVC)
    set_target_properties(${BSSAS_UPDATER_EXECUTABLE_NAME} PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    target_compile_options(${BSSAS_UPDATER_EXECUTABLE_NAME} PRIVATE /utf-8)
endif()

add_dependencies(${CMAKE_PROJECT_NAME} ${BSSAS_UPDATER_EXECUTABLE_NAME})

add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:${BSSAS_UPDATER_EXECUTABLE_NAME}>
        $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:UsbDaq4203>
        $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${PYTHON_MODULES_DIR}"
        "$<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/PythonModules"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/Generate"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${GENERATE_DATA_CHUNK_DIR}"
        "$<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/Generate/DataChunk"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${GENERATE_REPORT_DIR}"
        "$<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/Generate/report"
)

if (LINK_INSIGHT)
    include(insight OPTIONAL)
endif()

set(BSSAS_INSTALL_APP_DIR ".")

if (MSVC)
    get_filename_component(BSSAS_MSVC_BIN_ARCH_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(BSSAS_MSVC_BIN_HOST_DIR "${BSSAS_MSVC_BIN_ARCH_DIR}" DIRECTORY)
    get_filename_component(BSSAS_MSVC_BIN_DIR "${BSSAS_MSVC_BIN_HOST_DIR}" DIRECTORY)
    get_filename_component(BSSAS_MSVC_TOOLSET_DIR "${BSSAS_MSVC_BIN_DIR}" DIRECTORY)
    get_filename_component(BSSAS_VC_ROOT_DIR "${BSSAS_MSVC_TOOLSET_DIR}/../../.." ABSOLUTE)

    file(GLOB BSSAS_MSVC_REDIST_DIRS
        "${BSSAS_VC_ROOT_DIR}/Redist/MSVC/*/x64/Microsoft.VC*.CRT"
    )
    list(SORT BSSAS_MSVC_REDIST_DIRS)
    list(REVERSE BSSAS_MSVC_REDIST_DIRS)

    set(BSSAS_MSVC_RUNTIME_DLLS "")
    foreach(BSSAS_MSVC_REDIST_DIR IN LISTS BSSAS_MSVC_REDIST_DIRS)
        file(GLOB BSSAS_MSVC_REDIST_CANDIDATES
            "${BSSAS_MSVC_REDIST_DIR}/concrt140.dll"
            "${BSSAS_MSVC_REDIST_DIR}/msvcp140*.dll"
            "${BSSAS_MSVC_REDIST_DIR}/vcruntime140*.dll"
        )
        if (BSSAS_MSVC_REDIST_CANDIDATES)
            set(BSSAS_MSVC_RUNTIME_DLLS ${BSSAS_MSVC_REDIST_CANDIDATES})
            break()
        endif()
    endforeach()

    if (NOT BSSAS_MSVC_RUNTIME_DLLS)
        file(GLOB BSSAS_MSVC_RUNTIME_DLLS
            "${BSSAS_MSVC_BIN_ARCH_DIR}/msvcp140*.dll"
            "${BSSAS_MSVC_BIN_ARCH_DIR}/vcruntime140*.dll"
            "${BSSAS_MSVC_BIN_ARCH_DIR}/vcruntime140_*.dll"
        )
    endif()

    if (BSSAS_MSVC_RUNTIME_DLLS)
        list(REMOVE_DUPLICATES BSSAS_MSVC_RUNTIME_DLLS)
        install(FILES ${BSSAS_MSVC_RUNTIME_DLLS}
            DESTINATION ${BSSAS_INSTALL_APP_DIR}
        )
    else()
        message(WARNING "Could not locate MSVC runtime redistribution DLLs for packaging.")
    endif()
endif()

install(TARGETS ${CMAKE_PROJECT_NAME}
    BUNDLE DESTINATION ${BSSAS_INSTALL_APP_DIR}
    LIBRARY DESTINATION ${BSSAS_INSTALL_APP_DIR}
    RUNTIME DESTINATION ${BSSAS_INSTALL_APP_DIR}
)

install(TARGETS ${BSSAS_UPDATER_EXECUTABLE_NAME}
    BUNDLE DESTINATION ${BSSAS_INSTALL_APP_DIR}
    LIBRARY DESTINATION ${BSSAS_INSTALL_APP_DIR}
    RUNTIME DESTINATION ${BSSAS_INSTALL_APP_DIR}
)

install(FILES "${BACKENDCORE_USB_DAQ4203_DLL}"
    DESTINATION ${BSSAS_INSTALL_APP_DIR}
)

install(FILES ${BSSAS_REQUIRED_PYTHON_SCRIPT_FILES}
    DESTINATION PythonModules/Scripts
)

if (PYTHON_EMBEDDED_RUNTIME_DIR)
    install(DIRECTORY "${PYTHON_EMBEDDED_RUNTIME_DIR}"
        DESTINATION PythonModules/Runtime
    )
endif()

install(FILES ${BSSAS_REQUIRED_DATA_CHUNK_FILES}
    DESTINATION Generate/DataChunk
)

install(FILES ${BSSAS_REQUIRED_REPORT_FILES}
    DESTINATION Generate/report
)

qt_generate_deploy_qml_app_script(
    TARGET ${CMAKE_PROJECT_NAME}
    EXCLUDE_PLUGIN_TYPES qmltooling
    EXCLUDE_PLUGINS
        qsqlibase
        qsqlmimer
        qsqloci
        qsqlodbc
        qsqlpsql
    NO_COMPILER_RUNTIME
    OUTPUT_SCRIPT BSSAS_DEPLOY_SCRIPT
)

install(CODE [[
set(QT_DEPLOY_BIN_DIR ".")
]])
install(SCRIPT "${BSSAS_DEPLOY_SCRIPT}")

if (CMAKE_CONFIGURATION_TYPES)
    set(BSSAS_DISTRIBUTION_BUILD_CONFIG "$<CONFIG>")
else()
    set(BSSAS_DISTRIBUTION_BUILD_CONFIG "${CMAKE_BUILD_TYPE}")
endif()

add_custom_target(${CMAKE_PROJECT_NAME}_distribution ALL
    COMMAND ${CMAKE_COMMAND}
        -DBSSAS_BUILD_CONFIG=${BSSAS_DISTRIBUTION_BUILD_CONFIG}
        -DBSSAS_BINARY_DIR=${CMAKE_BINARY_DIR}
        -DBSSAS_DISTRIBUTION_DIR=$<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/${BSSAS_DISTRIBUTION_DIR_NAME}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildDistribution.cmake"
    DEPENDS ${CMAKE_PROJECT_NAME}
    VERBATIM
)
