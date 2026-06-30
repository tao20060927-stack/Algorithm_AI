cmake_minimum_required(VERSION 3.20)
project(AIPlayerDesktop LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(NLOHMANN_JSON_INCLUDE_DIR "" CACHE PATH "Directory that contains nlohmann/json.hpp")
if (NOT NLOHMANN_JSON_INCLUDE_DIR)
    if (EXISTS "$ENV{CONDA_PREFIX}/Library/include/nlohmann/json.hpp")
        set(NLOHMANN_JSON_INCLUDE_DIR "$ENV{CONDA_PREFIX}/Library/include")
    elseif (EXISTS "$ENV{USERPROFILE}/anaconda3/Library/include/nlohmann/json.hpp")
        set(NLOHMANN_JSON_INCLUDE_DIR "$ENV{USERPROFILE}/anaconda3/Library/include")
    endif ()
endif ()

if (NOT EXISTS "${NLOHMANN_JSON_INCLUDE_DIR}/nlohmann/json.hpp")
    message(FATAL_ERROR "nlohmann/json.hpp was not found. Set NLOHMANN_JSON_INCLUDE_DIR.")
endif ()

add_library(ai_player_core STATIC
    src/AIPlayerEngine.cpp
    src/AStarStrategy.cpp
    src/BossStrategy.cpp
    src/BranchBoundStrategy.cpp
    src/DijkstraStrategy.cpp
    src/DivideConquerStrategy.cpp
    src/RealtimeGreedyStrategy.cpp
    src/Reward.cpp
    src/ShortestPathStrategy.cpp
    vendor/sha256.c
)

target_include_directories(ai_player_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor"
    "${NLOHMANN_JSON_INCLUDE_DIR}"
)

target_compile_definitions(ai_player_core PUBLIC NOMINMAX)

add_executable(ai_player_smoke src/SmokeTest.cpp)
target_link_libraries(ai_player_smoke PRIVATE ai_player_core)

add_executable(ai_player_eval src/GreedyEval.cpp)
target_link_libraries(ai_player_eval PRIVATE ai_player_core)

if (MSVC)
    set(WEBVIEW2_PACKAGE_ROOT "$ENV{WEBVIEW2_SDK}" CACHE PATH "Microsoft.Web.WebView2 package root")
    if (NOT WEBVIEW2_PACKAGE_ROOT)
        file(GLOB _webview2_versions "$ENV{USERPROFILE}/.nuget/packages/microsoft.web.webview2/*")
        list(SORT _webview2_versions COMPARE NATURAL ORDER DESCENDING)
        list(GET _webview2_versions 0 WEBVIEW2_PACKAGE_ROOT)
    endif ()

    set(WEBVIEW2_NATIVE_DIR "${WEBVIEW2_PACKAGE_ROOT}/build/native")
    if (NOT EXISTS "${WEBVIEW2_NATIVE_DIR}/include/WebView2.h")
        message(FATAL_ERROR "WebView2 SDK was not found. Install Microsoft.Web.WebView2 or set WEBVIEW2_SDK.")
    endif ()

    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(WEBVIEW2_ARCH_DIR "${WEBVIEW2_NATIVE_DIR}/x64")
    else ()
        set(WEBVIEW2_ARCH_DIR "${WEBVIEW2_NATIVE_DIR}/x86")
    endif ()

    add_executable(AIPlayerDesktop WIN32 src/DesktopApp.cpp)
    target_link_libraries(AIPlayerDesktop PRIVATE
        ai_player_core
        "${WEBVIEW2_ARCH_DIR}/WebView2LoaderStatic.lib"
        ole32
        oleaut32
        user32
    )
    target_include_directories(AIPlayerDesktop PRIVATE
        "${WEBVIEW2_NATIVE_DIR}/include"
        "${WEBVIEW2_NATIVE_DIR}/include-winrt"
    )
    target_compile_definitions(AIPlayerDesktop PRIVATE UNICODE _UNICODE)
    target_compile_options(AIPlayerDesktop PRIVATE /utf-8)
    target_compile_options(ai_player_core PRIVATE /utf-8)
    target_compile_options(ai_player_smoke PRIVATE /utf-8)
    target_compile_options(ai_player_eval PRIVATE /utf-8)
else ()
    message(WARNING "MSVC was not detected. Building ai_player_core and ai_player_smoke only; the WebView2 desktop executable requires MSVC.")
endif ()
