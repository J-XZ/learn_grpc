#include "iostream"
#include "xzxz.grpc.pb.h"
#include "xzxz.pb.h"
#include "xzxz_client.h"
#include "xzxz_rpc_const_values.h"
#include <chrono>

int main() {
  std::cout << "Client!" << std::endl;

  XzxzClient<XzxzServiceTest> client(std::string(server_address1), 1, {1}, "");

  std::vector<std::unique_ptr<AsyncClientCallBaseWithType<AddReply>>> calls;

  AddArg a;
  a.set_a(1);
  a.set_b(2);

  for (int i = 0; i < 128; i++) {
    calls.emplace_back(XZ_GRPC_ASYNC_CALL((&client), Add, a));
    std::cout << "call" << i << std::endl;
  }

  std::chrono::system_clock::time_point start =
      std::chrono::high_resolution_clock::now();
  for (auto &call : calls) {
    XZ_GRPC_ASYNC_CALL_WAIT_UNTIL_OK((&client), Add, a, call);
    const auto &response = call->GetReplyConstRef();
    std::cout << response.sum() << std::endl;

    std::chrono::system_clock::time_point end =
        std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_seconds = end - start;

    std::cout << "elapsed time: " << elapsed_seconds.count() << "s\n";
  }

  return 0;
}