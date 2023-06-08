#pragma once

#include <clean-core/string_view.hh>

#include <resource-system/

namespace res
{
/// a computation node encapsulates the function or computation of how resources are created
/// they correspond to a single comp_hash in the base api
///
/// nodes are created via:
///   auto n = res::node(...)
///   auto n = res::node_volatile(...)
///   auto n = res::node_runtime(...)
class Node
{
    Node() = default;

    // non-copyable / non-movable
    Node(Node const&) = delete;
    Node& operator=(Node const&) = delete;
};

template <class Fun>
class FunctionNode
{
public:
    FunctionNode(Fun fun) : fun(cc::move(fun)) { }

    template <class... Args>
    auto define_resource(Args&&... args)
    {
        // TODO impure part
        return detail::define_res_via_lambda(
            name, detail::res_type::normal, [](auto&&... args) { return file.execute(args...); }, name, cc::forward<Args>(args)...);
    }

private:
    Fun fun;
};

/// helper to wrap a named function into a callable that returns handles given args
/// NOTE: the name must be _globally_ unique!
///       the version should be changed whenever the semantic of the function changes
template <class FunT>
Node node(cc::string_view name, int version, FunT&& fun)
{
    return [name, fun](auto&&... args) { return res::define(name, fun, cc::forward<decltype(args)>(args)...); };
}

/// a volatile node has no invocation cache
/// it is always called if the environment is suspected to have changed
template <class FunT>
Node node_volatile(FunT&& fun)
{
    return [name, fun](auto&&... args) { return res::define_volatile(name, fun, cc::forward<decltype(args)>(args)...); };
}
/// runtime nodes are not persistent and basically "anonymous"
/// internally, they are assigned a random comp_hash
/// they still benefit from all runtime caching and deduplication
template <class FunT>
Node node_runtime(FunT&& fun)
{
    return [name, fun](auto&&... args) { return res::define_volatile(name, fun, cc::forward<decltype(args)>(args)...); };
}
} // namespace res
