# Windows application executable — WinUI 3 / C++/WinRT
# Phase 2: full GUI shell with drag-drop, clipboard, and export.

# Locate the NuGet packages we fetched into deps/nuget.
file(GLOB SCANWISE_WASDK_DIRS "${CMAKE_SOURCE_DIR}/deps/nuget/Microsoft.WindowsAppSDK.*")
file(GLOB SCANWISE_CPPWINRT_DIRS "${CMAKE_SOURCE_DIR}/deps/nuget/Microsoft.Windows.CppWinRT.*")
list(LENGTH SCANWISE_WASDK_DIRS WASDK_COUNT)
list(LENGTH SCANWISE_CPPWINRT_DIRS CPPWINRT_COUNT)
if(WASDK_COUNT EQUAL 0 OR CPPWINRT_COUNT EQUAL 0)
    message(FATAL_ERROR "WinUI 3 NuGet packages not found. Run: deps/nuget.exe install Microsoft.WindowsAppSDK -OutputDirectory deps/nuget && deps/nuget.exe install Microsoft.Windows.CppWinRT -OutputDirectory deps/nuget")
endif()
list(GET SCANWISE_WASDK_DIRS 0 SCANWISE_WASDK_ROOT)
list(GET SCANWISE_CPPWINRT_DIRS 0 SCANWISE_CPPWINRT_ROOT)
message(STATUS "Windows App SDK: ${SCANWISE_WASDK_ROOT}")
message(STATUS "C++/WinRT:       ${SCANWISE_CPPWINRT_ROOT}")

set(SCANWISE_WINRT_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated/winrt")
file(MAKE_DIRECTORY "${SCANWISE_WINRT_GENERATED_DIR}")

# Generate C++/WinRT projection headers from the Windows App SDK winmd files
# plus the Windows SDK metadata.  This only needs to be done once per configure
# because the inputs are fixed for a given checkout.
set(SCANWISE_CPPWINRT_EXE "${SCANWISE_CPPWINRT_ROOT}/bin/cppwinrt.exe")
set(SCANWISE_WASDK_WINMD_DIR "${SCANWISE_WASDK_ROOT}/lib/uap10.0")
set(SCANWISE_WASDK_WINMD_DIR2 "${SCANWISE_WASDK_ROOT}/lib/uap10.0.18362")
set(SCANWISE_WEBVIEW2_WINMD "${CMAKE_SOURCE_DIR}/deps/nuget/Microsoft.Web.WebView2.*/lib/Microsoft.Web.WebView2.Core.winmd")
file(GLOB SCANWISE_WEBVIEW2_WINMD_PATHS "${SCANWISE_WEBVIEW2_WINMD}")
list(GET SCANWISE_WEBVIEW2_WINMD_PATHS 0 SCANWISE_WEBVIEW2_WINMD_PATH)

if(NOT EXISTS "${SCANWISE_CPPWINRT_EXE}")
    message(FATAL_ERROR "cppwinrt.exe not found at ${SCANWISE_CPPWINRT_EXE}")
endif()

set(CPPWINRT_ARGS
    -optimize
    -input "${SCANWISE_WASDK_WINMD_DIR}"
    -input "${SCANWISE_WASDK_WINMD_DIR2}"
    -input "${SCANWISE_WEBVIEW2_WINMD_PATH}"
    -input sdk
    -output "${SCANWISE_WINRT_GENERATED_DIR}"
)
execute_process(
    COMMAND "${SCANWISE_CPPWINRT_EXE}" ${CPPWINRT_ARGS}
    RESULT_VARIABLE SCANWISE_CPPWINRT_RESULT
    OUTPUT_VARIABLE SCANWISE_CPPWINRT_OUT
    ERROR_VARIABLE SCANWISE_CPPWINRT_ERR
)
if(NOT SCANWISE_CPPWINRT_RESULT EQUAL 0)
    message(FATAL_ERROR "cppwinrt failed: ${SCANWISE_CPPWINRT_ERR}\n${SCANWISE_CPPWINRT_OUT}")
endif()
message(STATUS "Generated C++/WinRT headers in ${SCANWISE_WINRT_GENERATED_DIR}")

# ---------------------------------------------------------------------------
# UI executable
# ---------------------------------------------------------------------------
add_executable(ScanWise WIN32
    winui/main.cpp
    winui/app.cpp
    winui/wic_helpers.cpp
    winui/drag_drop.cpp
    winui/clipboard.cpp
    winui/page_item.cpp
    winui/ScanWise.rc
)

target_include_directories(ScanWise PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/include
    ${SCANWISE_WASDK_ROOT}/include
    ${SCANWISE_WINRT_GENERATED_DIR}
)

target_link_libraries(ScanWise PRIVATE
    scanwise_engine
)

if(MSVC)
    target_compile_definitions(ScanWise PRIVATE
        UNICODE _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR
    )
    target_compile_options(ScanWise PRIVATE
        /await:strict
        /W4
        /WX-
    )
    target_link_options(ScanWise PRIVATE
        /SUBSYSTEM:WINDOWS
    )
    target_link_directories(ScanWise PRIVATE
        ${SCANWISE_WASDK_ROOT}/lib/win10-x64
        ${SCANWISE_WASDK_ROOT}/lib/native/win10-x64
    )
    target_link_libraries(ScanWise PRIVATE
        Microsoft.WindowsAppRuntime.Bootstrap.lib
        Microsoft.WindowsAppRuntime.lib
        Microsoft.UI.Dispatching.lib
        WindowsApp.lib
    )
endif()

# Copy the bootstrap DLL next to the executable so the unpackaged app can start.
add_custom_command(TARGET ScanWise POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SCANWISE_WASDK_ROOT}/runtimes/win-x64/native/Microsoft.WindowsAppRuntime.Bootstrap.dll"
        "$<TARGET_FILE_DIR:ScanWise>"
)

# Copy the Windows App SDK MSIX runtime packages and a local installer script
# so the unpacked app can be deployed to machines that don't have the runtime.
file(GLOB SCANWISE_WASDK_MSIX "${SCANWISE_WASDK_ROOT}/tools/MSIX/win10-x64/*.msix")
add_custom_command(TARGET ScanWise POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SCANWISE_WASDK_MSIX} "$<TARGET_FILE_DIR:ScanWise>"
)
add_custom_command(TARGET ScanWise POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/install_runtime.ps1"
        "$<TARGET_FILE_DIR:ScanWise>"
)
