cmake_minimum_required(VERSION 3.8)

# 指定的 grpc 安装路径
#
# grpc 安装方法见： /users/ruixuan/code/grpc_test/xz_init.sh
set(CMAKE_PREFIX_PATH "/root/.local")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

project(learn_grpc C CXX)
find_package(Threads REQUIRED)
find_package(Protobuf REQUIRED CONFIG)
find_package(gRPC CONFIG REQUIRED)
set(_PROTOBUF_LIBPROTOBUF protobuf::libprotobuf)
set(_REFLECTION gRPC::grpc++_reflection)
set(_GRPC_GRPCPP gRPC::grpc++)

add_executable(server ./test_server/main.cc)
add_executable(client ./test_client/main.cc)

set(grpc_cpp_file_list xzxz_proto/proto_out/xzxz.pb.cc
                       xzxz_proto/proto_out/xzxz.grpc.pb.cc)

foreach(_target server client)
  target_include_directories(
    ${_target} PRIVATE /root/.local/include xzxz_proto/proto_out include
                       test_server test_client)
  target_link_directories(${_target} PRIVATE /root/.local/lib)
  target_link_libraries(${_target} PRIVATE ${_REFLECTION} ${_GRPC_GRPCPP}
                                           ${_PROTOBUF_LIBPROTOBUF})
  target_sources(${_target} PRIVATE ${grpc_cpp_file_list})
endforeach()

include_directories(xzxz_proto/proto_out)
