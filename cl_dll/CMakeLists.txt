cmake_minimum_required(VERSION 3.10)
project(client)

set(CLIENT_LIB client)

file(GLOB CS_CLIENT_SRC "*.cpp")
file(GLOB CS_EV_SRC "events/*.cpp")
file(GLOB CS_WPN_SRC "cs_wpn/*.cpp")
file(GLOB CS_WPNSH_SRC "../dlls/wpn_shared/*.cpp")
file(GLOB CS_HUD_SRC "hud/*.cpp")
file(GLOB CS_STUDIO_SRC "studio/*.cpp")
file(GLOB CS_INPUT_SRC "input/*.cpp")
file(GLOB CS_PM_SRC "../pm_shared/*.cpp")
file(GLOB CS_GAMSH_SRC "../game_shared/*.cpp")
file(GLOB MINIUTL_SRC "../3rdparty/mainui_cpp/miniutl/*.cpp")
file(GLOB CS_PARTICLEMAN_SRC "particleman/*.cpp")

list(REMOVE_ITEM CS_CLIENT_SRC "${CMAKE_CURRENT_SOURCE_DIR}/ev_hldm.cpp")
list(REMOVE_ITEM CS_INPUT_SRC "${CMAKE_CURRENT_SOURCE_DIR}/input/input_sdl.cpp")
list(REMOVE_ITEM CS_CLIENT_SRC "${CMAKE_CURRENT_SOURCE_DIR}/inputw32.cpp")
list(REMOVE_ITEM CS_WPN_SRC "${CMAKE_CURRENT_SOURCE_DIR}/cs_wpn/cs_baseentity.cpp")
list(APPEND CS_CLIENT_SRC ../common/interface.cpp)
list(APPEND CS_CLIENT_SRC ../public/utflib.cpp)

if(HAVE_STRLCPY)
	add_definitions(-DHAVE_STRLCPY)
else()
	list(APPEND CS_CLIENT_SRC ../public/strlcpy.c)
endif()

if(HAVE_STRLCAT)
	add_definitions(-DHAVE_STRLCAT)
else()
	list(APPEND CS_CLIENT_SRC ../public/strlcat.c)
endif()

list(APPEND CS_CLIENT_SRC ${CS_EV_SRC})
list(APPEND CS_CLIENT_SRC ${CS_WPN_SRC})
list(APPEND CS_CLIENT_SRC ${CS_WPNSH_SRC})
list(APPEND CS_CLIENT_SRC ${CS_HUD_SRC})
list(APPEND CS_CLIENT_SRC ${CS_STUDIO_SRC})
list(APPEND CS_CLIENT_SRC ${CS_PM_SRC})
list(APPEND CS_CLIENT_SRC ${CS_INPUT_SRC})
list(APPEND CS_CLIENT_SRC ${CS_GAMSH_SRC})
list(APPEND CS_CLIENT_SRC ${MINIUTL_SRC})
list(APPEND CS_CLIENT_SRC ${CS_PARTICLEMAN_SRC})

include_directories(
	include
	include/hud
	include/studio
	include/math
	../cl_dll
	../common
	../engine
	../pm_shared
	../dlls
	../game_shared
	../public/
	../public/cl_dll
	../3rdparty/mainui_cpp
	../3rdparty/mainui_cpp/controls
	../3rdparty/mainui_cpp/menus
	../3rdparty/mainui_cpp/miniutl
	particleman)

add_definitions(-DCLIENT_WEAPONS -DCLIENT_DLL -DSTDINT_H=<cstdint>)

if(NOT MSVC)
	add_definitions(-DLINUX -D_LINUX)
	add_definitions(-Dstricmp=strcasecmp -D_strnicmp=strncasecmp -Dstrnicmp=strncasecmp -fms-extensions)
else()
	add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE)
endif()

add_library(${CLIENT_LIB} SHARED ${CS_CLIENT_SRC})

# The build tree keeps object files that are older than the headers they
# include, so a header can change without the translation units that use it
# being rebuilt. The weapon ids are the ones that hurt: a stale object kept
# the old value and the buy menu drew the P90 for the Zeus. Every header of
# this directory is pinned, so a change to any of them always rebuilds every
# unit that includes it. Only headers that really changed are rebuilt, so
# the cost is paid once per header change.
file(GLOB_RECURSE OC2_CLIENT_HEADERS "*.h")
set(OC2_PINNED_HEADERS ${OC2_CLIENT_HEADERS}
    "${CMAKE_CURRENT_SOURCE_DIR}/../dlls/weapontype.h")
foreach(pinned ${OC2_PINNED_HEADERS})
    if(EXISTS "${pinned}")
        set_property(SOURCE ${CS_CLIENT_SRC} APPEND PROPERTY OBJECT_DEPENDS "${pinned}")
    endif()
endforeach()

# --- OC2 ragdoll (BulletPhysics) batch 1: x64 static link smoke test ---
# The Bullet3 static libraries are built separately (x64, /MD to match this DLL);
# see BulletPhysics-Xash3D-adaptation-plan.md for the build command. This step only
# proves they can be linked into client_amd64.dll; the physics core itself lands
# in batch 3.
set(OC2_BULLET3_ROOT "D:/xashcs2/_ai-build/bullet3_build" CACHE PATH
    "Prebuilt Bullet3 static library root (x64)")
set(OC2_BULLET3_SRC "D:/xashcs2/_ai-build/deps/bullet3/src" CACHE PATH
    "Bullet3 source root (header search path)")
if(EXISTS "${OC2_BULLET3_SRC}")
    # Batch 1 only compiles one smoke translation unit, so the Bullet3 include path
    # is set per file: a target-wide include path would invalidate every object.
    set_source_files_properties(oc2_bullet_smoke.cpp oc2_bullet_ragdoll.cpp PROPERTIES
        COMPILE_OPTIONS "/I${OC2_BULLET3_SRC}")
    set(OC2_BULLET3_LIBS "")
    foreach(_b3 Bullet3Collision Bullet3Common Bullet3Dynamics Bullet3Geometry
            BulletCollision BulletDynamics LinearMath)
        find_library(OC2_B3_${_b3} NAMES ${_b3}
            PATHS "${OC2_BULLET3_ROOT}/lib" NO_DEFAULT_PATH)
        if(OC2_B3_${_b3})
            list(APPEND OC2_BULLET3_LIBS "${OC2_B3_${_b3}}")
        else()
            message(WARNING "OC2 ragdoll: Bullet3 static library not found: ${_b3}")
        endif()
    endforeach()
    if(OC2_BULLET3_LIBS)
        target_link_libraries(${CLIENT_LIB} PRIVATE ${OC2_BULLET3_LIBS})
        message(STATUS "OC2 ragdoll: linked ${OC2_BULLET3_LIBS}")
    endif()
endif()

if(MSVC)
	set_source_files_properties(ammo.cpp buy_menu_state.cpp hud/scoreboard.cpp hud/spectator_gui.cpp statusbar.cpp text_message.cpp message.cpp saytext.cpp vgui_parser.cpp PROPERTIES COMPILE_OPTIONS "/utf-8")
endif()

set_target_postfix(${CLIENT_LIB})

install(TARGETS ${CLIENT_LIB}
	DESTINATION "${GAME_DIR}/${CLIENT_INSTALL_DIR}/"
	PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
	    GROUP_READ GROUP_EXECUTE
		WORLD_READ WORLD_EXECUTE)

# Install PDB file on Windows
if(MSVC)
	install(FILES $<TARGET_PDB_FILE:${CLIENT_LIB}>
		DESTINATION "${GAME_DIR}/${CLIENT_INSTALL_DIR}/" OPTIONAL)
endif()
