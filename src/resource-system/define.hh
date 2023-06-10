#pragma once

#include <type_traits>

#include <resource-system/detail/internal_define.hh>
#include <resource-system/node.hh>

namespace res
{
/// given a type T, returns a handle<U>
/// where U is how this resource is internally accessed
/// e.g. vector<int> becomes handle<span<int>>
template <class T>
using handle_for = handle<detail::result_to_resource<T>>;

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
[[nodiscard]] auto define(NodeT& node, Args&&... args)
{
    static_assert(std::is_base_of_v<res::Node, NodeT>, "for now, all nodes must derive from res::Node");
    return node.define_resource(cc::forward<Args>(args)...);
}
/// same as
///   auto n = res::node_runtime(fun);
///   auto r = res::define(n, args...);
template <class Fun, class... Args>
[[nodiscard]] auto define_runtime(Fun&& fun, Args&&... args)
{
    auto n = res::node_runtime(cc::forward<Fun>(fun));
    return res::define(n, cc::forward<Args>(args)...);
}
/// same as
///   auto n = res::node_volatile(fun);
///   auto r = res::define(n, args...);
template <class Fun, class... Args>
[[nodiscard]] auto define_volatile(Fun&& fun, Args&&... args)
{
    auto n = res::node_volatile(cc::forward<Fun>(fun));
    return res::define(n, cc::forward<Args>(args)...);
}

/// defines a resource and provides an explicit value
/// NOTE: values are not deduplicated (each invocation creates a new node)
template <class T>
[[nodiscard]] auto create(T value) -> handle<detail::const_to_resource<T>>
{
    // TODO: alternative:
    //   static n = res::node("__internal/const/" + T, [](T const& v) { return v; });
    //   return res::define(n, cc::move(value));
    return detail::define_constant(cc::move(value));
}
/// creates a _volatile_ resource referencing 'value'
/// CAUTION: user must ensure that lifetime of value is greater than any resource use
template <class T>
[[nodiscard]] auto create_volatile_ref(T& value) -> handle<detail::result_to_resource<T>>
{
    auto n = res::node_volatile([&value] { return value; });
    return res::define(n);
}

/// defines a resource, triggers its loading, and returns a handle to it
template <class NodeT, class... Args>
[[nodiscard]] auto load(NodeT& node, Args&&... args)
{
    auto h = res::define(node, cc::forward<Args>(args)...);
    h.try_get();
    return h;
}
} // namespace res
