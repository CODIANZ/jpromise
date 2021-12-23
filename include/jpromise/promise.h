#if !defined(__h_promise__)
#define __h_promise__

#include <functional>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <future>
#include <type_traits>
#include <queue>
#include <unordered_map>

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

class PromiseBase : public std::enable_shared_from_this<PromiseBase> {
public:
  using sp = std::shared_ptr<PromiseBase>;

private:
  const std::vector<PromiseBase::sp> upstream_;
  std::vector<PromiseBase::sp> upstream() {
    auto u = upstream_;
    u.push_back(shared_base()); /** add self instance */
    return std::move(u);
  }

public:
  PromiseBase() = default;
  PromiseBase(PromiseBase::sp source) : upstream_(source->upstream()) {}
  virtual ~PromiseBase() = default;

  sp shared_base() { return shared_from_this(); }
  template <typename T> std::shared_ptr<Promise<T>> shared_this_as() {
    return std::dynamic_pointer_cast<Promise<T>>(shared_from_this());
  }
  PromiseBase::sp above() { return upstream_.size() > 0 ? upstream_.back() : PromiseBase::sp(); }
  virtual void remove_handler(PromiseBase*) = 0;
};

template <typename T> class Promise : public PromiseBase {
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
    mutable std::weak_ptr<Promise<T>> p_;
    resolver(sp p) : p_(p) {}
  public:
    template <typename U> void resolve(U&& value) const {
      auto p = p_.lock();
      if(p){
        p->on_fulfilled(std::forward<U>(value));
      }
    }
    void reject(std::exception_ptr err) const {
      auto p = p_.lock();
      if(p){
        p->on_rejected(err);
      }
    }
  };
  friend struct resolver;

  struct handler {
    std::function<void(const value_type&)>  on_fulfilled;
    std::function<void(std::exception_ptr)> on_rejected;
  };

public:
  using executor_fn = std::function<void(resolver)>;

private:
  enum class state {pending, fulfilled, rejected};

  mtx                     mtx_;
  std::condition_variable cond_;
  state                   state_;
  value_type              value_;
  std::exception_ptr      error_;
  std::unordered_map<PromiseBase*, handler>   handlers_;

  template <typename SINK> typename Promise<SINK>::sp create_sink() {
    auto sink = std::shared_ptr<Promise<SINK>>(new Promise<SINK>(shared_base()));
    return sink;
  }

  sp shared_this() {
    return shared_this_as<T>();
  }

  void add_handler(PromiseBase* base, handler h){
    const state s = [&](){
      guard lock(mtx_);
      if(state_ == state::pending){
        handlers_.insert({base, h});
      }
      return state_;
    }();
    if(s == state::fulfilled){
      if(h.on_fulfilled) h.on_fulfilled(value_);
    }
    else if(s == state::rejected){
      if(h.on_rejected) h.on_rejected(error_);
    }
  }

  bool consume_handler(handler& h) {
    guard lock(mtx_);
    if(handlers_.empty()) return false;
    auto it = handlers_.begin();
    h = it->second;
    handlers_.erase(it);
    return true;
  }

  template<typename U>
  void on_fulfilled(U&& value) {
    {
      guard lock(mtx_);
      assert(state_ == state::pending);
      state_ = state::fulfilled;
      value_ = std::forward<U>(value);
      cond_.notify_all();
    }
    handler h;
    while(consume_handler(h)){
      if(h.on_fulfilled) h.on_fulfilled(value_);
    }
  }

  void on_rejected(std::exception_ptr err) {
    {
      guard lock(mtx_);
      assert(state_ == state::pending);
      state_ = state::rejected;
      error_ = err;
      cond_.notify_all();
    }
    handler h;
    while(consume_handler(h)){
      if(h.on_rejected) h.on_rejected(err);
    }
  }

public:
  Promise() : state_(state::pending) {}
  Promise(PromiseBase::sp source) :
    state_(state::pending),
    PromiseBase(source)
  {}
  ~Promise(){
    auto source = above();
    if(source){
      source->remove_handler(this);
    }
  };

  /** internal use */
  virtual void remove_handler(PromiseBase* inst){
    guard lock(mtx_);
    handlers_.erase(inst);
  }

  /** internal use */
  void execute(executor_fn executor) {
    try{
      executor(resolver(shared_this()));
    }
    catch(...){
      on_rejected(std::current_exception());
    }
  }

  const value_type& wait() {
    std::mutex mtx;
    std::unique_lock<std::mutex> lock(mtx);
    auto THIS = shared_this();
    cond_.wait(lock, [THIS]{ return THIS->state_ != state::pending; });
    if(state_ == state::rejected) std::rethrow_exception(error_);
    return value_; 
  }

  template <typename F>
  auto then(F func) -> std::enable_if_t<
    is_promise_sp<decltype(func(value_type{}))>::value
    , decltype(func(value_type{}))
  >
  {
    using PROMISE = typename decltype(func(value_type{}))::element_type;
    using TYPE = typename PROMISE::value_type;
    auto THIS = shared_this();
    auto sink = create_sink<TYPE>();
    sink->execute([THIS, sink, func](typename PROMISE::resolver resolver){
      THIS->add_handler(sink.get(), {
        .on_fulfilled = [resolver, func](const value_type& value){
          try{
            resolver.resolve(func(value)->wait());
          }
          catch(...){
            resolver.reject(std::current_exception());
          }
        },
        .on_rejected = [resolver](std::exception_ptr err) {
          resolver.reject(err);
        }
      });
    });
    return sink;
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
    auto THIS = shared_this();
    auto sink = create_sink<TYPE>();
    sink->execute([THIS, sink, func](typename PROMISE::resolver resolver) {
      THIS->add_handler(sink.get(), {
        .on_fulfilled = [resolver, func](const value_type& value) {
          resolver.resolve(func(value));
        },
        .on_rejected = [resolver](std::exception_ptr err) {
          resolver.reject(err);
        }
      });
    });
    return sink;
  } 

  template <typename F>
  auto then(F func) -> std::enable_if_t<
    !is_promise_sp<decltype(func(value_type{}))>::value &&
    std::is_same<decltype(func(value_type{})), void>::value
    , std::shared_ptr<Promise<value_type>>
  >
  {
    auto THIS = shared_this();
    auto sink = create_sink<value_type>();
    sink->execute([THIS, sink, func](resolver resolver){
      THIS->add_handler(sink.get(), {
        .on_fulfilled = [resolver, func](const value_type& value) {
          func(value);
          resolver.resolve(value);
        },
        .on_rejected = [resolver](std::exception_ptr err) {
          resolver.reject(err);
        }
      });
    });
    return sink;
  } 

  template <typename F>
  auto error(F func) -> std::enable_if_t<
    is_promise_sp<decltype(func(std::exception_ptr{}))>::value
    , decltype(func(std::exception_ptr{}))
  >
  {
    using PROMISE = typename decltype(func(std::exception_ptr{}))::element_type;
    using TYPE = typename PROMISE::value_type;
    auto THIS = shared_this();
    auto sink = create_sink<TYPE>();
    sink->execute([THIS, sink, func](typename PROMISE::resolver resolver){
      THIS->add_handler(sink.get(), {
        .on_fulfilled = [resolver](const value_type& value) {
          resolver.resolve(value);
        },
        .on_rejected = [resolver, func](std::exception_ptr err){
          try{
            resolver.resolve(func(err)->wait());
          }
          catch(...){
            resolver.reject(std::current_exception());
          }
        }
      });
    });
    return sink;
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
    auto THIS = shared_this();
    auto sink = create_sink<TYPE>();
    sink->execute([THIS, sink, func](typename PROMISE::resolver resolver){
      THIS->add_handler(sink.get(), {
        .on_fulfilled = [resolver](const value_type& value){
          resolver.resolve(value);
        },
        .on_rejected = [resolver, func](std::exception_ptr err){
          resolver.resolve(func(err));
        }
      });
    });
    return sink;
  } 

  template <typename F>
  auto error(F func) -> std::enable_if_t<
    !is_promise_sp<decltype(func(std::exception_ptr{}))>::value &&
    std::is_same<decltype(func(std::exception_ptr{})), void>::value
    , std::shared_ptr<Promise<value_type>>
  >
  {
    auto THIS = shared_this();
    auto sink = create_sink<value_type>();
    sink->execute([THIS, sink, func](resolver resolver) {
      THIS->add_handler(sink.get(), {
        .on_fulfilled = [resolver](const value_type& value) {
          resolver.resolve(value);
        },
        .on_rejected = [resolver, func](std::exception_ptr err) {
          func(err);
          resolver.reject(err);
        }
      });
    });
    return sink;
  }

  template <typename F>
  auto finally(F func) -> std::enable_if_t<
    is_promise_sp<decltype(func())>::value
    , decltype(func())
  >
  {
    using PROMISE = typename decltype(func())::element_type;
    using TYPE = typename PROMISE::value_type;
    auto THIS = shared_this();
    auto sink = create_sink<TYPE>();
    sink->execute([THIS, sink, func](typename PROMISE::resolver resolver){
      auto fn = [resolver, func](){
        try{
          resolver.resolve(func()->wait());
        }
        catch(...){
          resolver.reject(std::current_exception());
        }
      };
      THIS->add_handler(sink.get(), {
        .on_fulfilled = [fn](const value_type&){ fn(); },
        .on_rejected = [fn](std::exception_ptr){ fn(); },
      });
    });
    return sink;
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
    auto THIS = shared_this();
    auto sink = create_sink<TYPE>();
    sink->execute([THIS, sink, func](typename PROMISE::resolver resolver){
      THIS->add_handler(sink.get(), {
        .on_fulfilled = [resolver, func](const value_type&){
          resolver.resolve(func());
        },
        .on_rejected = [resolver, func](std::exception_ptr){
          resolver.resolve(func());
        }
      });
    });
    return sink;
  } 

  template <typename F>
  auto finally(F func) -> std::enable_if_t<
    !is_promise_sp<decltype(func())>::value &&
    std::is_same<decltype(func()), void>::value
    , std::shared_ptr<Promise<value_type>>
  >
  {
    auto THIS = shared_this();
    auto sink = create_sink<value_type>();
    sink->execute([THIS, sink, func](resolver resolver) {
      THIS->add_handler(sink.get(), {
        .on_fulfilled = [resolver, func](const value_type& value){
          func();
          resolver.resolve(value);
        },
        .on_rejected = [resolver, func](std::exception_ptr err){
          func();
          resolver.reject(err);
        }
      });
    });
    return sink;
  }
};

} /** namespace JPromise */
#endif /* !defined(__h_promise__) */