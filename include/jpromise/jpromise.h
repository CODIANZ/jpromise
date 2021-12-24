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

  /** strip const and reference */
  template <typename T> struct strip_const_referece {
    using type = typename std::remove_const<typename std::remove_reference<T>::type>::type;
  };

  /** fetch value type in Promise::sp */
  template<typename T> struct promise_sp_value_type {};
  template<typename T> struct promise_sp_value_type<std::shared_ptr<Promise<T>>> {
    using type = typename strip_const_referece<T>::type;
  };

  /** make tuple from Promise::sp parameteres */
  template <typename ...> struct make_value_tuple_impl;

  template <typename ...TUPLE_ELEMENTS, typename PROMISE_SP, typename ...ARGS>
    struct make_value_tuple_impl<std::tuple<TUPLE_ELEMENTS...>, PROMISE_SP, ARGS...>
  {
    using type = typename make_value_tuple_impl<
      std::tuple<
        TUPLE_ELEMENTS...,
        typename promise_sp_value_type<PROMISE_SP>::type
      >,
      ARGS...
    >::type;
  };

  template <typename ...TUPLE_ELEMENTS, typename PROMISE_SP>
  struct make_value_tuple_impl<std::tuple<TUPLE_ELEMENTS...>, PROMISE_SP>
  {
    using type = std::tuple<
      TUPLE_ELEMENTS...,
      typename promise_sp_value_type<PROMISE_SP>::type
    >;
  };

  template <typename ...ARGS>
  struct make_value_tuple
  {
    using type = typename make_value_tuple_impl<
      std::tuple<>,
      ARGS...
    >::type;
  };

public:
  template <typename T> static typename Promise<T>::sp create(typename Promise<T>::executor_fn executer) {
    auto p = std::shared_ptr<Promise<T>>(new Promise<T>());
    p->execute(executer);
    return p;
  }

  template <typename T, typename TT = typename strip_const_referece<T>::type>
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

private:
  /** implementation for the `all()` */
  template <typename RESOLVER, typename TUPLE, typename PROMISE_SP, typename ...ARGS>
  static void all_impl(RESOLVER r, TUPLE t, PROMISE_SP p, ARGS...args) {
    p->stand_alone({
      .on_fulfilled = [=](const auto& x){
        all_impl(r, std::tuple_cat(t, std::forward_as_tuple(x)), args...);
      },
      .on_rejected = [=](std::exception_ptr e){
        r.reject(e);
      }
    });
  }

  template <typename RESOLVER, typename TUPLE, typename PROMISE_SP>
  static void all_impl(RESOLVER r, TUPLE t, PROMISE_SP p) {
    p->stand_alone({
      .on_fulfilled = [=](const auto& x){
        r.resolve(std::tuple_cat(t, std::forward_as_tuple(x)));
      },
      .on_rejected = [=](std::exception_ptr e){
        r.reject(e);
      }
    });
  }

public:
  template <typename ...ARGS>
  static auto all(ARGS...args) -> typename Promise<typename make_value_tuple<ARGS...>::type>::sp {
    using TUPLE_TYPE = typename make_value_tuple<ARGS...>::type;
    return Promise<>::create<TUPLE_TYPE>([=](auto resolver){
      all_impl(resolver, std::tuple<>(), args...);
    });
  }

  template <typename PROMISE_SP, typename VALUE_TYPE = typename promise_sp_value_type<PROMISE_SP>::type>
  static auto all(std::initializer_list<PROMISE_SP> list) -> typename Promise<std::vector<VALUE_TYPE>>::sp {
    return all<VALUE_TYPE>(std::begin(list), std::end(list));
  }

  template <typename VALUE_TYPE, typename ITER>
  static auto all(ITER it_begin, ITER it_end) -> typename Promise<std::vector<VALUE_TYPE>>::sp {
    return Promise::create<std::vector<VALUE_TYPE>>([it_begin, it_end](auto resolver){
      auto results = std::make_shared<std::vector<VALUE_TYPE>>(it_end - it_begin);
      auto mtx = std::make_shared<std::mutex>();
      auto bEmitted = std::make_shared<bool>(false);
      auto nFulfilled = std::make_shared<int>(0);

      auto i = 0;
      for(auto it = it_begin; it != it_end; it++, i++){
        (*it)->stand_alone({
          .on_fulfilled = [resolver, i, results, mtx, bEmitted, nFulfilled](const auto& x){
            auto bExecute = false;
            {
              std::lock_guard<std::mutex> lock(*mtx);
              (*results)[i] = x;
              if(!(*bEmitted)){
                (*nFulfilled)++;
                if((*nFulfilled) == results->size()){
                  *bEmitted = true;
                  bExecute = true;
                }
              }
            }
            if(bExecute) resolver.resolve(std::move(*results));
          },
          .on_rejected = [resolver, mtx, bEmitted](std::exception_ptr e){
            auto bExecute = false;
            {
              std::lock_guard<std::mutex> lock(*mtx);
              if(!(*bEmitted)){
                *bEmitted = true;
                bExecute = true;
              }
            }
            if(bExecute) resolver.reject(e);
          }
        });
      }
    });
  }

  template <typename PROMISE_SP, typename VALUE_TYPE = typename promise_sp_value_type<PROMISE_SP>::type>
  static auto race(std::initializer_list<PROMISE_SP> list) -> typename Promise<VALUE_TYPE>::sp {
    return race<VALUE_TYPE>(std::begin(list), std::end(list));
  }

  template <typename VALUE_TYPE, typename ITER>
  static auto race(ITER it_begin, ITER it_end) -> typename Promise<VALUE_TYPE>::sp {
    return Promise::create<VALUE_TYPE>([it_begin, it_end](auto resolver){
      auto mtx = std::make_shared<std::mutex>();
      auto bEmitted = std::make_shared<bool>(false);

      for(auto it = it_begin; it != it_end; it++){
        (*it)->stand_alone({
          .on_fulfilled = [resolver, mtx, bEmitted](const auto& x){
            auto bExecute = false;
            {
              std::lock_guard<std::mutex> lock(*mtx);
              if(!(*bEmitted)){
                *bEmitted = true;
                bExecute = true;
              }
            }
            if(bExecute) resolver.resolve(x);
          },
          .on_rejected = [resolver, mtx, bEmitted](std::exception_ptr e){
            auto bExecute = false;
            {
              std::lock_guard<std::mutex> lock(*mtx);
              if(!(*bEmitted)){
                *bEmitted = true;
                bExecute = true;
              }
            }
            if(bExecute) resolver.reject(e);
          }
        });
      }
    });
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
  PromiseBase::sp above() { return upstream_.size() > 0 ? upstream_.back() : PromiseBase::sp(); }

protected:
  enum class state {pending, fulfilled, rejected};

  using mtx         = std::mutex;
  using guard       = std::lock_guard<mtx>;
  using ulock       = std::unique_lock<mtx>;

  mtx                     mtx_;
  std::condition_variable cond_;
  state                   state_ = state::pending;
  std::exception_ptr      error_ = nullptr;

  sp shared_base() { return shared_from_this(); }

  template <typename T> std::shared_ptr<Promise<T>> shared_this_as() {
    return std::dynamic_pointer_cast<Promise<T>>(shared_from_this());
  }

  virtual void remove_handler(PromiseBase*) = 0;

  template <typename SINK> typename Promise<SINK>::sp create_sink() {
    return std::shared_ptr<Promise<SINK>>(new Promise<SINK>(shared_base()));
  }

  template<typename SINK> void execute_sink(typename Promise<SINK>::sp sink, typename Promise<SINK>::executor_fn executor){
    sink->execute(executor);
  }

  PromiseBase() = default;
  PromiseBase(PromiseBase::sp source) : upstream_(source->upstream()) {}

public:
  virtual ~PromiseBase(){
    auto source = above();
    if(source){
      source->remove_handler(this);
    }
  }
};

template <typename T> class Promise : public PromiseBase {
friend class Promise<>;
friend class PromiseBase;
public:
  using value_type  = T;
  using sp          = std::shared_ptr<Promise<value_type>>;

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
    std::function<void(const value_type&)>  on_fulfilled = {};
    std::function<void(std::exception_ptr)> on_rejected = {};
  };

  using executor_fn = std::function<void(resolver)>;

private:
  value_type              value_ = {};
  std::unordered_map<PromiseBase*, handler>   handlers_;

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

  virtual void remove_handler(PromiseBase* inst){
    guard lock(mtx_);
    handlers_.erase(inst);
  }

  void execute(executor_fn executor) {
    try{
      executor(resolver(shared_this()));
    }
    catch(...){
      on_rejected(std::current_exception());
    }
  }

  Promise() = default;
  Promise(PromiseBase::sp source) : PromiseBase(source){}

public:
  ~Promise() = default;

  const value_type& wait() {
    std::mutex mtx;
    std::unique_lock<std::mutex> lock(mtx);
    auto THIS = shared_this();
    cond_.wait(lock, [THIS]{ return THIS->state_ != state::pending; });
    if(state_ == state::rejected) std::rethrow_exception(error_);
    return value_; 
  }

  void stand_alone(handler h = {}) {
    auto THIS = shared_this();
    auto sink = create_sink<value_type>();  /** dummy */
    add_handler(sink.get(), {
      .on_fulfilled = [THIS, sink, h](const value_type& value){
        if(h.on_fulfilled){
          h.on_fulfilled(value);
        }
      },
      .on_rejected = [THIS, sink, h](std::exception_ptr err) {
        if(h.on_rejected){
          h.on_rejected(err);
        }
      }
    });
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
    execute_sink<TYPE>(sink, [THIS, sink, func](typename PROMISE::resolver resolver){
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
    execute_sink<TYPE>(sink, [THIS, sink, func](typename PROMISE::resolver resolver) {
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
    execute_sink<value_type>(sink, [THIS, sink, func](resolver resolver){
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
    execute_sink<TYPE>(sink, [THIS, sink, func](typename PROMISE::resolver resolver){
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
    execute_sink<TYPE>(sink, [THIS, sink, func](typename PROMISE::resolver resolver){
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
    execute_sink<value_type>(sink, [THIS, sink, func](resolver resolver) {
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
    execute_sink<TYPE>(sink, [THIS, sink, func](typename PROMISE::resolver resolver){
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
    execute_sink<TYPE>(sink, [THIS, sink, func](typename PROMISE::resolver resolver){
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
    execute_sink<value_type>(sink, [THIS, sink, func](resolver resolver) {
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