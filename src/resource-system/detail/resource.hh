#pragma once

#include <clean-core/function_ptr.hh>
#include <clean-core/vector.hh>

#include <resource-system/fwd.hh>
#include <resource-system/handle.hh>

namespace res::detail
{
/// A resource is a node + parameters + dependencies
struct resource
{
    // TODO: better data layout and stuff

    Node* node = nullptr;
    void* data = nullptr;
    void* args = nullptr;
    cc::function_ptr<void(resource&)> deleter = nullptr;
    cc::function_ptr<void(resource&)> load = nullptr;

    // TODO: lifetime?
    /// this resource depends on all resources in dependencies
    cc::vector<resource*> dependencies;

    bool is_loaded() const { return data != nullptr; }

    template <class T>
    handle<T> make_handle()
    {
        return handle<T>(*this);
    }
};
}
