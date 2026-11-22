# f1core: pure logic + settings, Qt Core only. Everything the unit tests need.
add_library(f1core STATIC
    core/pyjson.cpp core/pyjson.h
    core/sanitize.cpp core/sanitize.h
    core/i18n.cpp core/i18n.h
    core/timeutil.cpp core/timeutil.h
    core/sessions.cpp core/sessions.h
    core/teams.cpp core/teams.h
    core/weather.cpp core/weather.h
    core/spoilers.cpp core/spoilers.h
    core/parsers.cpp core/parsers.h
    core/f1feed.cpp core/f1feed.h
    core/pilltext.cpp core/pilltext.h
    core/tracks.cpp core/tracks.h
    core/notifications.cpp core/notifications.h
    core/weekend.cpp core/weekend.h
    core/update_rules.cpp core/update_rules.h
    core/release_key.h
    settings.cpp settings.h
)
target_include_directories(f1core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(f1core PUBLIC Qt6::Core)

# f1services: network, cache, Windows helpers.
add_library(f1services STATIC
    paths.cpp paths.h
    services/store.cpp services/store.h
    services/ratelimit.h
    services/httpapi.h
    services/http.cpp services/http.h
    services/credentials.cpp services/credentials.h
    services/f1stream.cpp services/f1stream.h
    services/poller.cpp services/poller.h
    services/winshell.cpp services/winshell.h
    services/widgets_button.cpp services/widgets_button.h
    services/notifier.cpp services/notifier.h
    services/updater.cpp services/updater.h
    services/release_sig.cpp services/release_sig.h
    services/wintrust.cpp services/wintrust.h
    services/autostart.cpp services/autostart.h
)
target_link_libraries(f1services PUBLIC f1core Qt6::Network PRIVATE dwmapi psapi advapi32 ole32 oleaut32 uuid wintrust crypt32 bcrypt)

# f1ui: widgets, tabs, panel, popup, taskbar countdown.
add_library(f1ui STATIC
    ui/theme.cpp ui/theme.h
    ui/widgets.cpp ui/widgets.h
    ui/tabs/common.cpp ui/tabs/common.h
    ui/tabs/schedule.cpp ui/tabs/schedule.h
    ui/tabs/weather.cpp ui/tabs/weather.h
    ui/tabs/tables.cpp ui/tabs/tables.h
    ui/tabs/live.cpp ui/tabs/live.h
    ui/panel.cpp ui/panel.h
    ui/popup.cpp ui/popup.h
    ui/settings_dialog.cpp ui/settings_dialog.h
    ui/taskbar_pill.cpp ui/taskbar_pill.h
    ui/desktop_widget.cpp ui/desktop_widget.h
    services/taskbar_embed.cpp services/taskbar_embed.h   # needs Qt Gui (QImage)
)
target_link_libraries(f1ui PUBLIC f1services Qt6::Widgets Qt6::Svg)

# The app: tray icon, panel, taskbar countdown.
qt_add_executable(F1DockTimers WIN32
    main.cpp
    app.cpp app.h
    app.rc
    app.manifest
)
target_link_libraries(F1DockTimers PRIVATE f1ui)
set_target_properties(F1DockTimers PROPERTIES OUTPUT_NAME F1DockTimers)

# Assets next to the exe, like the Python build's f1win/assets and lang folders.
add_custom_command(TARGET F1DockTimers POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:F1DockTimers>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/lang $<TARGET_FILE_DIR:F1DockTimers>/lang
    VERBATIM)

if(F1_DEPLOY AND WIN32)
    # Copy the Qt runtime next to the exe so it runs from the build folder. Only what the app
    # needs: no translations, no software OpenGL, no system D3D compiler.
    get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qtbin "${_qmake}" DIRECTORY)
    add_custom_command(TARGET F1DockTimers POST_BUILD
        COMMAND "${_qtbin}/windeployqt.exe" --no-translations --no-system-d3d-compiler --no-system-dxc-compiler
                --no-opengl-sw --no-compiler-runtime
                --skip-plugin-types generic,iconengines,networkinformation,styles
                --exclude-plugins qcertonlybackend,qjpeg,qgif,qico
                $<IF:$<CONFIG:Debug>,--debug,--release> $<TARGET_FILE:F1DockTimers>
        COMMENT "windeployqt"
        VERBATIM)
endif()

# Dev tool: renders the README screenshots (tools/readme_shots.cpp). Never shipped: the release
# packaging copies only the app.
add_executable(readme_shots ${PROJECT_SOURCE_DIR}/tools/readme_shots.cpp app.manifest)
target_link_libraries(readme_shots PRIVATE f1ui)
