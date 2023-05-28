#pragma once

#include <cstddef>
#include <cstdint>

#include <clean-core/optional.hh>
#include <clean-core/pair.hh>
#include <clean-core/span.hh>
#include <clean-core/string.hh>
#include <clean-core/unique_function.hh>
#include <clean-core/unique_ptr.hh>
#include <clean-core/vector.hh>

// for atomic_add
#include <clean-core/intrinsics.hh>

#include <resource-system/base/hash.hh>

// [hash based resource system]
// this file contains the base API to create and manage resources
// everything not in base/ is the "porcelain" part of the resource API

// [open questions]
//  - how to handle cancellation in all this?

// hash types:
//
// [comp hash] (from computation description)
//   - a unique value for the "algorithm"/"function"
//   - all static parameters
//   - number and type of arguments (other resources!)
//   - type of return value
//   -> comp description also contains code to execute and execution modalities
//   -> TODO: think about if static parameters can be optimized / separated
//
// [res hash] (from resource description)
//   - comp hash
//   - res hash of all arguments
//   -> this is the "resource id" / "resource name"
//
// [content hash]
//   - hash of computed data
//
// [invoc hash] (from invocation description)
//   - comp hash
//   - content hash of all arguments
//   -> for pure resources, this deterministically defines the resulting content hash

// key value storages:
//
// {    comp hash -> comp metadata, args, fun ptr, ... }
// {     res hash -> comp hash, arg res hashes ... }
// { content hash -> content bytes }
// {   invoc hash -> content hash }
//
// - some storages can be persistet / remote, others not
//   in particular, { comp hash -> ... } must be repopulated on startup

// solution for "load from file" / "tweakable params" non-purity:
// - resources can be flagged as "don't cache invocation"
// - cached content per resource can be globally invalidated via generation int
//   NOTE: { content hash -> content bytes } stays valid
//         only the { res hash -> content hash } mapping is invalidated
// - optimization: resources that transitively don't depend on impure resource can stay valid

namespace res::base
{

struct alignas(64) ref_count
{
    int count = 1;
    void inc() { cc::intrin_atomic_add((int volatile*)&count, 1); }
    void dec() { cc::intrin_atomic_add((int volatile*)&count, -1); }
};

// TODO: proper states
//       in memory but not serialized
//       hash only
//       serialized but not deserialized
//       ...
// TODO: refcounting here?
struct content_ref
{
    content_hash hash;

    // generation that this content was computed for
    int generation = -1;

    // if this is a true, then the data is not necessarily the "most current"
    // it's still accessible but will change in the future
    bool is_outdated = false;

    void const* data_ptr = nullptr;

    // TODO: more elaborate error type?
    cc::string_view error_msg;

    bool has_value() const { return data_ptr != nullptr; }
    bool has_error() const { return data_ptr == nullptr; }
};

struct content_serialized_data
{
    cc::vector<std::byte> blob;
};
struct content_runtime_data
{
    void* data_ptr;
    cc::function_ptr<void(void*)> deleter;
};
struct content_error_data
{
    cc::string message;
};

struct computation_result
{
    cc::optional<content_serialized_data> serialized_data;
    cc::optional<content_runtime_data> runtime_data;
    cc::optional<content_error_data> error_data;
};

struct computation_desc
{
    cc::string name;
    hash algo_hash;

    // NOTE: the arg content is never outdated
    cc::unique_function<computation_result(cc::span<content_ref const>)> compute_resource;

    // TODO: where can this be computed?
    // TODO: is this immediate or multipart?
    // comp metadata
    // arg size and types
    // return type
    // executor stuff
    // "impure resources" (should be stored in res_desc probably)

    // nonserializable resources:
    // - content_hash = invocation_hash
    // - no persisting
    // - otherwise easy to support

    // NOTE: we want the following features
    // - computation can be done in other threads / custom queues
    // - computation can be async + multistep
    // - computation can return a resource that should be evaluated
};

/// a resource system manages access / computation / lifetimes of resources
/// the comp_hash key-value-storage usually must be recreated on startup and cannot be persistet
/// but all other key-value-storages are customizable and "POD"
///
/// NOTE: this version implements the base API
///       in particular, there are very few safety rails on purpose
///
/// MULTITHREADING: all public methods are threadsafe
///
/// TODO: refcounting for comp+res
///       can be done by returning pointers to an atomic int
class ResourceSystem
{
public:
    ResourceSystem();
    ~ResourceSystem();

    ResourceSystem(ResourceSystem&&) = delete;
    ResourceSystem(ResourceSystem const&) = delete;
    ResourceSystem& operator=(ResourceSystem&&) = delete;
    ResourceSystem& operator=(ResourceSystem const&) = delete;

    // core operations
public:
    // NOTE: defining the same computation twice returns the same hash
    //       there are some debug checks to prevent easy errors
    // TODO: split into mandatory (aka hash-defining part)
    //       and lazy stuff (aka populating the desc)
    // TODO: refcounting?
    comp_hash define_computation(computation_desc desc);

    // TODO: flags for impure resources here?
    // NOTE: the returned counter is initialized with count=1
    cc::pair<res_hash, ref_count*> define_resource(comp_hash const& computation, cc::span<res_hash const> args);

    // NOTE: can return content with is_outdated = true
    cc::optional<content_ref> try_get_resource_content(res_hash res, bool enqueue_if_not_found = true);

    // invalidates all impure resources such as file timestamps or tweakable data
    // this is an extremely cheap O(1) operation
    // it will cause gradual recompution of all dependent resources
    // though in practice, most will hit the content caches anyways
    void invalidate_impure_resources();

    /// returns true if this content can be used
    /// returns false if try_get_resource_content should be called again
    /// this call is extremely cheap
    /// it's designed to be executed before every access to the content
    CC_FORCE_INLINE bool is_up_to_date(content_ref const& content) const { return content.generation >= generation; }
    CC_FORCE_INLINE bool is_up_to_date(int gen) const { return gen >= generation; }

    // processing API
public:
    // NOTE: THIS IS DEBUG API
    void process_all();

    // internal core operations
    // TODO: does it make sense to expose these?
private:
    // TODO: error states?
    cc::optional<content_ref> query_content(content_hash hash);

    // NOTE: this is really fast and does not need DB access
    invoc_hash define_invocation(comp_hash const& computation, cc::span<content_hash const> args);

    // NOTE: never returns outdated data
    cc::optional<content_hash> try_get_resource_content_hash(res_hash res, bool enqueue_if_not_found = true);

    // queue processing
private:
    // returns true if one task was processed
    bool impl_process_queue_res(bool need_content);

private:
    // all complex implementation is pimpl'd to keep the header clean
    struct impl;
    cc::unique_ptr<impl> m;

    // generation counter used for O(1) invalidation of locally cached content
    int volatile generation = 1000;
};

} // namespace res::base
