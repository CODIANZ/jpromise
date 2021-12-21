#if !defined(__h_promise__)
#define __h_promise__

#include <functional>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <future>
#include <type_traits>

struct PromseBase {};

template <typename T = void> class Promise;

template <> class Promise<void> {
public:
  template<typename T> static typename Promise<T>::sp create(typename Promise<T>::executor_fn executor) {
    auto p = std::make_shared<Promise<T>>();
    p->execute(executor);
    return p;
  }

  template<typename T> static typename Promise<T>::sp error(std::exception_ptr err) {
    auto p = std::make_shared<Promise<T>>();
    p->failure_ = err;
    return p;
  }
};

template <typename T, std::enable_if_t<
  std::is_base_of<T, Promise<typename T::value_type>>::value
, bool> = true> struct is_promise {
  static constexpr bool value = true;
  using promise_value = typename T::value_type;
  using promise_type  = Promise<promise_value>;
  using promise_sp    = typename promise_type::sp;
};

template <typename T, std::enable_if_t<
  !std::is_same<T, Promise<typename T::value_type>>::value
, bool> = true> struct is_promise {
  static constexpr bool value = false;
};


template <typename T, std::enable_if_t<
  std::is_same<T, typename is_promise<typename T::element_type>::promise_sp>::value
, bool> = true> struct is_promise_sp : public is_promise<typename T::element_type> {};




template <typename T> class Promise : public PromseBase, public std::enable_shared_from_this<Promise<T>> {
friend class Promise<>;

public:
  using self_type   = Promise<T>;
  using sp          = std::shared_ptr<Promise<T>>;
  using value_type  = T;
  using value_sp    = std::shared_ptr<value_type>;

private:
  using mtx         = std::mutex;
  using guard       = std::lock_guard<mtx>;
  using ulock       = std::unique_lock<mtx>;

  struct resolver {
    Promise::sp p_;
    resolver(Promise::sp p) : p_(p) {}
    template <typename U> void resolve(U&& value){
      guard lock(p_->mtx_);
      p_->success_ = std::make_shared<T>(std::forward<U>(value));
      p_->cond_.notify_one();
    }
    void reject(std::exception_ptr err){
      guard lock(p_->mtx_);
      p_->failure_ = err;
      p_->cond_.notify_one();
    }
  };
  friend struct resolver;

public:
  using executor_fn = std::function<void(resolver)>;

private:
  mtx                     mtx_;
  std::condition_variable cond_;
  value_sp                success_;
  std::exception_ptr      failure_;

  void execute(executor_fn executor) {
    executor(resolver(this->shared_from_this()));
  }

public:
  Promise() = default;
  ~Promise() = default;

  // sp then(std::function<T(value_sp)> func) {
  //   auto THIS = this->shared_from_this();
  //   return Promise<>::create<T>([THIS, func](auto resolver){
  //     ulock lock(THIS->mtx_);
  //     THIS->cond_.wait(lock, [THIS, func]{
  //       return THIS->success_.get() != nullptr || THIS->failure_ == nullptr;
  //     });
  //     if(THIS->success_) {
  //       resolver.resolve(func(THIS->success_));
  //     }
  //     else if(THIS->failure_){
  //       resolver.reject(THIS->failure_);
  //     }
  //   });
  // }

  template <typename F>
  auto then(F func) -> std::enable_if_t<
    std::is_same<
      decltype(func(value_sp{})),
      typename is_promise_sp<decltype(func(value_sp{}))>::promise_sp
    >::value
  , decltype(func(value_sp{}))>
  {
    using RET = decltype(func(value_sp{}));
    using PROM = typename RET::element_type;
    auto THIS = this->shared_from_this();
    return Promise<>::create<T>([THIS, func](auto resolver){
      ulock lock(THIS->mtx_);
      THIS->cond_.wait(lock, [THIS, func]{
        return THIS->success_.get() != nullptr || THIS->failure_ == nullptr;
      });
      if(THIS->success_) {
        resolver.resolve(func(THIS->success_));
      }
      else if(THIS->failure_){
        resolver.reject(THIS->failure_);
      }
    });
  }

  template <typename F>
  auto then(F func) -> std::enable_if_t<
    !std::is_same<
      decltype(func(value_sp{})),
      void
    >::value
  , typename Promise<decltype(func(value_sp{}))>::sp>
  {
    using RET = decltype(func(value_sp{}));
    auto THIS = this->shared_from_this();
    return Promise<>::create<RET>([THIS, func](auto resolver){
      ulock lock(THIS->mtx_);
      THIS->cond_.wait(lock, [THIS, func]{
        return THIS->success_.get() != nullptr || THIS->failure_ == nullptr;
      });
      if(THIS->success_) {
        resolver.resolve(func(THIS->success_));
      }
      else if(THIS->failure_){
        resolver.reject(THIS->failure_);
      }
    });
  }

  template <typename F>
  auto then(F func) -> std::enable_if_t<
    std::is_same<
      decltype(func(value_sp{})),
      void
    >::value
  , typename Promise<decltype(func(value_sp{}))>::sp>
  {
    using RET = decltype(func(value_sp{}));
    auto THIS = this->shared_from_this();
    return Promise<>::create<RET>([THIS, func](auto resolver){
      ulock lock(THIS->mtx_);
      THIS->cond_.wait(lock, [THIS, func]{
        return THIS->success_.get() != nullptr || THIS->failure_ == nullptr;
      });
      if(THIS->success_) {
        resolver.resolve(func(THIS->success_));
      }
      else if(THIS->failure_){
        resolver.reject(THIS->failure_);
      }
    });
  }




  // sp error(std::function<void(std::exception_ptr)> func) {
  //   auto THIS = this->shared_from_this();
  //   return Promise<>::create<T>([THIS, func](auto resolver){
  //     ulock lock(THIS->mtx_);
  //     THIS->cond_.wait(lock, [THIS]{
  //       return THIS->success_.get() != nullptr || THIS->failure_ == nullptr;
  //     });
  //     if(THIS->success_) {
  //       resolver.resolve(THIS->success_);
  //     }
  //     else if(THIS->failure_){
  //       func(THIS->failure_);
  //       resolver.reject(THIS->failure_);
  //     }
  //   });
  // }
};

#endif /* !defined(__h_promise__) */