#pragma once

#include <google/protobuf/message.h>
#include <grpcpp/completion_queue.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/impl/codegen/completion_queue.h>
#include <grpcpp/impl/codegen/server_context.h>
#include <grpcpp/impl/codegen/status.h>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>

class CallData {
public:
  inline CallData(grpc::ServerCompletionQueue *cq) : cq_(cq){};

  inline void SetFinish() { status_ = kFinish; }

  virtual ~CallData() = default;
  virtual void Proceed() = 0;

protected:
  enum CallStatus { kCreate, kProcess, kFinish };
  std::atomic<CallStatus> status_{kCreate}; // NOLINT

  grpc::ServerContext ctx_;         // NOLINT
  grpc::ServerCompletionQueue *cq_; // NOLINT
};

template <typename Derived, typename RequestType, typename ResponseType,
          typename ServiceName, typename ServiceType>
class CallHandler : public CallData {
public:
  CallHandler(ServiceType *service, grpc::ServerCompletionQueue *cq,
              void (ServiceType::*rpcMethod)(
                  ::grpc::ServerContext *, RequestType *,
                  ::grpc::ServerAsyncResponseWriter<ResponseType> *,
                  ::grpc::CompletionQueue *, ::grpc::ServerCompletionQueue *,
                  void *))
      : CallData(cq), service_(service), responder_(&ctx_),
        rpcMethod_(rpcMethod) {
    Proceed();
  }

  void Proceed() override {
    if (status_ == kCreate) {
      status_ = kProcess;
      (service_->*rpcMethod_)(&ctx_, &request_, &responder_, cq_, cq_, this);
    } else if (status_ == kProcess) {
      status_ = kFinish;
      new Derived(service_, cq_);
      static_cast<Derived *>(this)->DoRpc_();
      responder_.Finish(response_, grpc::Status::OK, this);
    } else {
      delete this;
    }
  }

protected:
  RequestType request_;                                     // NOLINT
  ResponseType response_;                                   // NOLINT
  grpc::ServerAsyncResponseWriter<ResponseType> responder_; // NOLINT
  ServiceType *service_;                                    // NOLINT
  void (ServiceType::*rpcMethod_)(
      ::grpc::ServerContext *, RequestType *,
      ::grpc::ServerAsyncResponseWriter<ResponseType> *,
      ::grpc::CompletionQueue *, ::grpc::ServerCompletionQueue *,
      void *); // NOLINT
};

template <typename ServerType, typename... Args> class XzxzServer final {
public:
  XzxzServer() {
    static_assert(sizeof...(Args) > 0, "Args must have at least one element");
  }

  ~XzxzServer() {
    server_->Shutdown();

    for (auto &cq : completion_queues_) {
      cq->Shutdown();
    }

    for (auto &worker : workers_) {
      worker->join();
    }
  }

  // 最后一个参数shared_completion_queue表示是否使用共享的CompletionQueue
  // 如果是true，那么所有的工作线程都会使用同一个 CompletionQueue
  // 如果是false，那么每个工作线程都会有自己的 CompletionQueue
  // 要避免死锁，建议使用共享的 CompletionQueue
  // 确定不会死锁的情况下，可以使用独立的 CompletionQueue，这样可以提高并发度
  void Run(const std::string &server_address, int worker_thread_cnt = 0,
           std::vector<int> core_list = {}, std::string worker_thread_name = "",
           bool shared_completion_queue = false) {
    if (worker_thread_cnt == 0) {
      worker_thread_cnt = 1;
    }

    if (core_list.size() != worker_thread_cnt) {
      core_list.resize(worker_thread_cnt);
      for (auto &core : core_list) {
        core = -1;
      }
    }

    if (worker_thread_name.empty()) {
      worker_thread_name = "r_server_worker";
    }

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service_);

    if (!shared_completion_queue) {
      completion_queues_.reserve(worker_thread_cnt);
      for (int i = 0; i < worker_thread_cnt; i++) {
        completion_queues_.emplace_back(builder.AddCompletionQueue());
      }
    } else {
      completion_queues_.emplace_back(builder.AddCompletionQueue());
    }
    server_ = builder.BuildAndStart();

    {
      std::stringstream ss;
      ss << "server listening on " << server_address << "\n";
      std::cout << ss.str() << std::flush;
    }

    for (int i = 0; i < worker_thread_cnt; i++) {
      workers_.emplace_back(std::make_unique<std::thread>(
          &XzxzServer::HandleRpcs_, this,
          completion_queues_[i % completion_queues_.size()], core_list[i],
          worker_thread_name));
    }
  }

private:
  void HandleRpcs_(const std::shared_ptr<grpc::ServerCompletionQueue> &cq,
                   int core, const std::string &worker_thread_name) {
    if (core >= 0) {
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);
      CPU_SET(core, &cpuset);
      pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    }
    pthread_setname_np(pthread_self(), worker_thread_name.c_str());

    auto *cq_ptr = cq.get();

    ((new Args(&service_, cq_ptr)), ...);

    void *tag = nullptr;
    bool ok = false;
    while (cq_ptr->Next(&tag, &ok)) {
      auto *t = static_cast<CallData *>(tag);
      if (!ok) {
        t->SetFinish();
      }
      t->Proceed();
    }
  }

  std::vector<std::shared_ptr<grpc::ServerCompletionQueue>> completion_queues_;
  std::shared_ptr<grpc::Server> server_;
  ServerType service_;

  std::vector<std::unique_ptr<std::thread>> workers_;
};

#define DEFINE_CALL_HANDLER(CLASS_NAME, ARG_TYPE, REPLY_TYPE, SERVICE_TYPE,    \
                            REQUEST_FUNC, IMPLEMENTATION)                      \
  class CLASS_NAME                                                             \
      : public CallHandler<CLASS_NAME, ARG_TYPE, REPLY_TYPE, SERVICE_TYPE,     \
                           SERVICE_TYPE::AsyncService> {                       \
  public:                                                                      \
    CLASS_NAME(SERVICE_TYPE::AsyncService *service,                            \
               grpc::ServerCompletionQueue *cq)                                \
        : CallHandler(service, cq,                                             \
                      &SERVICE_TYPE::AsyncService::REQUEST_FUNC) {}            \
                                                                               \
    void DoRpc_() { IMPLEMENTATION }                                           \
  };

#define DEFINE_CALL_HANDLER_WITH_ARG(CLASS_NAME, ARG_TYPE, REPLY_TYPE,         \
                                     SERVICE_TYPE, REQUEST_FUNC,               \
                                     IMPLEMENTATION, ACTION)                   \
  class CLASS_NAME                                                             \
      : public CallHandler<CLASS_NAME, ARG_TYPE, REPLY_TYPE, SERVICE_TYPE,     \
                           SERVICE_TYPE::AsyncService> {                       \
  public:                                                                      \
    CLASS_NAME(SERVICE_TYPE::AsyncService *service,                            \
               grpc::ServerCompletionQueue *cq)                                \
        : CallHandler(service, cq,                                             \
                      &SERVICE_TYPE::AsyncService::REQUEST_FUNC) {}            \
                                                                               \
    void DoRpc_() {                                                            \
      auto *arg = ACTION##_Arg_Storage;                                        \
      IMPLEMENTATION                                                           \
    }                                                                          \
  };

#define CALL_ARG_STORAGE(TYPE, ACTION) static TYPE ACTION##_Arg_Storage;

// 新宏，自动拼接关键字生成类定义和实例化
#define AUTO_DEFINE_CALL_HANDLER_WITH_ARG(ACTION, IMPLEMENTATION, TYPE, ARG)   \
  CALL_ARG_STORAGE(TYPE, ACTION);                                              \
  DEFINE_CALL_HANDLER_WITH_ARG(Call##ACTION, ACTION##Arg, ACTION##Reply,       \
                               XzxzServiceTest, Request##ACTION,               \
                               IMPLEMENTATION, ACTION);                        \
  ACTION##_Arg_Storage = ARG

#define AUTO_DEFINE_CALL_HANDLER(ACTION, IMPLEMENTATION)                       \
  DEFINE_CALL_HANDLER(Call##ACTION, ACTION##Arg, ACTION##Reply,                \
                      XzxzServiceTest, Request##ACTION, IMPLEMENTATION)
