#if !defined(__h_promise__)
#define __h_promise__

#include <functional>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <future>
#include <type_traits>

namespace JPromise2 {
  
template <typename T = void> class Promise;

template <> class Promise<void> {
public:
  template<typename T> static typename Promise<T>::sp create(typename Promise<T>::executor_fn executer) {
    auto p = std::shared_ptr<Promise<T>>(new Promise<T>());
    p->execute(executer);
    return p;
  }
};

template<typename T> struct is_promise_sp : std::false_type {};
template<typename T> struct is_promise_sp<std::shared_ptr<Promise<T>>> : std::true_type {};

template <typename T> class Promise : public std::enable_shared_from_this<Promise<T>> {
friend class Promise<>;
public:
  using self_type   = Promise<T>;
  using sp          = std::shared_ptr<self_type>;
  using value_type  = T;

private:
  using mtx         = std::mutex;
  using guard       = std::lock_guard<mtx>;
  using ulock       = std::unique_lock<mtx>;

public:
  struct resolver {
    sp p_;
    resolver(sp p) : p_(p) {}
    template <typename U> void resolve(U&& value){
      guard lock(p_->mtx_);
      p_->state_ = state::fulfilled;
      p_->value_ = std::forward<U>(value);
      p_->cond_.notify_one();
    }
    void reject(std::exception_ptr err){
      guard lock(p_->mtx_);
      p_->state_ = state::rejected;
      p_->error_ = err;
      p_->cond_.notify_one();
    }
  };
  friend struct resolver;

public:
  using executor_fn = std::function<void(resolver)>;

private:
  enum class state {pending, fulfilled, rejected};
  mtx                     mtx_;
  std::condition_variable cond_;
  state                   state_;
  value_type              value_;
  std::exception_ptr      error_;

  void execute(executor_fn executor) {
    executor(resolver(this->shared_from_this()));
  }

  Promise() : state_(state::pending) {}

public:
  ~Promise() = default;

  template <typename F>
  auto then(F func) -> std::enable_if_t<
    is_promise_sp<decltype(func(value_type{}))>::value
    , decltype(func(value_type{}))
  >
  {
    using PROMISE = typename decltype(func(value_type{}))::element_type;
    using TYPE = typename PROMISE::value_type;
    auto THIS = this->shared_from_this();
    return Promise<>::create<TYPE>([THIS, func](typename PROMISE::resolver resolver){
      ulock lock(THIS->mtx_);
      THIS->cond_.wait(lock, [THIS]{ return THIS->state_ != state::pending; });
      if(THIS->state_ == state::fulfilled) {
        func(std::move(THIS->value_))
        ->then([resolver](TYPE x) mutable {
          resolver.resolve(std::move(x));
        })
        ->error([resolver](std::exception_ptr err) mutable{
           resolver.reject(err);
        });
      }
      else if(THIS->state_ == state::rejected){
        resolver.reject(THIS->error_);
      }
    });
  } 

  template <typename F>
  auto then(F func) -> std::enable_if_t<
    !is_promise_sp<decltype(func(value_type{}))>::value &&
    !std::is_same<decltype(func(value_type{})), void>::value
    , std::shared_ptr<Promise<decltype(func(value_type{}))>>
  >
  {
    using TYPE = decltype(func(value_type{}));
    auto THIS = this->shared_from_this();
    return Promise<>::create<TYPE>([THIS, func](auto resolver){
      ulock lock(THIS->mtx_);
      THIS->cond_.wait(lock, [THIS]{ return THIS->state_ != state::pending; });
      if(THIS->state_ == state::fulfilled) {
        resolver.resolve(func(std::move(THIS->value_)));
      }
      else if(THIS->state_ == state::rejected){
        resolver.reject(THIS->error_);
      }
    });
  } 

  template <typename F>
  auto then(F func) -> std::enable_if_t<
    !is_promise_sp<decltype(func(value_type{}))>::value &&
    std::is_same<decltype(func(value_type{})), void>::value
    , std::shared_ptr<Promise<value_type>>
  >
  {
    auto THIS = this->shared_from_this();
    return Promise<>::create<value_type>([THIS, func](auto resolver) mutable{
      ulock lock(THIS->mtx_);
      THIS->cond_.wait(lock, [THIS]{ return THIS->state_ != state::pending; });
      if(THIS->state_ == state::fulfilled) {
        func(THIS->value_);
        resolver.resolve(std::move(THIS->value_));
      }
      else if(THIS->state_ == state::rejected){
        resolver.reject(THIS->error_);
      }
    });
  } 


  template <typename F>
  auto error(F func) -> std::enable_if_t<
    is_promise_sp<decltype(func(std::exception_ptr{}))>::value
    , decltype(func(std::exception_ptr{}))
  >
  {
    using PROMISE = typename decltype(func(std::exception_ptr{}))::element_type;
    using TYPE = typename PROMISE::value_type;
    auto THIS = this->shared_from_this();
    return Promise<>::create<TYPE>([THIS, func](typename PROMISE::resolver resolver){
      ulock lock(THIS->mtx_);
      THIS->cond_.wait(lock, [THIS]{ return THIS->state_ != state::pending; });
      if(THIS->state_ == state::fulfilled) {
        resolver.resolve(std::move(THIS->value_));
      }
      else if(THIS->state_ == state::rejected){
        func(std::move(THIS->value_))
        ->then([resolver](TYPE x) mutable {
          resolver.resolve(std::move(x));
        })
        ->error([resolver](std::exception_ptr err){
           resolver.reject(err);
        });
      }
    });
  } 

  template <typename F>
  auto error(F func) -> std::enable_if_t<
    !is_promise_sp<decltype(func(std::exception_ptr{}))>::value &&
    !std::is_same<decltype(func(std::exception_ptr{})), void>::value
    , std::shared_ptr<Promise<decltype(func(std::exception_ptr{}))>>
  >
  {
    using TYPE = decltype(func(std::exception_ptr{}));
    auto THIS = this->shared_from_this();
    return Promise<>::create<TYPE>([THIS, func](auto resolver){
      ulock lock(THIS->mtx_);
      THIS->cond_.wait(lock, [THIS]{ return THIS->state_ != state::pending; });
      if(THIS->state_ == state::fulfilled) {
        resolver.resolve(std::move(THIS->value_));
      }
      else if(THIS->state_ == state::rejected){
        resolver.reject(func(THIS->error_));
      }
    });
  } 

  template <typename F>
  auto error(F func) -> std::enable_if_t<
    !is_promise_sp<decltype(func(std::exception_ptr{}))>::value &&
    std::is_same<decltype(func(std::exception_ptr{})), void>::value
    , std::shared_ptr<Promise<value_type>>
  >
  {
    auto THIS = this->shared_from_this();
    return Promise<>::create<value_type>([THIS, func](auto resolver) mutable{
      ulock lock(THIS->mtx_);
      THIS->cond_.wait(lock, [THIS]{ return THIS->state_ != state::pending; });
      if(THIS->state_ == state::fulfilled) {
        resolver.resolve(std::move(THIS->value_));
      }
      else if(THIS->state_ == state::rejected){
        func(THIS->error_);
        resolver.reject(THIS->error_);
      }
    });
  } 
};



} /** namespace JPromise */
#endif /* !defined(__h_promise__) */