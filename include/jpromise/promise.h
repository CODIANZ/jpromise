#if !defined(__h_promise__)
#define __h_promise__

#include <functional>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <future>
#include <type_traits>
#include <queue>

namespace JPromise {

template <typename T = void> class Promise;

template <> class Promise<void> {
private:
struct never {};

public:
  template <typename T> static typename Promise<T>::sp create(typename Promise<T>::executor_fn executer) {
    auto p = std::shared_ptr<Promise<T>>(new Promise<T>());
    p->execute(executer);
    return p;
  }

  template <typename T, typename TT = typename std::remove_const<typename std::remove_reference<T>::type>::type>
  static typename Promise<TT>::sp resolve(T&& value) {
    auto p = std::shared_ptr<Promise<TT>>(new Promise<TT>());
    p->on_fulfilled(std::forward<T>(value));
    return p;
  }

  template <typename T = struct never>
  static typename Promise<T>::sp reject(std::exception_ptr err) {
    auto p = std::shared_ptr<Promise<T>>(new Promise<T>());
    p->on_rejected(err);
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
  /** treat as mutable for use in lambda functions */
  class resolver {
  friend class Promise<T>;
  private:
    mutable sp p_;
    resolver(sp p) : p_(p) {}
  public:
    template <typename U> void resolve(U&& value) const {
      p_->on_fulfilled(std::forward<U>(value));
    }
    void reject(std::exception_ptr err) const {
      p_->on_rejected(err);
    }
  };
  friend struct resolver;

public:
  using executor_fn = std::function<void(resolver)>;

private:
  enum class state {pending, fulfilled, rejected};
  using on_fulfilled_fn = std::function<void(const value_type&)>;
  using on_rejected_fn  = std::function<void(std::exception_ptr)>;
  using on_finally_fn   = std::function<void()>;

  mtx                     mtx_;
  state                   state_;
  value_type              value_;
  std::exception_ptr      error_;

  std::queue<on_fulfilled_fn> fulfilled_handlers_;
  std::queue<on_rejected_fn>  rejected_handlers_;
  std::queue<on_finally_fn>   finally_handlers_;

  void execute(executor_fn executor) {
    executor(resolver(this->shared_from_this()));
  }

  Promise() : state_(state::pending) {}

  void add_fulfilled_handler(on_fulfilled_fn f){
    const state s = [&](){
      guard lock(mtx_);
      if(state_ == state::pending){
        fulfilled_handlers_.push(f);
      }
      return state_;
    }();
    if(s == state::fulfilled){
      f(value_);
    }
  }

  void add_rejected_handler(on_rejected_fn f){
    const state s = [&](){
      guard lock(mtx_);
      if(state_ == state::pending){
        rejected_handlers_.push(f);
      }
      return state_;
    }();
    if(s == state::rejected){
      f(error_);
    }
  }

  void add_finally_handler(on_finally_fn f){
    const state s = [&](){
      guard lock(mtx_);
      if(state_ == state::pending){
        finally_handlers_.push(f);
      }
      return state_;
    }();
    if(s != state::pending){
      f();
    }
  }

  template<typename U>
  void on_fulfilled(U&& value) {
    {
      guard lock(mtx_);
      state_ = state::fulfilled;
      value_ = std::forward<U>(value);
    }
    /** no lock required from here */
    while(!fulfilled_handlers_.empty()){
      fulfilled_handlers_.front()(value_);
      fulfilled_handlers_.pop();
    }
    while(!finally_handlers_.empty()){
      finally_handlers_.front()();
      finally_handlers_.pop();
    }
    while(!rejected_handlers_.empty()) rejected_handlers_.pop();
  }

  void on_rejected(std::exception_ptr err) {
    {
      guard lock(mtx_);
      state_ = state::rejected;
      error_ = err;
    }
    /** no lock required from here */
    while(!rejected_handlers_.empty()){
      rejected_handlers_.front()(error_);
      rejected_handlers_.pop();
    }
    while(!finally_handlers_.empty()){
      finally_handlers_.front()();
      finally_handlers_.pop();
    }
    while(!fulfilled_handlers_.empty()) fulfilled_handlers_.pop();
  }

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
      THIS->add_fulfilled_handler([THIS, resolver, func](const value_type& value){
        func(value)
        ->then([resolver](const TYPE& x) {
          resolver.resolve(x);
        })
        ->error([resolver](std::exception_ptr err) {
           resolver.reject(err);
        });
      });
      THIS->add_rejected_handler([THIS, resolver](std::exception_ptr err) {
        resolver.reject(err);
      });
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
    using PROMISE = Promise<TYPE>;
    auto THIS = this->shared_from_this();
    return Promise<>::create<TYPE>([THIS, func](typename PROMISE::resolver resolver) {
      THIS->add_fulfilled_handler([THIS, resolver, func](const value_type& value) {
        resolver.resolve(func(value));
      });
      THIS->add_rejected_handler([THIS, resolver](std::exception_ptr err) {
        resolver.reject(err);
      });
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
    return Promise<>::create<value_type>([THIS, func](resolver resolver){
      THIS->add_fulfilled_handler([THIS, resolver, func](const value_type& value) {
        func(value);
        resolver.resolve(value);
      });
      THIS->add_rejected_handler([THIS, resolver](std::exception_ptr err) {
        resolver.reject(err);
      });
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
      THIS->add_fulfilled_handler([THIS, resolver](const value_type& value) {
        resolver.resolve(value);
      });
      THIS->add_rejected_handler([THIS, resolver, func](std::exception_ptr err){
        func(err)
        ->then([resolver](const TYPE& x) {
          resolver.resolve(x);
        })
        ->error([resolver](std::exception_ptr err){
           resolver.reject(err);
        });
      });
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
    using PROMISE = Promise<TYPE>;
    auto THIS = this->shared_from_this();
    return Promise<>::create<TYPE>([THIS, func](typename PROMISE::resolver resolver){
      THIS->add_fulfilled_handler([THIS, resolver](const value_type& value){
        resolver.resolve(value);
      });
      THIS->add_rejected_handler([THIS, resolver, func](std::exception_ptr err){
        resolver.resolve(func(err));
      });
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
    return Promise<>::create<value_type>([THIS, func](resolver resolver) {
      THIS->add_fulfilled_handler([THIS, resolver](const value_type& value) {
        resolver.resolve(value);
      });
      THIS->add_rejected_handler([THIS, resolver, func](std::exception_ptr err) {
        func(err);
        resolver.reject(err);
      });
    });
  }

  template <typename F>
  auto finally(F func) -> std::enable_if_t<
    is_promise_sp<decltype(func())>::value
    , decltype(func())
  >
  {
    using PROMISE = typename decltype(func())::element_type;
    using TYPE = typename PROMISE::value_type;
    auto THIS = this->shared_from_this();
    return Promise<>::create<TYPE>([THIS, func](typename PROMISE::resolver resolver){
      // THIS->add_fulfilled_handler([THIS, resolver](const value_type& value) {
      // });
      // THIS->add_rejected_handler([THIS, resolver](std::exception_ptr err){
      // });
      THIS->add_finally_handler([THIS, resolver, func](){
        func()
        ->then([resolver](const TYPE& x) {
          resolver.resolve(x);
        })
        ->error([resolver](std::exception_ptr err){
           resolver.reject(err);
        });
      });
    });
  } 

  template <typename F>
  auto finally(F func) -> std::enable_if_t<
    !is_promise_sp<decltype(func())>::value &&
    !std::is_same<decltype(func()), void>::value
    , std::shared_ptr<Promise<decltype(func())>>
  >
  {
    using TYPE = decltype(func());
    using PROMISE = Promise<TYPE>;
    auto THIS = this->shared_from_this();
    return Promise<>::create<TYPE>([THIS, func](typename PROMISE::resolver resolver){
      // THIS->add_fulfilled_handler([THIS, resolver](const value_type& value){
      // });
      // THIS->add_rejected_handler([THIS, resolver, func](std::exception_ptr err){
      // });
      THIS->add_finally_handler([THIS, resolver, func](){
        resolver.resolve(func());
      });
    });
  } 

  template <typename F>
  auto finally(F func) -> std::enable_if_t<
    !is_promise_sp<decltype(func())>::value &&
    std::is_same<decltype(func()), void>::value
    , std::shared_ptr<Promise<value_type>>
  >
  {
    auto THIS = this->shared_from_this();
    return Promise<>::create<value_type>([THIS, func](resolver resolver) {
      THIS->add_fulfilled_handler([THIS, resolver](const value_type& value) {
        resolver.resolve(value);
      });
      THIS->add_rejected_handler([THIS, resolver](std::exception_ptr err) {
        resolver.reject(err);
      });
      THIS->add_finally_handler([THIS, resolver, func](){
        func();
      });
    });
  }
};

} /** namespace JPromise */
#endif /* !defined(__h_promise__) */