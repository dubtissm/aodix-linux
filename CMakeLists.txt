cmake_minimum_required(VERSION 3.16)

project(aodix VERSION 4.2.1.5 LANGUAGES C CXX)
add_compile_definitions(AODIX_VERSION_STRING="${PROJECT_VERSION}")

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
find_package(ZLIB REQUIRED)

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

# subset of the VST3 SDK (MIT license) needed for hosting
set(VST3SDK ${CMAKE_SOURCE_DIR}/third_party/vst3sdk)
add_library(vst3hosting STATIC
	${VST3SDK}/pluginterfaces/base/coreiids.cpp
	${VST3SDK}/pluginterfaces/base/funknown.cpp
	${VST3SDK}/pluginterfaces/base/ustring.cpp
	${VST3SDK}/pluginterfaces/base/conststringtable.cpp
	${VST3SDK}/public.sdk/source/common/commoniids.cpp
	${VST3SDK}/public.sdk/source/common/commonstringconvert.cpp
	${VST3SDK}/public.sdk/source/common/memorystream.cpp
	${VST3SDK}/public.sdk/source/common/threadchecker_linux.cpp
	${VST3SDK}/public.sdk/source/vst/vstinitiids.cpp
	${VST3SDK}/public.sdk/source/vst/hosting/connectionproxy.cpp
	${VST3SDK}/public.sdk/source/vst/hosting/eventlist.cpp
	${VST3SDK}/public.sdk/source/vst/hosting/hostclasses.cpp
	${VST3SDK}/public.sdk/source/vst/hosting/module.cpp
	${VST3SDK}/public.sdk/source/vst/hosting/module_linux.cpp
	${VST3SDK}/public.sdk/source/vst/hosting/parameterchanges.cpp
	${VST3SDK}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
	${VST3SDK}/public.sdk/source/vst/moduleinfo/moduleinfoparser.cpp
	${VST3SDK}/public.sdk/source/vst/utility/stringconvert.cpp
)
target_include_directories(vst3hosting PUBLIC ${VST3SDK})
target_compile_definitions(vst3hosting PUBLIC RELEASE=1)
target_compile_options(vst3hosting PRIVATE -w)
target_link_libraries(vst3hosting PUBLIC ${CMAKE_DL_LIBS} Threads::Threads)

# plugin hosting (VST3, CLAP, LV2, LADSPA)
add_library(adxplugin STATIC
	src/plugin/plugin_vst3.cpp
	src/plugin/plugin_ladspa.cpp
	src/plugin/plugin_clap.cpp
	src/plugin/plugin_lv2.cpp
	src/plugin/plugin_registry.cpp
)
target_include_directories(adxplugin PUBLIC src/plugin)
target_include_directories(adxplugin PRIVATE third_party/clap/include third_party/lv2/include)
target_link_libraries(adxplugin PUBLIC vst3hosting adxplatform)

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
	src/aodix/AodixCoreDspGraph.cpp
	src/aodix/AodixDspPool.cpp
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
target_link_libraries(aodix PRIVATE adxplatform adxplugin ZLIB::ZLIB)

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
install(FILES resources/aodix-mime.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages RENAME aodix.xml)
install(FILES resources/aodix.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/32x32/apps)
foreach(size 64 128 256)
	install(FILES resources/icons/aodix-${size}.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps RENAME aodix.png)
endforeach()
install(FILES README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR} RENAME copyright)
install(FILES third_party/vst3sdk/LICENSE.txt DESTINATION ${CMAKE_INSTALL_DOCDIR}/third_party RENAME LICENSE.vst3sdk)
install(FILES third_party/clap/LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR}/third_party RENAME LICENSE.clap)
install(FILES third_party/lv2/LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR}/third_party RENAME LICENSE.lv2)

include(cmake/Packaging.cmake)
