#pragma once

#include <clean-core/invoke.hh>
#include <clean-core/polymorphic.hh>
#include <clean-core/string_view.hh>

#include <resource-system/detail/resource.hh>
#include <resource-system/fwd.hh>

namespace res
{
/// base class for parameterized resources in the computation graph
/// NOTE: resources are a combination of nodes and parameters
///
/// TODO: configurable if old value should be reported
class Node : public cc::polymorphic
{
public:
    // TODO: don't use internal types?
    // TODO: can maybe made non-virtual because resources are type-erasing anyways
    virtual void load(detail::resource& r) = 0;
};

template <class F>
class FunctionNode : public Node
{
public:
    F function;
    explicit FunctionNode(F fun) : function(cc::move(fun)) {}

    void load(detail::resource& r) override
    {
        CC_ASSERT(r.load && "no load function?");
        r.load(r);
        r.is_loaded = true;
    }

    template <class... Args>
    auto execute(Args const&... args) const // can be executed multiple times -> const&, not &&
    {
        return cc::invoke(function, args...);
    }
};

/// create a named node that executes a function
/// name is used for persistent caching
template <class F>
FunctionNode<F> node(cc::string_view name, F&& fun)
{
    (void)name; // TODO!
    return FunctionNode<F>(cc::forward<F>(fun));
}
}
