cmake_minimum_required(VERSION 3.14)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

project(twocrypto_pool VERSION 0.1.0 LANGUAGES CXX)

include(CMakeFindDependencyMacro)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# The public pool is header-only. Boost.Multiprecision is its only public
# third-party requirement; JSON, threads, and optional compiled policies stay
# private to final executables.
find_package(Boost 1.79 REQUIRED)

add_library(twocrypto_pool INTERFACE)
add_library(twocrypto::pool ALIAS twocrypto_pool)
set_target_properties(twocrypto_pool PROPERTIES EXPORT_NAME pool)
target_compile_features(twocrypto_pool INTERFACE cxx_std_17)
target_include_directories(twocrypto_pool INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
if(TARGET Boost::headers)
    target_link_libraries(twocrypto_pool INTERFACE Boost::headers)
elseif(TARGET Boost::boost)
    target_link_libraries(twocrypto_pool INTERFACE Boost::boost)
else()
    target_include_directories(twocrypto_pool INTERFACE ${Boost_INCLUDE_DIRS})
endif()

option(TWOCRYPTO_POOL_BUILD_BENCHMARKS
    "Build the optional exact-integer parity harness" OFF)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(_twocrypto_pool_build_tests_default ON)
else()
    set(_twocrypto_pool_build_tests_default OFF)
endif()
option(TWOCRYPTO_POOL_BUILD_TESTS
    "Build pool-local tests when this is the top-level project"
    ${_twocrypto_pool_build_tests_default})

set(TWOCRYPTO_POOL_POLICY_PATH "" CACHE FILEPATH
    "Optional repository policy header for final pool executables")
set(TWOCRYPTO_POOL_POLICY_SHA256 "" CACHE STRING
    "Optional expected SHA-256 digest for TWOCRYPTO_POOL_POLICY_PATH")
set(POOL_POLICY_ABS "")
set(POOL_POLICY_DIGEST "")
if(TWOCRYPTO_POOL_POLICY_PATH)
    get_filename_component(
        POOL_POLICY_ABS
        "${TWOCRYPTO_POOL_POLICY_PATH}"
        ABSOLUTE
        BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    if(NOT EXISTS "${POOL_POLICY_ABS}" OR IS_DIRECTORY "${POOL_POLICY_ABS}")
        message(FATAL_ERROR
            "TWOCRYPTO_POOL_POLICY_PATH does not name a regular file: "
            "${POOL_POLICY_ABS}")
    endif()
    file(SHA256 "${POOL_POLICY_ABS}" POOL_POLICY_DIGEST)
    string(TOLOWER "${POOL_POLICY_DIGEST}" POOL_POLICY_DIGEST)
    if(TWOCRYPTO_POOL_POLICY_SHA256)
        string(TOLOWER "${TWOCRYPTO_POOL_POLICY_SHA256}" POOL_POLICY_EXPECTED_DIGEST)
        if(NOT "${POOL_POLICY_EXPECTED_DIGEST}" STREQUAL "${POOL_POLICY_DIGEST}")
            message(FATAL_ERROR
                "TWOCRYPTO_POOL_POLICY_SHA256 does not match "
                "TWOCRYPTO_POOL_POLICY_PATH bytes")
        endif()
    endif()
    set_property(
        DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${POOL_POLICY_ABS}")
elseif(TWOCRYPTO_POOL_POLICY_SHA256)
    message(FATAL_ERROR
        "TWOCRYPTO_POOL_POLICY_SHA256 requires TWOCRYPTO_POOL_POLICY_PATH")
endif()

set(TWOCRYPTO_POOL_REVISION_OVERRIDE "" CACHE STRING
    "Attested pool source revision for archive builds")
if(TWOCRYPTO_POOL_REVISION_OVERRIDE)
    set(TWOCRYPTO_POOL_GIT_REVISION "${TWOCRYPTO_POOL_REVISION_OVERRIDE}")
else()
    execute_process(
        COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse HEAD
        OUTPUT_VARIABLE TWOCRYPTO_POOL_GIT_REVISION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT TWOCRYPTO_POOL_GIT_REVISION)
        set(TWOCRYPTO_POOL_GIT_REVISION "unknown")
    endif()
endif()
if(CMAKE_BUILD_TYPE)
    set(TWOCRYPTO_POOL_BUILD_MODE "${CMAKE_BUILD_TYPE}")
else()
    set(TWOCRYPTO_POOL_BUILD_MODE "default")
endif()
set(TWOCRYPTO_POOL_COMPILER
    "${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}")

function(pool_private_defaults TARGET_NAME)
    target_include_directories(${TARGET_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_options(${TARGET_NAME} PRIVATE -Wall -Wextra)
    target_compile_definitions(${TARGET_NAME} PRIVATE
        TWOCRYPTO_POOL_REVISION="${TWOCRYPTO_POOL_GIT_REVISION}"
        TWOCRYPTO_POOL_BUILD_MODE="${TWOCRYPTO_POOL_BUILD_MODE}"
        TWOCRYPTO_POOL_COMPILER="${TWOCRYPTO_POOL_COMPILER}"
        TWOCRYPTO_POOL_TARGET="${TARGET_NAME}")
endfunction()

# Only final executables receive a concrete repository policy.
function(pool_target_compiled_policy TARGET_NAME)
    if(POOL_POLICY_ABS)
        target_compile_definitions(${TARGET_NAME} PRIVATE
            TWOCRYPTO_POLICY_HEADER="${POOL_POLICY_ABS}"
            TWOCRYPTO_POOL_POLICY_PATH="${POOL_POLICY_ABS}"
            TWOCRYPTO_POOL_POLICY_SHA256="${POOL_POLICY_DIGEST}")
    endif()
endfunction()


if(TWOCRYPTO_POOL_BUILD_BENCHMARKS)
    find_package(Boost 1.79 REQUIRED COMPONENTS json)
    find_package(Threads REQUIRED)

    add_executable(benchmark_harness_i src/benchmark_harness.cpp)
    target_link_libraries(benchmark_harness_i PRIVATE
        twocrypto::pool Boost::json Threads::Threads)
    pool_private_defaults(benchmark_harness_i)
    pool_target_compiled_policy(benchmark_harness_i)
endif()

if(TWOCRYPTO_POOL_BUILD_TESTS)
find_package(Boost 1.79 REQUIRED COMPONENTS json)

add_executable(yb_policy_evaluator_i tests/yb_policy_evaluator.cpp)
target_link_libraries(yb_policy_evaluator_i PRIVATE twocrypto::pool Boost::json)
pool_private_defaults(yb_policy_evaluator_i)


# The snapshot test is supplied by the pool product and is intentionally
# conditional so package consumers may install headers without test sources.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_pool_snapshot.cpp")
    enable_testing()
    add_executable(test_pool_snapshot tests/test_pool_snapshot.cpp)
    target_link_libraries(test_pool_snapshot PRIVATE twocrypto::pool)
    pool_private_defaults(test_pool_snapshot)
    add_test(NAME test_pool_snapshot COMMAND test_pool_snapshot)
endif()

endif()

install(DIRECTORY include/pools/twocrypto_fx/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/pools/twocrypto_fx")
install(TARGETS twocrypto_pool EXPORT twocrypto_poolTargets)
install(EXPORT twocrypto_poolTargets
    FILE twocrypto_poolTargets.cmake
    NAMESPACE twocrypto::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/twocrypto_pool")

set(_twocrypto_pool_config_in
    "${CMAKE_CURRENT_BINARY_DIR}/twocrypto_poolConfig.cmake.in")
file(WRITE "${_twocrypto_pool_config_in}" [=[
@PACKAGE_INIT@
include(CMakeFindDependencyMacro)
find_dependency(Boost 1.79 CONFIG)
include("${CMAKE_CURRENT_LIST_DIR}/twocrypto_poolTargets.cmake")
check_required_components(twocrypto_pool)
]=])
configure_package_config_file(
    "${_twocrypto_pool_config_in}"
    "${CMAKE_CURRENT_BINARY_DIR}/twocrypto_poolConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/twocrypto_pool")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/twocrypto_poolConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/twocrypto_poolConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/twocrypto_poolConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/twocrypto_pool")
