qt_add_library(BSSASContent STATIC)

set_source_files_properties(
    "image resources/home page/home_bowel_sound_logo.png"
    PROPERTIES QT_RESOURCE_ALIAS "images/home/home_bowel_sound_logo.png"
)

set_source_files_properties(
    "image resources/home page/home_hero_intestine_waveform.png"
    PROPERTIES QT_RESOURCE_ALIAS "images/home/home_hero_intestine_waveform.png"
)

set_source_files_properties(
    "image resources/home page/home_metric_card_icons.png"
    PROPERTIES QT_RESOURCE_ALIAS "images/home/home_metric_card_icons.png"
)

set_source_files_properties(
    "image resources/home page/home_recent_signal_waveform.png"
    PROPERTIES QT_RESOURCE_ALIAS "images/home/home_recent_signal_waveform.png"
)

set_source_files_properties(
    "image resources/home page/home_security_badge_icon.png"
    PROPERTIES QT_RESOURCE_ALIAS "images/home/home_security_badge_icon.png"
)

set_source_files_properties(
    "image resources/home page/home_sidebar_nav_icons.png"
    PROPERTIES QT_RESOURCE_ALIAS "images/home/home_sidebar_nav_icons.png"
)

set_source_files_properties(
    "image resources/home page/home_system_status_icons.png"
    PROPERTIES QT_RESOURCE_ALIAS "images/home/home_system_status_icons.png"
)

qt_add_qml_module(BSSASContent
    URI "BSSASContent"
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    QML_FILES
        "App.qml"
        "MainWindow_Background.qml"
        "SettingsSidebar.qml"
        "UpdateDialog.qml"
        "Right_Top_Toolbar.qml"
        "Left_Toolbar.qml"
        "Window_Display_Master_Control.qml"
        "display_content/About_Page.qml"
        "display_content/Auxiliary_Diagnosis.qml"
        "display_content/Software_Master_Control.qml"
        "display_content/Medical_Record_Management.qml"
        "display_content/Home_Page.qml"
        "display_content/Temporal_Spectral_Monitoring.qml"
        "display_content/software_master_control_content/Signal_Processing_Settings.qml"
        "display_content/software_master_control_content/File_Management.qml"
        "display_content/software_master_control_content/DAQ_Device_Console.qml"
        "display_content/software_master_control_content/Sensor_Monitoring.qml"
    RESOURCES
        "images/full_screen.png"
        "images/minus.png"
        "images/close.png"
        "images/setting.png"
        "images/details.png"
        "images/export.png"
        "images/save.png"
        "images/calendar.png"
        "images/correct.png"
        "images/error.png"
        "images/keyboard.png"
        "images/left.png"
        "images/pencil.png"
        "images/right.png"
        "images/search.png"
        "images/time.png"
        "images/upload.png"
        "images/whirlwind.png"
        "image resources/home page/home_bowel_sound_logo.png"
        "image resources/home page/home_hero_intestine_waveform.png"
        "image resources/home page/home_metric_card_icons.png"
        "image resources/home page/home_recent_signal_waveform.png"
        "image resources/home page/home_security_badge_icon.png"
        "image resources/home page/home_sidebar_nav_icons.png"
        "image resources/home page/home_system_status_icons.png"
)

qt_add_library(Component STATIC)

qt_add_qml_module(Component
    URI "MangoComponent"
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    QML_FILES
        "components/BlurGlass.qml"
        "components/CheckBoxes.qml"
        "components/ColumnTabBar.qml"
        "components/DataVisualizationGraphCartesianSpectrum.qml"
        "components/DateSelection.qml"
        "components/DirectionAwareHoverCard.qml"
        "components/OSDataVisualizationGraphCartesianWaveform.qml"
        "components/ProgressIndicator.qml"
        "components/RadioButton.qml"
        "components/RTDataVisualizationGraphCartesianWaveform.qml"
        "components/Select.qml"
        "components/StandbyAnimation.qml"
        "components/TextFields.qml"
        "components/TimeFrequencyAnalysisReport.qml"
        "components/ToastNotification.qml"
        "components/TimeSelection.qml"
        "components/ToggleSwitch.qml"
    IMPORTS
        BSSAS
)

qt_add_library(BSSASSettingsStorage STATIC)

set_source_files_properties(
    AppState.qml
    PROPERTIES
        QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(BSSASSettingsStorage
    URI "BSSASSettingsStorage"
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    QML_FILES
        "AppState.qml"
)
