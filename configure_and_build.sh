#!/usr/bin/bash

rm -rf build
mkdir -p build

pushd build

mkdir -p ../xzxz_proto/proto_out
/root/.local/bin/protoc -I ../xzxz_proto --grpc_out=../xzxz_proto/proto_out --plugin=protoc-gen-grpc=/users/ruixuan/code/grpc_test/grpc/cmake/build/grpc_cpp_plugin ../xzxz_proto/xzxz.proto
/root/.local/bin/protoc -I ../xzxz_proto --cpp_out=../xzxz_proto/proto_out ../xzxz_proto/xzxz.proto

cmake -G Ninja -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Debug ..
cmake --build . --target all -- -j 64

popd
