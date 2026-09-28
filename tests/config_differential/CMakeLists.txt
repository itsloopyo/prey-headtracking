# The differential test: the published build's reader (oracle/), the frozen
# reader in src/legacy_config/ with the import that maps it, the
# config owner's migration, and every input the corpus generator makes from the
# published build's shipped HeadTracking.ini.
#
# The oracle is compiled from published/ with `cameraunlock` and `preyht` renamed
# by the preprocessor. It needs core as it stood at the published build's pin,
# whose headers share their paths and names with the current ones, so the two can
# only live in one binary under different names.
add_library(prey_config_oracle STATIC
    oracle/oracle.cpp
    oracle/published/src/Config.cpp
    oracle/published/core/src/config/ini_reader.cpp
)
target_include_directories(prey_config_oracle PRIVATE
    oracle
    oracle/published/include
    oracle/published/core/include
)
target_compile_definitions(prey_config_oracle PRIVATE
    cameraunlock=prey_oracle_cameraunlock
    preyht=prey_oracle_preyht
)
if(MSVC)
    target_compile_options(prey_config_oracle PRIVATE /EHsc)
endif()

add_executable(prey_config_differential
    differential_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/PreyHeadTracking/Config.cpp
    ${CMAKE_SOURCE_DIR}/src/legacy_config/legacy_config.cpp
)
target_include_directories(prey_config_differential PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/cameraunlock-core/cpp/include
)
target_compile_definitions(prey_config_differential PRIVATE
    PREY_DIFFERENTIAL_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    PREY_REPO_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(prey_config_differential PRIVATE cameraunlock prey_config_oracle bcrypt)
if(MSVC)
    target_compile_options(prey_config_differential PRIVATE /W4 /WX /EHsc /bigobj)
endif()

add_test(NAME config_differential COMMAND prey_config_differential)

# Core's canonical config lint over the committed file and every file the
# differential test migrated, which it writes beside itself under migrated\.
add_test(NAME config_lint COMMAND node
    ${CMAKE_CURRENT_SOURCE_DIR}/lint-migrated.mjs
    $<TARGET_FILE_DIR:prey_config_differential>/migrated)
set_tests_properties(config_lint PROPERTIES DEPENDS config_differential)
set_tests_properties(config_differential PROPERTIES TIMEOUT 3600)
set_tests_properties(config_differential config_lint PROPERTIES LABELS differential)
