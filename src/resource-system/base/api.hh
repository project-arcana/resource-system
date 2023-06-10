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

#include <resource-system/base/comp_result.hh>
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
// - optimization: resources that transitively don't depend on volatile resource can stay valid

namespace res::base
{

struct alignas(64) ref_count
{
    int count = 1;
    void inc() { cc::intrin_atomic_add((int volatile*)&count, 1); }
    void dec() { cc::intrin_atomic_add((int volatile*)&count, -1); }
};

struct computation_desc
{
    // usually used to hash the computed function
    hash algo_hash;
    // used as additional hash for templated functions
    // is optional
    hash type_hash;

    // NOTE: the arg content is never outdated
    cc::unique_function<computation_result(cc::span<content_ref const>)> compute_resource;

    // content_ref has serialized_data and no runtime_data
    // deserialize must set either runtime_data or error_data
    // runtime_data is allowed to point into serialized_data
    // this is optional
    // NOTE: there is no serialize because that's part of compute_resource
    cc::function_ptr<void(computation_result&)> deserialize = nullptr;

    // function that computes the hash of a runtime value without needing serialization
    // this is optional
    cc::function_ptr<content_hash(void const*)> make_runtime_content_hash = nullptr;


    // TODO: where can this be computed?
    // TODO: is this immediate or multipart?
    // executor stuff

    // NOTE: we want the following features
    // - computation can be done in other threads / custom queues
    // - computation can be async + multistep
    // - computation can return a resource that should be evaluated
};

struct resource_desc
{
    // the function to execute for this resource
    comp_hash computation;

    // all dependencies of this resource
    cc::span<res_hash const> args;

    // volatile resources are assumed to change with their environment
    // however, this is only checked whenever a global generation counter is changed
    // volatile resources are checked relatively frequently when developing
    // so they should be extremely fast
    // expensive computations should be guarded by a dirty flag or put into later pure resources
    bool is_volatile = false;

    // persisted resources cause invoc cache and created content to be saved to disk
    bool is_persisted = true;
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

    // TODO: flags for volatile resources here?
    // NOTE: the returned counter is initialized with count=1
    cc::pair<res_hash, ref_count*> define_resource(resource_desc const& desc);

    // NOTE: can return content with is_outdated = true
    cc::optional<content_ref> try_get_resource_content(res_hash res, bool enqueue_if_not_found = true);

    // invalidates all volatile resources such as file timestamps or tweakable data
    // this is an extremely cheap O(1) operation
    // it will cause gradual recompution of all dependent resources
    // though in practice, most will hit the content caches anyways
    void invalidate_volatile_resources();

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

    // persistence API
public:
    /// adds all given invocations to the invoc store
    /// NOTE: not cheap
    void inject_invoc_cache(cc::span<cc::pair<base::invoc_hash, base::content_hash> const> invocs);

    /// returns a vector of all invocations to be persisted but not yet known
    /// NOTE: not cheap
    cc::vector<cc::pair<base::invoc_hash, base::content_hash>> collect_all_persistent_invocations(cc::set<base::invoc_hash> const& known_invocs);

    /// collect a list of all available content refs (of the given list) that can be persisted
    /// NOTE: not cheap
    cc::vector<content_ref> collect_all_persistent_content(cc::span<base::content_hash const> contents);

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
