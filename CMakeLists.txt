cmake_minimum_required(VERSION 3.16)

project(aodix VERSION 4.2.0.3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
	message(FATAL_ERROR "Aodix Linux is built as a 64-bit application")
endif()

find_package(X11 REQUIRED)
find_package(Threads REQUIRED)

if(NOT X11_XShm_FOUND)
	message(FATAL_ERROR "the X11 shared memory extension (libXext) is required")
endif()

# win32 compatibility layer on top of X11
add_library(adxplatform STATIC
	src/platform/win32_gdi.cpp
	src/platform/win32_window.cpp
	src/platform/win32_menu.cpp
	src/platform/win32_controls.cpp
	src/platform/win32_dialog.cpp
	src/platform/win32_system.cpp
)
target_include_directories(adxplatform PUBLIC src/platform ${X11_INCLUDE_DIR})
target_link_libraries(adxplatform PUBLIC ${X11_LIBRARIES} ${X11_Xext_LIB} Threads::Threads)

# plugin hosting
add_library(adxplugin STATIC
	src/plugin/plugin_loader.cpp
)
target_include_directories(adxplugin PUBLIC src/plugin)
target_link_libraries(adxplugin PUBLIC ${CMAKE_DL_LIBS})

# aodix
add_executable(aodix
	src/argulib/Argulib.cpp
	src/aodix/Aodix.cpp
	src/aodix/AodixAbout.cpp
	src/aodix/AodixBounce.cpp
	src/aodix/AodixConfig.cpp
	src/aodix/AodixCore.cpp
	src/aodix/AodixCoreAudio.cpp
	src/aodix/AodixCoreDsp.cpp
	src/aodix/AodixCoreEdit.cpp
	src/aodix/AodixCoreExport.cpp
	src/aodix/AodixCoreGui.cpp
	src/aodix/AodixCoreGuiCommand.cpp
	src/aodix/AodixCoreGuiKeyDown.cpp
	src/aodix/AodixCoreGuiKeyPiano.cpp
	src/aodix/AodixCoreGuiKeyUp.cpp
	src/aodix/AodixCoreGuiMouseDown.cpp
	src/aodix/AodixCoreGuiMouseMove.cpp
	src/aodix/AodixCoreGuiMousePin.cpp
	src/aodix/AodixCoreGuiMouseUp.cpp
	src/aodix/AodixCoreGuiMouseWheel.cpp
	src/aodix/AodixCoreHost.cpp
	src/aodix/AodixCoreImport.cpp
	src/aodix/AodixCoreInstance.cpp
	src/aodix/AodixCoreMidiIn.cpp
	src/aodix/AodixCorePaint.cpp
	src/aodix/AodixCorePaintRouting.cpp
	src/aodix/AodixCorePaintSequencer.cpp
	src/aodix/AodixCoreSequencer.cpp
	src/aodix/AodixLinux.cpp
	src/aodix/AodixMainWnd.cpp
	src/aodix/AodixPlug.cpp
)

# the original code base relies on a few msvc leniencies (string literals passed as char*, multi-char
# constants for file ids); keep the warnings quiet instead of touching every line of the original code
target_compile_options(aodix PRIVATE -Wno-write-strings -Wno-multichar -Wno-unused-parameter -Wno-int-to-pointer-cast -Wno-unused-result -Wno-format-truncation -Wno-format-overflow -Wno-conversion-null)
target_link_libraries(aodix PRIVATE adxplatform adxplugin)

# run from the build directory: copy the skins next to the executable
add_custom_command(TARGET aodix POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/resources/Skins $<TARGET_FILE_DIR:aodix>/Skins
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/resources/HTMLHelp $<TARGET_FILE_DIR:aodix>/HTMLHelp
)

include(GNUInstallDirs)
install(TARGETS aodix RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY resources/Skins DESTINATION ${CMAKE_INSTALL_DATADIR}/aodix)
install(DIRECTORY resources/HTMLHelp DESTINATION ${CMAKE_INSTALL_DATADIR}/aodix)
install(FILES resources/aodix.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES resources/aodix.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/32x32/apps)
