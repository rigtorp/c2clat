if(NOT CMAKE_CXX_STANDARD)
    include(CheckCXXCompilerFlag)
    CHECK_CXX_COMPILER_FLAG("-std=c++20" COMPILER_SUPPORTS_CXX20)
    CHECK_CXX_COMPILER_FLAG("-std=c++17" COMPILER_SUPPORTS_CXX17)
    CHECK_CXX_COMPILER_FLAG("-std=c++14" COMPILER_SUPPORTS_CXX14)
    CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)

    if(COMPILER_SUPPORTS_CXX20)
        set(CMAKE_CXX_STANDARD 20)
        message(STATUS "C++20: OK")
    elseif(COMPILER_SUPPORTS_CXX17)
        set(CMAKE_CXX_STANDARD 17)
        message(STATUS "C++17: OK")
    elseif(COMPILER_SUPPORTS_CXX14)
        set(CMAKE_CXX_STANDARD 14)
        message(STATUS "C++14: OK")
        message(WARNING "C++14 is old, please use newer compiler.")
    elseif(COMPILER_SUPPORTS_CXX11)
        set(CMAKE_CXX_STANDARD 11)
        message(STATUS "C++11: OK")
        message(WARNING "C++11 is old, please use newer compiler.")
    else()
        message(FATAL_ERROR "The compiler ${CMAKE_CXX_COMPILER} has no C++11 or above support. Please use a different C++ compiler.")
    endif()
endif()
