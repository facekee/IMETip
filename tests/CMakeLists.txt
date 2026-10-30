add_executable(imetip_tests
    test_main.cpp
    test_session_logic.cpp
    test_indicator_layout.cpp
    test_window_anchor.cpp
    test_uia_geometry.cpp
)

target_include_directories(imetip_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(imetip_tests PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MSVC)
    target_compile_options(imetip_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    set_property(TARGET imetip_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

add_test(NAME imetip_tests COMMAND imetip_tests)
