cmake_minimum_required(VERSION 3.21)

set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")

project(EdgeWidget VERSION 0.1.0 LANGUAGES CXX)
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Before Qt's deploy support file: exe and Qt DLLs side by side.
if(WIN32)
    set(CMAKE_INSTALL_BINDIR "." CACHE STRING "" FORCE)
endif()

# LGPL modules only: no Qt NetworkAuth (GPL), see README.
set(qt_components Widgets Network Bluetooth)
if(UNIX AND NOT APPLE)
    list(APPEND qt_components DBus)
endif()
find_package(Qt6 6.5 REQUIRED COMPONENTS ${qt_components})
qt_standard_project_setup()

# Qt 6.8 still links AGL, which the macOS 26 SDK no longer has.
if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
    get_target_property(wrap_opengl_libs WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
    if(wrap_opengl_libs)
        list(FILTER wrap_opengl_libs EXCLUDE REGEX "(^|[ /])AGL(\\.framework)?$")
        set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES INTERFACE_LINK_LIBRARIES "${wrap_opengl_libs}")
    endif()
endif()

qt_add_executable(EdgeWidget WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/app/AppSettings.cpp src/app/AppSettings.h
    src/app/Localization.cpp src/app/Localization.h
    src/app/MediaState.h
    src/ui/EdgePanel.cpp src/ui/EdgePanel.h
    src/ui/ErrorToasts.cpp src/ui/ErrorToasts.h
    src/ui/PanelContent.cpp src/ui/PanelContent.h
    src/ui/PanelLayout.h
    src/ui/NowPlayingPopup.cpp src/ui/NowPlayingPopup.h
    src/ui/PlaylistScrubPopup.cpp src/ui/PlaylistScrubPopup.h
    src/ui/ScaledWidget.h
    src/ui/PlayerView.cpp src/ui/PlayerView.h
    src/ui/TabStrip.cpp src/ui/TabStrip.h
    src/ui/SettingsPage.cpp src/ui/SettingsPage.h
    src/ui/SpotifySetup.cpp src/ui/SpotifySetup.h
    src/ui/TrayIcon.cpp src/ui/TrayIcon.h
    src/ui/VinylIcon.cpp src/ui/VinylIcon.h
    src/ui/VolumeOverlay.cpp src/ui/VolumeOverlay.h
    src/ui/library/LibraryCard.cpp src/ui/library/LibraryCard.h
    src/ui/library/LibraryView.cpp src/ui/library/LibraryView.h
    src/ui/library/LibraryStyle.cpp src/ui/library/LibraryStyle.h
    src/ui/library/TrackListPopup.cpp src/ui/library/TrackListPopup.h
    src/playback/MediaController.cpp src/playback/MediaController.h
    src/playback/VolumeController.cpp src/playback/VolumeController.h
    src/device/MediaSpinProtocol.cpp src/device/MediaSpinProtocol.h
    src/device/BleLink.cpp src/device/BleLink.h
    src/device/DeviceController.cpp src/device/DeviceController.h
    src/spotify/SpotifyAuth.cpp src/spotify/SpotifyAuth.h
    src/spotify/SpotifyClient.cpp src/spotify/SpotifyClient.h
    src/spotify/SpotifyLauncher.cpp src/spotify/SpotifyLauncher.h
    src/spotify/SpotifyLocalLibrary.cpp src/spotify/SpotifyLocalLibrary.h
    src/platform/AppInit.h
    src/platform/AppVolume.h
    src/platform/Autostart.h
    src/platform/BluetoothSupport.h
    src/platform/LocalPlayer.h
    src/platform/SecretStore.h
    src/platform/SpotifyApp.h
    src/platform/WindowLayer.cpp src/platform/WindowLayer.h
)

if(WIN32)
    target_sources(EdgeWidget PRIVATE
        src/platform/win/AppInit.cpp
        src/platform/win/AppVolume.cpp
        src/platform/win/Autostart.cpp
        src/platform/win/BluetoothSupport.cpp
        src/platform/win/SecretStore.cpp
        src/platform/win/SpotifyApp.cpp
        src/platform/win/WinLocalPlayer.cpp
        src/platform/win/WindowLayer.cpp
        resources/EdgeWidget.rc
    )
    target_link_libraries(EdgeWidget PRIVATE windowsapp crypt32)
    # winrt/base.h in C++17: newer MSVC made this deprecation an error
    target_compile_definitions(EdgeWidget PRIVATE _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS)
elseif(APPLE)
    target_sources(EdgeWidget PRIVATE
        src/platform/CoverFetcher.cpp src/platform/CoverFetcher.h
        src/platform/mac/AppInit.cpp
        src/platform/mac/AppVolume.cpp
        src/platform/mac/Autostart.cpp
        src/platform/mac/BluetoothSupport.cpp
        src/platform/mac/MacLocalPlayer.cpp
        src/platform/mac/SecretStore.cpp
        src/platform/mac/SpotifyApp.mm
        src/platform/mac/WindowLayer.mm
        resources/EdgeWidget.icns
    )
    set_source_files_properties(resources/EdgeWidget.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(EdgeWidget PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/packaging/mac/Info.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME "Edge Widget"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.edgewidget.EdgeWidget"
        MACOSX_BUNDLE_ICON_FILE EdgeWidget.icns
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    )
    target_link_libraries(EdgeWidget PRIVATE "-framework AppKit" "-framework Security" "-framework CoreFoundation")
else()
    target_sources(EdgeWidget PRIVATE
        src/platform/CoverFetcher.cpp src/platform/CoverFetcher.h
        src/platform/linux/AppInit.cpp
        src/platform/linux/AppVolume.cpp
        src/platform/linux/Autostart.cpp
        src/platform/linux/BluetoothSupport.cpp
        src/platform/linux/LinuxLocalPlayer.cpp
        src/platform/linux/SecretStore.cpp
        src/platform/linux/SpotifyApp.cpp
        src/platform/linux/WindowLayer.cpp
    )
    set_target_properties(EdgeWidget PROPERTIES OUTPUT_NAME edgewidget)
    target_link_libraries(EdgeWidget PRIVATE Qt6::DBus)
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBSECRET IMPORTED_TARGET libsecret-1)
    endif()
    if(LIBSECRET_FOUND)
        target_link_libraries(EdgeWidget PRIVATE PkgConfig::LIBSECRET)
        target_compile_definitions(EdgeWidget PRIVATE HAVE_LIBSECRET)
    else()
        message(WARNING "libsecret-1 not found: the Spotify token will be kept in the settings file")
    endif()
endif()

target_link_libraries(EdgeWidget PRIVATE Qt6::Widgets Qt6::Network Qt6::Bluetooth)
target_include_directories(EdgeWidget PRIVATE src)
get_target_property(edgewidget_sources EdgeWidget SOURCES)
list(FILTER edgewidget_sources INCLUDE REGEX "^src/")
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX src FILES ${edgewidget_sources})

add_executable(make_icon EXCLUDE_FROM_ALL tools/make_icon.cpp src/ui/VinylIcon.cpp src/ui/VinylIcon.h)
target_include_directories(make_icon PRIVATE src)
target_link_libraries(make_icon PRIVATE Qt6::Gui)

set(qtbase_uk "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}/qtbase_uk.qm")

if(WIN32)
    install(TARGETS EdgeWidget RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES THIRD_PARTY_NOTICES.txt DESTINATION .)
    install(DIRECTORY licenses/ DESTINATION licenses OPTIONAL)
    install(FILES ${qtbase_uk} DESTINATION translations)

    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
    set(CMAKE_INSTALL_UCRT_LIBRARIES OFF)
    include(InstallRequiredSystemLibraries)

    set(deploy_options DEPLOY_TOOL_OPTIONS --no-opengl-sw --no-system-d3d-compiler)
elseif(APPLE)
    install(TARGETS EdgeWidget BUNDLE DESTINATION .)
    set(resources EdgeWidget.app/Contents/Resources)
    install(FILES THIRD_PARTY_NOTICES.txt DESTINATION ${resources})
    install(DIRECTORY licenses/ DESTINATION ${resources}/licenses OPTIONAL)
    install(FILES ${qtbase_uk} DESTINATION ${resources}/translations)
    set(deploy_options "")
else()
    include(GNUInstallDirs)
    install(TARGETS EdgeWidget RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES packaging/linux/edgewidget.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES resources/edgewidget.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
    install(FILES THIRD_PARTY_NOTICES.txt DESTINATION ${CMAKE_INSTALL_DOCDIR})
    install(DIRECTORY licenses/ DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses OPTIONAL)
    if(EXISTS ${qtbase_uk})
        install(FILES ${qtbase_uk} DESTINATION ${CMAKE_INSTALL_DATADIR}/edgewidget/translations)
    endif()
endif()

if(WIN32 OR APPLE)
    qt_generate_deploy_app_script(
        TARGET EdgeWidget
        OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
        NO_TRANSLATIONS
        ${deploy_options}
    )
    install(SCRIPT ${deploy_script})
endif()
