set_source_files_properties(
    Constants.qml
    Theme.qml
    PROPERTIES
        QT_QML_SINGLETON_TYPE true
)

qt_add_library(BSSAS STATIC)

qt_add_qml_module(BSSAS
    URI "BSSAS"
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    QML_FILES
        "Constants.qml"
        "Theme.qml"
)
