cmake_minimum_required(VERSION 3.20)
project(BufferedMinhash VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)

option(BUFFERED_MINHASH_BUILD_EXAMPLES "Build the recovery example" ON)
option(BUFFERED_MINHASH_ENABLE_SANITIZERS "Enable ASan/UBSan on tests and examples" OFF)

add_library(buffered_minhash INTERFACE)
add_library(BufferedMinhash::buffered_minhash ALIAS buffered_minhash)
target_compile_features(buffered_minhash INTERFACE cxx_std_20)
target_include_directories(buffered_minhash INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

function(buffered_minhash_configure_executable target)
    target_link_libraries(${target} PRIVATE BufferedMinhash::buffered_minhash)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
        if(BUFFERED_MINHASH_ENABLE_SANITIZERS)
            target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    endif()
endfunction()

if(BUILD_TESTING)
    add_executable(buffered_minhash_tests tests/buffered_minhash_test.cpp)
    buffered_minhash_configure_executable(buffered_minhash_tests)
    add_test(NAME buffered_minhash.correctness COMMAND buffered_minhash_tests)
endif()

if(BUFFERED_MINHASH_BUILD_EXAMPLES)
    add_executable(buffered_minhash_example examples/recovery.cpp)
    buffered_minhash_configure_executable(buffered_minhash_example)
    if(BUILD_TESTING)
        add_test(NAME buffered_minhash.example COMMAND buffered_minhash_example)
    endif()
endif()

install(TARGETS buffered_minhash EXPORT BufferedMinhashTargets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT BufferedMinhashTargets
    NAMESPACE BufferedMinhash:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/BufferedMinhash)
configure_package_config_file(cmake/BufferedMinhashConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/BufferedMinhashConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/BufferedMinhash)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/BufferedMinhashConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion ARCH_INDEPENDENT)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/BufferedMinhashConfig.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/BufferedMinhashConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/BufferedMinhash)
