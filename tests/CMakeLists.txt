# Only the pure units are testable off the game: everything else in this mod
# either reads engine memory or installs a hook. CameraMath is deliberately free
# of both so the composition the camera applies and the projection the reticle is
# placed from can be checked against each other without launching Prey.
add_executable(preyht_tests
    camera_math_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/PreyHeadTracking/utility/CameraMath.cpp
)

target_include_directories(preyht_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/PreyHeadTracking)

if(MSVC)
    target_compile_options(preyht_tests PRIVATE /W4 /WX /EHsc)
endif()

add_test(NAME preyht_tests COMMAND preyht_tests)

# The INI boundary is the other part that runs off the game: Config only reads a
# file and clamps numbers, so what it lets through to the engine camera, the
# engine's CVars and the socket bind is checkable here.
add_executable(preyht_config_tests
    config_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/PreyHeadTracking/Config.cpp
)

target_include_directories(preyht_config_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(preyht_config_tests PRIVATE cameraunlock)

if(MSVC)
    target_compile_options(preyht_config_tests PRIVATE /W4 /WX /EHsc)
endif()

add_test(NAME preyht_config_tests COMMAND preyht_config_tests)
