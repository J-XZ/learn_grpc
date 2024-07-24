#include "iostream"

#include "xzxz.grpc.pb.h"
#include "xzxz.pb.h"
#include "xzxz_rpc_const_values.h"
#include "xzxz_server.h"
#include <sstream>

int main() {
  std::cout << "Server!" << std::endl;
  std::atomic<int> total_request_count(0);
  static int test = 1;
  // 定义 CallSayHello 和 CallAdd

  AUTO_DEFINE_CALL_HANDLER_WITH_ARG(
      SayHello,
      {
        response_.set_message("Hello " + request_.name());
        (*arg)++;
      },
      std::atomic<int> *, &total_request_count);

  AUTO_DEFINE_CALL_HANDLER_WITH_ARG(
      Add,
      {
        std::stringstream ss;
        ss << "work " << (*arg).fetch_add(1) << " start\n";
        std::cout << ss.str() << std::flush;

        for (int i = 0; i < 3; ++i) {
          std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        response_.set_sum(request_.a() + this->request_.b());

        std::cout << "work end\n" << std::flush;
      },
      std::atomic<int> *, &total_request_count);

  XzxzServer<XzxzServiceTest::AsyncService, CallAdd, CallSayHello> server;
  server.Run(server_address1, 12, {}, "worker", true);

  std::this_thread::sleep_for(std::chrono::seconds(300000));

  return 0;
}