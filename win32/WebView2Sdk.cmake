set(_fcitx_webview2_root "${CMAKE_BINARY_DIR}/_deps/webview2")
set(_fcitx_webview2_nupkg
    "${_fcitx_webview2_root}/Microsoft.Web.WebView2.nupkg")
set(_fcitx_webview2_extract "${_fcitx_webview2_root}/pkg")
set(_fcitx_webview2_include
    "${_fcitx_webview2_extract}/build/native/include")
set(_fcitx_webview2_loader
    "${_fcitx_webview2_extract}/runtimes/win-x64/native/WebView2Loader.dll")

file(MAKE_DIRECTORY "${_fcitx_webview2_root}")

if(NOT EXISTS "${_fcitx_webview2_include}/WebView2.h" OR
   NOT EXISTS "${_fcitx_webview2_loader}")
  file(
    DOWNLOAD "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2"
    "${_fcitx_webview2_nupkg}" STATUS _fcitx_webview2_download_status
    SHOW_PROGRESS TLS_VERIFY ON)
  list(GET _fcitx_webview2_download_status 0 _fcitx_webview2_status_code)
  list(GET _fcitx_webview2_download_status 1 _fcitx_webview2_status_text)
  if(NOT _fcitx_webview2_status_code EQUAL 0)
    message(
      FATAL_ERROR
        "Failed to download WebView2 SDK package: ${_fcitx_webview2_status_text}"
    )
  endif()
  file(REMOVE_RECURSE "${_fcitx_webview2_extract}")
  file(MAKE_DIRECTORY "${_fcitx_webview2_extract}")
  file(
    ARCHIVE_EXTRACT
    INPUT "${_fcitx_webview2_nupkg}"
    DESTINATION "${_fcitx_webview2_extract}")
endif()

if(NOT EXISTS "${_fcitx_webview2_include}/WebView2.h")
  message(FATAL_ERROR "WebView2.h not found in downloaded SDK package.")
endif()

if(NOT EXISTS "${_fcitx_webview2_loader}")
  message(FATAL_ERROR "WebView2Loader.dll not found in downloaded SDK package.")
endif()

set(FCITX_WIN32_WEBVIEW2_INCLUDE_DIR
    "${_fcitx_webview2_include}"
    CACHE PATH "Extracted WebView2 SDK include directory" FORCE)
set(FCITX_WIN32_WEBVIEW2_LOADER_DLL
    "${_fcitx_webview2_loader}"
    CACHE FILEPATH "Extracted WebView2 loader DLL" FORCE)
