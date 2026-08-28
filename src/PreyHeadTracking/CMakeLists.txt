set(PREY_DLL_SOURCES
    DllMain.cpp
    Framework.cpp
    Mods.cpp
    Config.cpp
    hooks/D3D11Hook.cpp
    mods/HeadTracking.cpp
    mods/CryEngineCamera.cpp
    mods/BuildProfile.cpp
    utility/AimTrace.cpp
    utility/CameraMath.cpp
    utility/Flashlight.cpp
    utility/GameCVars.cpp
    utility/GameState.cpp
    utility/Logging.cpp
    utility/PreyHud.cpp
    utility/ViewCamera.cpp
    utility/Watchpoint.cpp
)

add_library(PreyHeadTracking SHARED ${PREY_DLL_SOURCES})

set_target_properties(PreyHeadTracking PROPERTIES
    OUTPUT_NAME "PreyHeadTracking"
    SUFFIX ".asi"
    PREFIX ""
)

target_include_directories(PreyHeadTracking
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(PreyHeadTracking
    PRIVATE
        cameraunlock
        cameraunlock_hooks
        minhook
        d3d11
        dxgi
        psapi
        version
)

if(MSVC)
    target_compile_options(PreyHeadTracking PRIVATE
        /W4 /permissive- /EHa
        $<$<CONFIG:Release>:/O2 /GL>
    )
    set_target_properties(PreyHeadTracking PROPERTIES
        LINK_FLAGS_RELEASE "/LTCG /OPT:REF /OPT:ICF"
    )
endif()
