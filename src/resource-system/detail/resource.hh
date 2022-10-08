#pragma once

#include <clean-core/function_ptr.hh>
#include <clean-core/vector.hh>

#include <resource-system/fwd.hh>
#include <resource-system/handle.hh>

namespace res::detail
{
/// A resource is a node + parameters + dependencies
///
/// TODO: decouple data store from resources?
///       in particular using old data is not a problem anymore
///       we can also deduplicate stuff because everything is content-addressable
/// 
/// NOTE: resource is pointer-stable
struct resource
{
    // TODO: better data layout and stuff

    Node* node = nullptr;
    void* data = nullptr;
    void* args = nullptr;
    cc::function_ptr<void(resource&)> deleter = nullptr;
    cc::function_ptr<void(resource&)> load = nullptr;

    /// can be false even if data is valid (if stale data is in there)
    bool is_loaded = false;

    // TODO: lifetime?
    /// this resource depends on all resources in dependencies
    cc::vector<resource*> dependencies;
    /// all resources that depend on this
    cc::vector<resource*> dependers;

    template <class T>
    handle<T> make_handle()
    {
        return handle<T>(*this);
    }
};
}
