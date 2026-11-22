find_package(Qt6 REQUIRED COMPONENTS Test)

get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
get_filename_component(F1_QT_BIN "${_qmake}" DIRECTORY)

# One Qt Test executable per area, mirroring reference-python/tests/test_*.py.
function(f1_test name)
    add_executable(${name} ${name}.cpp common.h ${PROJECT_SOURCE_DIR}/src/app.manifest)
    target_link_libraries(${name} PRIVATE f1ui Qt6::Test bcrypt)
    target_compile_definitions(${name} PRIVATE F1_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/")
    add_test(NAME ${name} COMMAND ${name})
    # Qt DLLs from the kit; a private home so nothing touches the real %APPDATA%.
    set_tests_properties(${name} PROPERTIES ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${F1_QT_BIN};QT_PLUGIN_PATH=set:${F1_QT_BIN}/../plugins;NOCTALIA_F1_HOME=set:${CMAKE_CURRENT_BINARY_DIR}/home-${name};QT_QPA_PLATFORM=set:offscreen")
endfunction()

f1_test(tst_time)
f1_test(tst_sessions)
f1_test(tst_parsers)
f1_test(tst_weather)
f1_test(tst_openf1)
f1_test(tst_spoilers)
f1_test(tst_i18n)
f1_test(tst_feed)
f1_test(tst_settings)
f1_test(tst_poller)
f1_test(tst_ui)
f1_test(tst_stream)
f1_test(tst_embed)
f1_test(tst_updater)

# lang/_template.txt must list every text the app shows (as the Python tests check).
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME lang_template COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/make_lang_template.py --check)
endif()
