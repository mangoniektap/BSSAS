if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/qtinsight.conf")
    return()
endif()

if (QT_VERSION VERSION_LESS 6.5.0)
    message(WARNING "Qt Insight Tracker requires Qt 6.5.0 or newer.")
    return()
endif()

find_package(Qt6 REQUIRED COMPONENTS InsightTracker)

qt_add_resources(${CMAKE_PROJECT_NAME} "insight_configuration"
    PREFIX "/"
    FILES
        qtinsight.conf
)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    Qt6::InsightTracker
)
