cmake_minimum_required(VERSION 3.14)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

project(twocrypto_pool VERSION 0.1.0 LANGUAGES CXX)

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

# Private parity only: the installed pool target never selects a
# concrete policy. The header is compiled into parity executables below.
set(TWOCRYPTO_PARITY_POLICY_PATH "" CACHE FILEPATH
    "Private parity policy header for pool test executables")
set(_twocrypto_parity_policy_abs "")
if(TWOCRYPTO_PARITY_POLICY_PATH)
    if(NOT TWOCRYPTO_POOL_BUILD_BENCHMARKS AND NOT TWOCRYPTO_POOL_BUILD_TESTS)
        message(FATAL_ERROR
            "TWOCRYPTO_PARITY_POLICY_PATH requires private parity targets")
    endif()
    get_filename_component(_twocrypto_parity_policy_abs
        "${TWOCRYPTO_PARITY_POLICY_PATH}" ABSOLUTE
        BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    if(NOT EXISTS "${_twocrypto_parity_policy_abs}" OR
       IS_DIRECTORY "${_twocrypto_parity_policy_abs}")
        message(FATAL_ERROR
            "TWOCRYPTO_PARITY_POLICY_PATH is not a regular file: "
            "${_twocrypto_parity_policy_abs}")
    endif()
endif()

function(pool_private_defaults TARGET_NAME)
    target_compile_options(${TARGET_NAME} PRIVATE -Wall -Wextra)
endfunction()

function(pool_private_parity_policy TARGET_NAME)
    if(_twocrypto_parity_policy_abs)
        target_compile_definitions(${TARGET_NAME} PRIVATE
            TWOCRYPTO_POLICY_HEADER="${_twocrypto_parity_policy_abs}")
    endif()
endfunction()


if(TWOCRYPTO_POOL_BUILD_BENCHMARKS)
    find_package(Boost 1.79 REQUIRED COMPONENTS json)
    find_package(Threads REQUIRED)

    add_executable(benchmark_harness_i src/benchmark_harness.cpp)
    target_link_libraries(benchmark_harness_i PRIVATE
        twocrypto::pool Boost::json Threads::Threads)
    pool_private_defaults(benchmark_harness_i)
    pool_private_parity_policy(benchmark_harness_i)
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
    pool_private_parity_policy(test_pool_snapshot)
    add_test(NAME test_pool_snapshot COMMAND test_pool_snapshot)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_numeric_policy.cpp")
    enable_testing()
    add_executable(test_numeric_policy tests/test_numeric_policy.cpp)
    target_link_libraries(test_numeric_policy PRIVATE twocrypto::pool)
    pool_private_defaults(test_numeric_policy)
    pool_private_parity_policy(test_numeric_policy)
    add_test(NAME test_numeric_policy COMMAND test_numeric_policy)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_price_feed_policy.cpp")
    enable_testing()
    add_executable(test_price_feed_policy tests/test_price_feed_policy.cpp)
    target_link_libraries(test_price_feed_policy PRIVATE twocrypto::pool)
    pool_private_defaults(test_price_feed_policy)
    add_test(NAME test_price_feed_policy COMMAND test_price_feed_policy)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_fair_price_fee_policy.cpp")
    enable_testing()
    add_executable(test_fair_price_fee_policy tests/test_fair_price_fee_policy.cpp)
    target_link_libraries(test_fair_price_fee_policy PRIVATE twocrypto::pool)
    pool_private_defaults(test_fair_price_fee_policy)
    add_test(NAME test_fair_price_fee_policy COMMAND test_fair_price_fee_policy)
endif()

endif()

install(DIRECTORY include/pools/twocrypto_fx/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/pools/twocrypto_fx"
    PATTERN "yieldbasis.hpp" EXCLUDE)
install(TARGETS twocrypto_pool EXPORT twocrypto_poolTargets)
install(EXPORT twocrypto_poolTargets
    FILE twocrypto_poolTargets.cmake
    NAMESPACE twocrypto::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/twocrypto_pool")

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/twocrypto-pool-config.cmake.in"
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
