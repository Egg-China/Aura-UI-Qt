cmake_minimum_required(VERSION 3.21)

project(AuraUIQt VERSION 0.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Test)

add_library(aura_ui_protocol STATIC
    src/bridge_value.cpp
    src/bridge_value.h
    src/frame.cpp
    src/frame.h
    src/protocol.cpp
    src/protocol.h
)
target_link_libraries(aura_ui_protocol PUBLIC Qt6::Core)
target_include_directories(aura_ui_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(aura-ui-qt
    src/main.cpp
    src/main_window.cpp
    src/main_window.h
    src/runtime_controller.cpp
    src/runtime_controller.h
)
target_link_libraries(aura-ui-qt PRIVATE aura_ui_protocol Qt6::Widgets)

add_executable(aura_bridge_value_tests tests/bridge_value_tests.cpp)
target_link_libraries(aura_bridge_value_tests PRIVATE aura_ui_protocol Qt6::Test)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME bridge_value COMMAND aura_bridge_value_tests)
endif()

install(TARGETS aura-ui-qt RUNTIME DESTINATION bin)
