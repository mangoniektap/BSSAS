set(USB_DAQ4203_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/PeripheralDrivers/UsbDaq4203")
set(USB_DAQ4203_DLL "${USB_DAQ4203_ROOT}/Usb_Daq4203.dll")
set(USB_DAQ4203_LIB "${USB_DAQ4203_ROOT}/Usb_Daq4203.lib")

if (NOT EXISTS "${USB_DAQ4203_DLL}" OR NOT EXISTS "${USB_DAQ4203_LIB}")
    message(FATAL_ERROR "Usb_Daq4203 runtime files are missing from ${USB_DAQ4203_ROOT}")
endif()

add_library(UsbDaq4203 SHARED IMPORTED GLOBAL)
set_target_properties(UsbDaq4203 PROPERTIES
    IMPORTED_IMPLIB "${USB_DAQ4203_LIB}"
    IMPORTED_LOCATION "${USB_DAQ4203_DLL}"
)

set(BACKENDCORE_USB_DAQ4203_DLL "${USB_DAQ4203_DLL}" PARENT_SCOPE)

file(GLOB_RECURSE BACKEND_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Source_Files/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DataBase/*.cpp
)

file(GLOB_RECURSE BACKEND_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Header_Files/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Header_Files/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DataBase/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/DataBase/*.hpp
)

set(BACKEND_MANUAL_MOC_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/Header_Files/RecognitionServiceManager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Header_Files/SystemStatusMonitor.h
)

# Work around intermittent AUTOMOC source collection misses for these QObject classes.
set_source_files_properties(${BACKEND_MANUAL_MOC_HEADERS} PROPERTIES SKIP_AUTOMOC ON)
qt_wrap_cpp(BACKEND_MANUAL_MOC_SOURCES ${BACKEND_MANUAL_MOC_HEADERS})

add_library(BackendCore STATIC
    ${BACKEND_SOURCES}
    ${BACKEND_HEADERS}
    ${BACKEND_MANUAL_MOC_SOURCES}
)

target_include_directories(BackendCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Header_Files
        ${CMAKE_CURRENT_SOURCE_DIR}/DataBase
)

target_link_libraries(BackendCore PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Sql
    kfr_dsp
    kfr_dft
    kfr_io
)

target_link_libraries(BackendCore PRIVATE
    UsbDaq4203
)
