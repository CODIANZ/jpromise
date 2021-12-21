#if !defined(__h_promise__)
#define __h_promise__

#include <functional>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <future>
#include <type_traits>

namespace JPromise {

struct PromiseBase {};

template <typename T> struct is_promise {
  static constexpr bool value = std::is_base_of<PromiseBase, T>::value;
};

template <typename T> class Promise : public PromiseBase {
public:
  using self_type   = Promise<T>;
  using value_type  = T;

private:
  using mtx         = std::mutex;
  using guard       = std::lock_guard<mtx>;
  using ulock       = std::unique_lock<mtx>;

public:
  struct resolver {
    Promise& p_;
    resolver(Promise& p) : p_(p) {}
    template <typename U> void resolve(U&& value){
      guard lock(p_.mtx_);
      p_.state_ = state::fulfilled;
      p_.value_ = std::forward<U>(value);
      p_.cond_.notify_one();
    }
    void reject(std::exception_ptr err){
      guard lock(p_.mtx_);
      p_.state_ = state::rejected;
      p_.error_ = err;
      p_.cond_.notify_one();
    }
  };
  friend struct resolver;

public:
  using executor_fn = std::function<void(resolver)>;

private:
  enum class state {pending, fulfilled, rejected, destroyed};
  mtx                     mtx_;
  std::condition_variable cond_;
  state                   state_;
  value_type              value_;
  std::exception_ptr      error_;

public:
  Promise() : state_(state::pending) {}

  Promise(executor_fn executor) : state_(state::pending) {
    executor(resolver(*this));
  }

  Promise(Promise&& src) :
    state_(src.state_),
    value_(std::move(src.value_)),
    error_(src.error_)
  {
    src.state_ = state::destroyed;
  }

  Promise(const Promise& src) :
    state_(src.state_),
    value_(src.value_),
    error_(src.error_)
  {}

  Promise& operator = (Promise&& src) {
    state_ = src.state_;
    value_ = std::move(src.value_);
    error_ = std::move(src.error_);
    src.state_ = state::destroyed;
    return *this;
  }

  Promise& operator = (const Promise& src) {
    state_ = src.state_;
    value_ = src.value_;
    error_ = src.error_;
    return *this;
  }

  ~Promise() = default;

  template <typename F>
  auto then(F func) -> std::enable_if_t<
    is_promise<decltype(func(value_type{}))>::value
  , decltype(func(value_type{}))>
  {
    using PROMISE = decltype(func(value_type{}));
    return PROMISE{[&](typename PROMISE::resolver resolver){
      ulock lock(mtx_);
      cond_.wait(lock, [&]{ return state_ != state::pending; });
      if(state_ == state::fulfilled) {
        func(std::move(value_))
        .then([&](typename PROMISE::value_type x){
          resolver.resolve(std::move(x));
        })
        .error([&](std::exception_ptr err){
          resolver.reject(err);
        });
      }
      else if(state_ == state::rejected){
        resolver.reject(error_);
      }
    }};
  }

  template <typename F>
  auto then(F func) -> std::enable_if_t<
    !is_promise<decltype(func(value_type{}))>::value &&
    !std::is_same<decltype(func(value_type{})), void>::value
  , Promise<decltype(func(value_type{}))>>
  {
    using PROMISE = Promise<decltype(func(value_type{}))>;
    return PROMISE([&](auto resolver){
      ulock lock(mtx_);
      cond_.wait(lock, [&]{ return state_ != state::pending; });
      if(state_ == state::fulfilled) {
        resolver.resolve(func(std::move(value_)));
      }
      else if(state_ == state::rejected){
        resolver.reject(error_);
      }
    });
  }

  template <typename F>
  auto then(F func) -> std::enable_if_t<
    !is_promise<decltype(func(value_type{}))>::value &&
    std::is_same<decltype(func(value_type{})), void>::value
  , Promise<T>>
  {
    using PROMISE = Promise<T>;
    return PROMISE([&](auto resolver){
      ulock lock(mtx_);
      cond_.wait(lock, [&]{ return state_ != state::pending; });
      if(state_ == state::fulfilled) {
        func(value_);
        resolver.resolve(std::move(value_));
      }
      else if(state_ == state::rejected){
        resolver.reject(error_);
      }
    });
  }

  template <typename F>
  auto error(F func) -> std::enable_if_t<
    is_promise<decltype(func(std::exception_ptr{}))>::value
  , decltype(func(std::exception_ptr{}))>
  {
    using PROMISE = decltype(func(std::exception_ptr{}));
    return PROMISE{[&](typename PROMISE::resolver resolver){
      ulock lock(mtx_);
      cond_.wait(lock, [&]{ return state_ != state::pending; });
      if(state_ == state::fulfilled) {
        resolver.resolve(std::move(value_));
      }
      else if(state_ == state::rejected){
        func(value_)
        .then([&](typename PROMISE::value_type x){
          resolver.resolve(std::move(x));
          return 0;
        })
        .error([&](auto err){
          resolver.reject(err);
        });
      }
    }};
  }

  template <typename F>
  auto error(F func) -> std::enable_if_t<
    !is_promise<decltype(func(std::exception_ptr{}))>::value &&
    !std::is_same<decltype(func(std::exception_ptr{})), void>::value
  , Promise<decltype(func(std::exception_ptr{}))>>
  {
    using PROMISE = Promise<decltype(func(std::exception_ptr{}))>;
    return PROMISE([&](auto resolver){
      ulock lock(mtx_);
      cond_.wait(lock, [&]{ return state_ != state::pending; });
      if(state_ == state::fulfilled) {
        resolver.resolve(std::move(value_));
      }
      else if(state_ == state::rejected){
        resolver.reject(func(error_));
      }
    });
  }

  template <typename F>
  auto error(F func) -> std::enable_if_t<
    !is_promise<decltype(func(std::exception_ptr{}))>::value &&
    std::is_same<decltype(func(std::exception_ptr{})), void>::value
  , Promise<T>>
  {
    using PROMISE = Promise<T>;
    return PROMISE([&](auto resolver){
      ulock lock(mtx_);
      cond_.wait(lock, [&]{ return state_ != state::pending; });
      if(state_ == state::fulfilled) {
        resolver.resolve(std::move(value_));
      }
      else if(state_ == state::rejected){
        func(error_);
        resolver.reject(error_);
      }
    });
  }
};

} /** namespace JPromise */
#endif /* !defined(__h_promise__) */