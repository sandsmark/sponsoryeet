cmake_minimum_required(VERSION 3.5)
project(sponsoryeet)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

option(ENABLE_SANITIZERS "Enable runtime sanitizing (for development)")
if (ENABLE_SANITIZERS)
    message("Enabling asan and ubsan")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined")
endif()

add_executable(sponsoryeet
    main.cc
    ssl.cc
    )

target_link_libraries(sponsoryeet ${CMAKE_DL_LIBS})
install(TARGETS sponsoryeet)
