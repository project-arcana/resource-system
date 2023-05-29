#pragma once

#include <type_traits>

#include <clean-core/allocate.hh>
#include <clean-core/always_false.hh>
#include <clean-core/apply.hh>
#include <clean-core/forward.hh>
#include <clean-core/move.hh>
#include <clean-core/tuple.hh>

#include <clean-core/hash.sha1.hh>

// TODO: slim down to the api part we need
#include <resource-system/base/api.hh>

#include <resource-system/handle.hh>
#include <resource-system/meta.hh>

// currently we need to include this
// which includes cc::string
#include <resource-system/result.hh>

#include <resource-system/detail/hash_helper.hh>
#include <resource-system/detail/resource_slot.hh>

namespace res
{
namespace detail
{

template <class... Args>
struct count_resource_handles
{
    enum
    {
        value = (0 + ... + int(is_handle<std::decay_t<Args>>::value))
    };
};

template <class T>
void add_non_resource_to_hash(cc::sha1_builder& sha1, T const& val)
{
    if constexpr (is_handle<T>::value)
    {
        // nothing, is part of resource desc
    }
    else
    {
        static_assert(std::is_trivially_copyable_v<T>, "non-resource arguments must be POD currently. TODO: use meta serialization system");
        sha1.add(cc::as_byte_span(val));
    }
}

template <class T>
void add_resource_to_args(base::res_hash*& p_args, T const& val)
{
    if constexpr (is_handle<T>::value)
    {
        *p_args++ = val.get_hash();
    }
    else
    {
        // nothing
    }
}

resource_slot* get_or_create_resource_slot(res::base::computation_desc desc, cc::span<res::base::res_hash const> args, bool is_impure);

enum class res_type
{
    normal,
    impure,
};

template <class T>
auto res_eval_arg_from_ptr(void const* p)
{
    // is reference to other resource?
    // -> need to use runtime repr type
    if constexpr (is_handle<T>::value)
    {
        return resource_traits<typename T::resource_t>::from_content_ref(*(base::content_ref const*)p);
    }
    // otherwise is direct arg
    // -> just use it as-is
    else
    {
        return *reinterpret_cast<T const*>(p);
    }
}

template <class>
struct res_evaluator;

template <size_t... I>
struct res_evaluator<std::integer_sequence<size_t, I...>>
{
    template <class F, class DirectArgTuple, class... Args>
    static auto eval(F const& f, DirectArgTuple const& direct_args, cc::span<base::content_ref const> res_args)
    {
        // TODO: support res::result<T> in args

        void const* ptr_args[sizeof...(Args)];
        void const** p_args = ptr_args;
        auto p_res_args = res_args.data();
        ((*p_args++ =                          // we set each arg in order
          is_handle<std::decay_t<Args>>::value // if it's a handle
              ? (void const*)p_res_args++      // then we use the next res_arg
              : &cc::get<I>(direct_args)),     // otherwise we take the value from the tuple
         ...);                                 // NOTE: (,...) is needed for guaranteed order

        return cc::invoke(f, detail::res_eval_arg_from_ptr<std::decay_t<Args>>(ptr_args[I])...);
    }
};

template <class ResourceT, class ResultT>
base::computation_result make_comp_result(ResultT value)
{
    static_assert(!is_handle<ResultT>::value, "TODO: implement indirect resources");

    // result<ResourceT>
    if constexpr (is_result<ResultT>::value)
    {
        if (value.has_value())
        {
            return detail::make_comp_result<ResourceT>(cc::move(value.value()));
        }
        else
        {
            base::computation_result res;
            base::content_error_data err;
            err.message = value.error().to_string();
            res.error_data = cc::move(err);
            return res;
        }
    }
    else // just ResourceT
    {
        base::computation_result res;
        resource_traits<ResourceT>::make_comp_result(res, cc::move(value));
        return res;
    }
}

template <class FunT, class... Args>
auto define_res_via_lambda(cc::string_view name, res_type type, FunT&& fun, Args&&... args)
{
    static_assert(std::is_trivially_copyable_v<std::decay_t<FunT>>,                 //
                  "callables must be POD-like, i.e. all captures must be trivial. " //
                  "non-trivial captures should be provided as function argument.");

    static_assert(std::is_invocable_v<FunT, arg_to_resource<Args> const&...>, //
                  "function is not callable with the correct argument types");

    using ResultT = std::decay_t<std::invoke_result_t<FunT, arg_to_resource<Args> const&...>>;
    static_assert(!is_handle<ResultT>::value, "TODO: implement indirect resources");
    using ResourceT = result_to_resource<ResultT>;

    // collect resource handles
    auto constexpr res_arg_count = int(count_resource_handles<Args...>::value);
    base::res_hash res_args[res_arg_count > 0 ? res_arg_count : 1]; // the max(1,..) is to prevent zero sized arrays
    base::res_hash* p_res_args = res_args;
    (detail::add_resource_to_args(p_res_args, args), ...);

    // make algo hash
    cc::sha1_builder sha1;
    sha1.add(cc::as_byte_span(name));
    sha1.add(cc::as_byte_span(fun));
    (detail::add_non_resource_to_hash(sha1, args), ...);
    auto const algo_hash = detail::finalize_as<base::hash>(sha1);

    base::computation_desc comp_desc;
    comp_desc.name = name;
    comp_desc.algo_hash = algo_hash;
    comp_desc.compute_resource = [fun = cc::move(fun),                                                      //
                                  arg_tuple = cc::tuple<std::decay_t<Args>...>(cc::forward<Args>(args)...)] //
        (cc::span<base::content_ref const> res_args) -> base::computation_result
    {
        // if any arg is missing, result is error
        for (auto const& arg : res_args)
            if (arg.has_error())
            {
                base::computation_result res;
                base::content_error_data err;
                err.message = "at least one dependency had an error";
                res.error_data = cc::move(err);
                return res;
            }

        // actual eval
        auto eval_res = res_evaluator<std::make_index_sequence<sizeof...(Args)>> //
            ::template eval<std::decay_t<FunT>, decltype(arg_tuple), Args...>(fun, arg_tuple, res_args);

        // unpack / convert / serialize result
        return detail::make_comp_result<ResourceT>(eval_res);
    };

    auto is_impure = type == res_type::impure;
    auto slot = detail::get_or_create_resource_slot(cc::move(comp_desc), cc::span<base::res_hash>(res_args, res_arg_count), is_impure);
    return slot->template create_handle<ResourceT>();
}

} // namespace detail

/// defines a resource and returns a handle to it
/// NOTE: - the resource is not being loaded yet!
///       - Node can be a callable or a node protocol type (see node_traits in meta.hh)
///       - args can be resource handles or any (moveable) types
///         (non-handles will be moved to internal memory so lifetimes are not an issue)
///       - nodes create computation and will not be referenced afterwards
///         (unless that is how that node is implemented)
///
/// Naming convention:
///  - name should be globally unique to prevent comp_hash aliasing
///  - encode a 'path' like "materials/snow/snowy_ground_01"
///  - end name in "#1234" where 1234 is an increasing version number
///    e.g. "mesh-processing/smoothing#17"
///  - some nodes support using the name as argument as well
///    e.g. res::define("/path/to/some/file", res::file);
template <class NodeT, class... Args>
[[nodiscard]] auto define(cc::string_view name, NodeT&& node, Args&&... args)
{
    // explicit node
    if constexpr (res::node_traits<std::decay_t<NodeT>>::is_node)
    {
        return node.define_resource(name, cc::forward<Args>(args)...);
    }
    // .. or callable
    else
    {
        return detail::define_res_via_lambda(name, detail::res_type::normal, cc::forward<NodeT>(node), cc::forward<Args>(args)...);
    }
}
/// same as res::define but will never use a cached invocation
/// aka treats the computation function as non-deterministic / impure
template <class NodeT, class... Args>
[[nodiscard]] auto define_impure(cc::string_view name, NodeT&& node, Args&&... args)
{
    // explicit node
    if constexpr (res::node_traits<std::decay_t<NodeT>>::is_node)
    {
        static_assert(cc::always_false<NodeT>, "TODO: implement impure for nodes");
    }
    // .. or callable
    else
    {
        return detail::define_res_via_lambda(name, detail::res_type::impure, cc::forward<NodeT>(node), cc::forward<Args>(args)...);
    }
}

/// defines a resource and provides an explicit value
template <class T>
[[nodiscard]] handle<T> create(cc::string_view name, T value)
{
    return res::define(
        name, [](T value) { return value; }, cc::move(value));
}
template <class T>
[[nodiscard]] handle<T> create(T value)
{
    return res::define(
        "$const", [](T value) { return value; }, cc::move(value));
}

/// defines a resource, triggers its loading, and returns a handle to it
template <class NodeT, class... Args>
[[nodiscard]] auto load(cc::string_view name, NodeT&& node, Args&&... args)
{
    auto h = res::define(name, cc::forward<NodeT>(node), cc::forward<Args>(args)...);
    h.try_get();
    return h;
}

/// helper to wrap a named function into a callable that returns handles given args
/// i.e.
///    auto h = res::define(name, fun, args...)
/// is the same as
///    auto n = res::node(name, fun);
///    auto h = n(args...);
///
template <class FunT>
auto node(cc::string_view name, FunT&& fun)
{
    return [name, fun](auto&&... args) { return res::define(name, fun, cc::forward<decltype(args)>(args)...); };
}
template <class FunT>
auto node_impure(cc::string_view name, FunT&& fun)
{
    return [name, fun](auto&&... args) { return res::define_impure(name, fun, cc::forward<decltype(args)>(args)...); };
}
} // namespace res
