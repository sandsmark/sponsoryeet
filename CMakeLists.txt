cmake_minimum_required(VERSION 3.5)
project(sponsoryeet)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(OpenSSL REQUIRED)

find_package(Protobuf REQUIRED)
protobuf_generate_cpp(protobufs_SRC protobufs_HDR
    cast_channel.proto
    )
add_executable(sponsoryeet
    main.cpp
    ${protobufs_SRC}
    ${protobufs_HDR}
    )

target_link_libraries(sponsoryeet protobuf::libprotobuf OpenSSL::SSL)
install(TARGETS sponsoryeet)
