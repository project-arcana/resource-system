#pragma once

#include <clean-core/invoke.hh>
#include <clean-core/string_view.hh>

#include <resource-system/detail/internal_define.hh>

namespace res
{
namespace detail
{
void register_node_name(cc::string_view name);
base::hash make_name_version_algo_hash(cc::string_view name, int version);
} // namespace detail

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
    FunctionNode(Fun fun, base::comp_hash comp_hash, detail::res_type type) : fun(cc::move(fun)), comp_hash(comp_hash), type(type) {}

    template <class... Args>
    auto define_resource(Args&&... args)
    {
        return detail::define_res_via_lambda(comp_hash, type, fun, cc::forward<Args>(args)...);
    }

private:
    Fun fun;
    base::comp_hash comp_hash;
    detail::res_type type;
};

/// helper to wrap a named function into a callable that returns handles given args
/// NOTE: the name must be _globally_ unique!
///       the version should be changed whenever the semantic of the function changes
template <class FunT>
auto node(cc::string_view name, int version, FunT&& fun)
{
    detail::register_node_name(name);
    return FunctionNode<std::decay_t<FunT>>(cc::forward<FunT>(fun), detail::make_name_version_algo_hash(name, version), detail::res_type::normal);
}

/// a volatile node has no invocation cache
/// it is always called if the environment is suspected to have changed
template <class FunT>
auto node_volatile(FunT&& fun)
{
    return FunctionNode<std::decay_t<FunT>>(cc::forward<FunT>(fun), base::detail::make_random_unique_hash(), detail::res_type::volatile_);
}
/// runtime nodes are not persistent and basically "anonymous"
/// internally, they are assigned a random comp_hash
/// they still benefit from all runtime caching and deduplication
template <class FunT>
auto node_runtime(FunT&& fun)
{
    return FunctionNode<std::decay_t<FunT>>(cc::forward<FunT>(fun), base::detail::make_random_unique_hash(), detail::res_type::runtime);
}
} // namespace res
