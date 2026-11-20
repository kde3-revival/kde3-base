include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/.."
  "${PROJECT_SOURCE_DIR}/khotkeys/shared"
)

# libui.la (dummy.cpp, an empty file, only made automake link it)
kde3_add_library(khotkeys_ui OBJECT
  SOURCES
    windowdef_simple_widget_ui.ui
    windowdef_list_widget_ui.ui
    command_url_widget_ui.ui
    menuentry_widget_ui.ui
    triggers_tab_ui.ui
    general_tab_ui.ui
    action_group_tab_ui.ui
    dcop_widget_ui.ui
    main_buttons_widget_ui.ui
    actions_listview_widget_ui.ui
    info_tab_ui.ui
    window_trigger_widget_ui.ui
    action_list_widget_ui.ui
    keyboard_input_widget_ui.ui
    condition_list_widget_ui.ui
    gesture_triggers_tab_ui.ui
    gestures_settings_tab_ui.ui
    general_settings_tab_ui.ui
    voice_input_widget_ui.ui
    voice_settings_tab_ui.ui
  LINK
    khotkeys_shared
    KDE3::kdeui
)
