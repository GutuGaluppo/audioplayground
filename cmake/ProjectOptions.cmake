# Shared compile options for first-party targets. Third-party code is never built with these.

add_library(ap_project_options INTERFACE)
add_library(ap::project_options ALIAS ap_project_options)

target_compile_features(ap_project_options INTERFACE cxx_std_20)

if(MSVC)
    target_compile_options(ap_project_options INTERFACE /W4 /permissive- /utf-8)
    if(AP_WARNINGS_AS_ERRORS)
        target_compile_options(ap_project_options INTERFACE /WX)
    endif()
else()
    target_compile_options(ap_project_options INTERFACE
        -Wall -Wextra -Wpedantic
        -Wshadow -Wconversion -Wsign-conversion
        -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual
        -Wdouble-promotion -Wimplicit-fallthrough)

    # Compile-time real-time safety for functions marked AP_NONBLOCKING (core/include/ap/core/RealtimeSafety.h).
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag(-Wfunction-effects AP_HAS_FUNCTION_EFFECTS)
    if(AP_HAS_FUNCTION_EFFECTS)
        target_compile_options(ap_project_options INTERFACE -Wfunction-effects)
    endif()
    if(AP_WARNINGS_AS_ERRORS)
        target_compile_options(ap_project_options INTERFACE -Werror)
    endif()
endif()

if(AP_SANITIZERS)
    if(MSVC)
        message(FATAL_ERROR "AP_SANITIZERS is only supported with Clang/GCC")
    endif()
    list(JOIN AP_SANITIZERS "," _ap_sanitizers)
    target_compile_options(ap_project_options INTERFACE -fsanitize=${_ap_sanitizers} -fno-omit-frame-pointer)
    target_link_options(ap_project_options INTERFACE -fsanitize=${_ap_sanitizers})
endif()
