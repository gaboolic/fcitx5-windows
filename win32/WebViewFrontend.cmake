get_filename_component(_fcitx_repo_root "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(_fcitx_webview_source_dir "${_fcitx_repo_root}/fcitx5-webview")
set(_fcitx_webview_dist_dir "${CMAKE_BINARY_DIR}/_deps/fcitx5-webview/dist")
set(_fcitx_webview_stamp_dir "${CMAKE_BINARY_DIR}/_deps/fcitx5-webview")
set(_fcitx_webview_npm_stamp "${_fcitx_webview_stamp_dir}/npm-install.stamp")

if(NOT EXISTS "${_fcitx_webview_source_dir}/package.json")
  message(
    FATAL_ERROR
      "fcitx5-webview submodule missing: expected ${_fcitx_webview_source_dir}/package.json. Run git submodule update --init --recursive."
  )
endif()

find_program(FCITX_WIN32_PNPM_EXECUTABLE pnpm)
find_program(FCITX_WIN32_NPM_EXECUTABLE npm REQUIRED)

if(FCITX_WIN32_PNPM_EXECUTABLE)
  set(_fcitx_webview_install_command "${FCITX_WIN32_PNPM_EXECUTABLE}" install)
  set(_fcitx_webview_build_command
      "${FCITX_WIN32_PNPM_EXECUTABLE}" exec parcel build --target universal
      --dist-dir "${_fcitx_webview_dist_dir}" --public-url ./)
else()
  set(_fcitx_webview_install_command "${FCITX_WIN32_NPM_EXECUTABLE}" install
      --no-package-lock)
  set(_fcitx_webview_build_command
      "${FCITX_WIN32_NPM_EXECUTABLE}" exec -- parcel build --target universal
      --dist-dir "${_fcitx_webview_dist_dir}" --public-url ./)
endif()

file(GLOB_RECURSE _fcitx_webview_sources CONFIGURE_DEPENDS
     "${_fcitx_webview_source_dir}/page/*"
     "${_fcitx_webview_source_dir}/package.json"
     "${_fcitx_webview_source_dir}/pnpm-lock.yaml"
     "${_fcitx_webview_source_dir}/package-lock.json"
     "${_fcitx_webview_source_dir}/tsconfig.json")

file(MAKE_DIRECTORY "${_fcitx_webview_stamp_dir}")

add_custom_command(
  OUTPUT "${_fcitx_webview_npm_stamp}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${_fcitx_webview_stamp_dir}"
  COMMAND ${_fcitx_webview_install_command}
  COMMAND ${CMAKE_COMMAND} -E touch "${_fcitx_webview_npm_stamp}"
  WORKING_DIRECTORY "${_fcitx_webview_source_dir}"
  DEPENDS "${_fcitx_webview_source_dir}/package.json"
  COMMENT "Install fcitx5-webview frontend dependencies")

add_custom_command(
  OUTPUT "${_fcitx_webview_dist_dir}/index.html"
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${_fcitx_webview_dist_dir}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${_fcitx_webview_dist_dir}"
  COMMAND ${_fcitx_webview_build_command}
  WORKING_DIRECTORY "${_fcitx_webview_source_dir}"
  DEPENDS ${_fcitx_webview_sources} "${_fcitx_webview_npm_stamp}"
  COMMENT "Build fcitx5-webview frontend assets")

add_custom_target(GenerateWebViewAssets
                  DEPENDS "${_fcitx_webview_dist_dir}/index.html")

set(FCITX_WIN32_WEBVIEW_DIST_DIR "${_fcitx_webview_dist_dir}"
    CACHE PATH "Built fcitx5-webview asset directory" FORCE)
