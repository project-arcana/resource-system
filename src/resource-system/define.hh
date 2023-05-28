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
template <class T>
struct is_handle : std::false_type
{
};
template <class T>
struct is_handle<handle<T>> : std::true_type
{
};

template <class T>
struct is_result : std::false_type
{
};
template <class T>
struct is_result<result<T>> : std::true_type
{
};

template <class T>
struct resource_type_of_t
{
    using type = T;
};
template <class T>
struct resource_type_of_t<result<T>>
{
    using type = typename resource_type_of_t<T>::type;
};
template <class T>
struct resource_type_of_t<handle<T>>
{
    using type = typename resource_type_of_t<T>::type;
};

// int -> int
// handle<float> -> float
// result<bool> -> bool
template <class T>
using resolve_resource_type_of = typename resource_type_of_t<std::decay_t<T>>::type;

template <class... Args>
struct count_resource_handles
{
    enum
    {
        value = (0 + ... + int(is_handle<Args>::value))
    };
};

template <class T>
auto get_arg(base::content_ref const& content, T&& arg)
{
    // TODO
}

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

resource_slot* get_or_create_resource_slot(res::base::computation_desc desc, cc::span<res::base::res_hash const> args);

enum class res_type
{
    normal,
    impure,
};

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
              ? (*p_res_args++).data_ptr       // then we use the next res_arg
              : &cc::get<I>(direct_args)),     // otherwise we take the value from the tuple
         ...);                                 // NOTE: (,...) is needed for guaranteed order

        return cc::invoke(f, *reinterpret_cast<resolve_resource_type_of<Args> const*>(ptr_args[I])...);
    }
};

template <class FunT, class... Args>
auto define_res_via_lambda(cc::string_view name, res_type type, FunT&& fun, Args&&... args)
{
    static_assert(std::is_trivially_copyable_v<std::decay_t<FunT>>,                 //
                  "callables must be POD-like, i.e. all captures must be trivial. " //
                  "non-trivial captures should be provided as function argument.");

    static_assert(std::is_invocable_v<FunT, resolve_resource_type_of<Args> const&...>, //
                  "function is not callable with the correct argument types");

    using ResultT = std::decay_t<std::invoke_result_t<FunT, resolve_resource_type_of<Args> const&...>>;
    static_assert(!is_handle<ResultT>::value, "TODO: implement indirect resources");
    using ResourceT = resolve_resource_type_of<ResultT>;

    // collect resource handles
    auto constexpr res_arg_count = count_resource_handles<Args...>::value;
    base::res_hash res_args[res_arg_count > 0 ? res_arg_count : 1]; // the max(1,..) is to prevent zero sized arrays
    base::res_hash* p_res_args = res_args;
    (detail::add_resource_to_args(p_res_args, args), ...);

    // make algo hash
    cc::sha1_builder sha1;
    sha1.add(cc::as_byte_span(name));
    sha1.add(cc::as_byte_span(fun));
    (detail::add_non_resource_to_hash(sha1, args), ...);

    base::computation_desc comp_desc;
    comp_desc.name = name;
    comp_desc.algo_hash = detail::finalize_as<base::hash>(sha1);
    comp_desc.compute_resource = [fun = cc::move(fun),                                   //
                                  arg_tuple = cc::tuple<std::decay_t<Args>...>(args...)] //
        (cc::span<base::content_ref const> res_args) -> base::computation_result
    {
        base::computation_result res;

        // if any arg is missing, result is error
        for (auto const& arg : res_args)
            if (arg.has_error())
            {
                base::content_error_data err;
                err.message = "at least one dependency had an error";
                res.error_data = cc::move(err);
                return res;
            }

        // actual eval
        auto eval_res = res_evaluator<std::make_index_sequence<sizeof...(Args)>> //
            ::template eval<std::decay_t<FunT>, decltype(arg_tuple), Args...>(fun, arg_tuple, res_args);

        // result<ResourceT>
        if constexpr (is_result<ResultT>::value)
        {
            if (eval_res.has_value())
            {
                base::content_runtime_data data;
                data.data_ptr = cc::alloc<ResourceT>(cc::move(eval_res.value()));
                data.deleter = [](void* p) { cc::free(reinterpret_cast<ResourceT*>(p)); };
                res.runtime_data = cc::move(data);
            }
            else
            {
                base::content_error_data err;
                err.message = eval_res.error().to_string();
                res.error_data = cc::move(err);
            }
        }
        else // just ResourceT
        {
            base::content_runtime_data data;
            data.data_ptr = cc::alloc<ResourceT>(cc::move(eval_res));
            data.deleter = [](void* p) { cc::free(reinterpret_cast<ResourceT*>(p)); };
            res.runtime_data = cc::move(data);
        }

        // TODO: serialization

        return res;
    };

    (void)type; // declare impure resource

    auto slot = detail::get_or_create_resource_slot(cc::move(comp_desc), cc::span<base::res_hash>(res_args, res_arg_count));
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
} // namespace res
