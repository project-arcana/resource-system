#pragma once

#include <type_traits>

#include <clean-core/allocate.hh>
#include <clean-core/always_false.hh>
#include <clean-core/apply.hh>
#include <clean-core/forward.hh>
#include <clean-core/move.hh>
#include <clean-core/tuple.hh>

#include <resource-system/Node.hh>
#include <resource-system/detail/resource.hh>
#include <resource-system/handle.hh>

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
void register_dependency(detail::resource& r, T&& arg)
{
    if constexpr (is_handle<std::decay_t<T>>::value)
    {
        CC_ASSERT(arg.is_valid() && "invalid resource handle, did you forget to initialize it?");
        r.dependencies.push_back(resource_from_handle(arg));
    }
}
template <class T>
decltype(auto) get_arg(detail::resource& r, T&& arg)
{
    if constexpr (is_handle<std::decay_t<T>>::value)
    {
        CC_ASSERT(arg.is_valid() && "invalid resource handle, did you forget to initialize it?");
        CC_ASSERT(arg.is_loaded() && "argument is not loaded (should never happen)");
        return arg.get();
    }
    else
        return arg;
}
}

/// defines a resource and returns a handle to it
/// NOTE: - the resource is not being loaded yet!
///       - Node can be a callable or a res::Node
///       - args can be resource handles or any types
///         (non-handles will be moved to internal memory so lifetimes are not an issue)
template <class NodeT, class... Args>
[[nodiscard]] auto define(NodeT&& node, Args&&... args)
{
    // explicit node
    if constexpr (std::is_base_of_v<res::Node, std::decay_t<NodeT>>)
    {
        // TODO: maybe require to register nodes (for better lifetime management)

        auto r = cc::alloc<detail::resource>();
        r->node = &node;
        using args_t = cc::tuple<std::decay_t<Args>...>;
        auto argp = cc::alloc<args_t>(cc::forward<Args>(args)...);
        r->args = argp;

        // add dependencies to r
        (detail::register_dependency(*r, args), ...);

        using T = std::decay_t<decltype(node.execute(detail::get_arg(*r, args)...))>;

        r->load = [](detail::resource& r) {
            CC_ASSERT(!r.data && "already loaded?");
            CC_ASSERT(r.node && "no node provided?");
            CC_ASSERT(r.args && "no arguments provided? already deleted?");
            auto n = static_cast<std::remove_reference_t<NodeT>*>(r.node);
            auto argp = static_cast<args_t*>(r.args);
            r.data = cc::alloc<T>(cc::apply([&r, n](auto&&... args) { return n->execute(detail::get_arg(r, args)...); }, *argp));
        };

        r->deleter = [](detail::resource& r) {
            cc::free(static_cast<args_t*>(r.args));
            r.args = nullptr;

            if (r.data)
            {
                cc::free(static_cast<T*>(r.data));
                r.data = nullptr;
            }
        };

        return r->make_handle<T>();
    }
    // .. or callable
    else
    {
        static_assert(std::is_invocable_v<NodeT, decltype(detail::get_arg(std::declval<detail::resource&>(), args))...>,
                      "function is not callable with the provided arguments");

        // TODO: shared_ptr semantic
        // TODO: deduplicate
        // TODO: lifetime
        auto n = new FunctionNode(cc::forward<NodeT>(node));

        return res::define(*n, cc::forward<Args>(args)...);
    }
}

/// defines a resource and provides an explicit value
/// this resource is always immediately set to loaded
/// TODO: should this support polymorphic types?
template <class T>
[[nodiscard]] handle<T> create(T value)
{
    auto r = cc::alloc<detail::resource>();
    r->data = cc::alloc<T>(cc::move(value));
    r->deleter = [](detail::resource& r) {
        cc::free(static_cast<T*>(r.data));
        r.data = nullptr;
    };
    // TODO: who is freeing r?
    return r->make_handle<T>();
}

/// defines a resource, triggers its loading, and returns a handle to it
template <class NodeT, class... Args>
[[nodiscard]] auto load(NodeT&& node, Args&&... args)
{
    auto h = res::define(cc::forward<NodeT>(node), cc::forward<Args>(args)...);
    h.request();
    return h;
}
}
