#pragma once

#include <condition_variable>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/async_unary_call.h>
#include <memory>
#include <thread>

class AsyncClientCallBase {
public:
  virtual ~AsyncClientCallBase() = default;
  virtual void CallBack() {};

  inline auto Done() {
    {
      std::lock_guard<std::mutex> lock{mu_};
      is_done_ = true;
    }
    cv_.notify_one();
  }

  inline auto Wait() {
    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait(lock, [this] { return is_done_; });
  }

private:
  bool is_done_{false};
  std::mutex mu_;
  std::condition_variable cv_;
};

template <typename ReplyType>
class AsyncClientCallBaseWithType : public AsyncClientCallBase {
public:
  const ReplyType &GetReplyConstRef() { return rep_.reply; }
  void GetReplyMove(ReplyType &r) { r = std::move(rep_.reply); }
  grpc::Status *GetStatus() { return &rep_.status; }

  struct AsyncClientCallContextData {
    ReplyType reply;
    grpc::ClientContext context;
    grpc::Status status;
    std::unique_ptr<grpc::ClientAsyncResponseReader<ReplyType>> response_reader;
    void (*call_back_fn)(void *) = nullptr;
    void *call_back_arg = nullptr;
  } rep_;
};

template <typename ServiceType> class XzxzClient final {
public:
  explicit XzxzClient(const std::string &address, int worker_thread_cnt = 0,
                      std::vector<int> core_list = {},
                      std::string worker_thread_name = "") {
    grpc::ChannelArguments channel_args;
    channel_args.SetInt(GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS, 10000); // 10秒
    channel_args.SetInt(GRPC_ARG_MAX_RECONNECT_BACKOFF_MS, 100000);    // 100秒
    channel_ = grpc::CreateCustomChannel(
        address, grpc::InsecureChannelCredentials(), channel_args);
    stub_ = ServiceType::NewStub(channel_);

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
      worker_thread_name = "r_client_worker";
    }

    for (int i = 0; i < worker_thread_cnt; i++) {
      completion_queues_.push_back(std::make_shared<grpc::CompletionQueue>());
      auto &cq = completion_queues_.back();
      workers_.push_back(std::make_unique<std::thread>([this, i, &core_list,
                                                        &cq]() {
        if (core_list[i] != -1) {
          cpu_set_t cpuset;
          CPU_ZERO(&cpuset);
          CPU_SET(core_list[i], &cpuset);
          pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        }
        AsyncCompleteRpc(cq);
      }));
    }
  }

  ~XzxzClient() {
    for (auto &cq : completion_queues_) {
      cq->Shutdown();
    }
    for (auto &worker : workers_) {
      worker->join();
    }
  }

  template <typename RequestType, typename ReplyType, typename CallFn>
  std::unique_ptr<AsyncClientCallBaseWithType<ReplyType>>
  CallAsync(CallFn call_fn, const RequestType &request,
            void (*call_back_fn)(void *) = nullptr,
            void *call_back_arg = nullptr) {
    class AsyncClientCall : public AsyncClientCallBaseWithType<ReplyType> {
    public:
      void CallBack() override {
        if (AsyncClientCallBaseWithType<ReplyType>::rep_.call_back_fn !=
            nullptr) {
          (*AsyncClientCallBaseWithType<ReplyType>::rep_.call_back_fn)(
              AsyncClientCallBaseWithType<ReplyType>::rep_.call_back_arg);
        }
        AsyncClientCallBase::Done();
      }

      AsyncClientCall(void (*call_back_fn)(void *), void *call_back_arg,
                      const RequestType &request,
                      typename ServiceType::Stub *stub, CallFn call_fn,
                      std::atomic<int> &which_cq_,
                      std::vector<std::shared_ptr<grpc::CompletionQueue>>
                          &completion_queues_) {
        auto current_which_cq = which_cq_++ % completion_queues_.size();
        AsyncClientCallBaseWithType<ReplyType>::rep_.call_back_fn =
            call_back_fn;
        AsyncClientCallBaseWithType<ReplyType>::rep_.call_back_arg =
            call_back_arg;
        AsyncClientCallBaseWithType<ReplyType>::rep_.response_reader =
            (stub->*call_fn)(
                &AsyncClientCallBaseWithType<ReplyType>::rep_.context, request,
                completion_queues_[current_which_cq].get());
        AsyncClientCallBaseWithType<ReplyType>::rep_.response_reader
            ->StartCall();
        AsyncClientCallBaseWithType<ReplyType>::rep_.response_reader->Finish(
            &AsyncClientCallBaseWithType<ReplyType>::rep_.reply,
            &AsyncClientCallBaseWithType<ReplyType>::rep_.status, this);
      }

    private:
    };

    return std::make_unique<AsyncClientCall>(call_back_fn, call_back_arg,
                                             request, stub_.get(), call_fn,
                                             which_cq_, completion_queues_);
  }

  void AsyncCompleteRpc(const std::shared_ptr<grpc::CompletionQueue> &cq) {
    void *tag = nullptr;
    bool ok = false;
    auto *cq_ptr = cq.get();
    while (cq_ptr->Next(&tag, &ok)) {
      auto *async_call = static_cast<AsyncClientCallBase *>(tag);
      async_call->CallBack();
    }
  }

private:
  std::shared_ptr<grpc::Channel> channel_;
  std::unique_ptr<typename ServiceType::Stub> stub_;
  std::vector<std::shared_ptr<grpc::CompletionQueue>> completion_queues_;
  std::vector<std::unique_ptr<std::thread>> workers_;
  std::atomic<int> which_cq_{0};
};

#define XZ_GRPC_ASYNC_CALL(client, func, arg)                                  \
  client->CallAsync<func##Arg, func##Reply>(                                   \
      &XzxzServiceTest::Stub::PrepareAsync##func, arg)

#define XZ_GRPC_SYNC_CALL(client, func, arg)                                   \
  [&]() {                                                                      \
    auto ret = (client)->CallAsync<func##Arg, func##Reply>(                    \
        &XzxzServiceTest::Stub::PrepareAsync##func, arg);                      \
    ret->Wait();                                                               \
    while (!ret->GetStatus()->ok()) {                                          \
      ret = (client)->CallAsync<func##Arg, func##Reply>(                       \
          &XzxzServiceTest::Stub::PrepareAsync##func, arg);                    \
      ret->Wait();                                                             \
    }                                                                          \
    return ret;                                                                \
  }()

#define XZ_GRPC_ASYNC_CALL_WAIT_UNTIL_OK(client, func, arg, ret)               \
  [&]() {                                                                      \
    ret->Wait();                                                               \
    while (!ret->GetStatus()->ok()) {                                          \
      ret = (client)->CallAsync<func##Arg, func##Reply>(                       \
          &XzxzServiceTest::Stub::PrepareAsync##func, arg);                    \
      ret->Wait();                                                             \
    }                                                                          \
  }()
