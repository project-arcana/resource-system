#pragma once

#include <cstddef>
#include <cstdint>

#include <clean-core/optional.hh>
#include <clean-core/span.hh>
#include <clean-core/string.hh>
#include <clean-core/unique_function.hh>
#include <clean-core/unique_ptr.hh>

// [hash based resource system]
// this file contains the base API to create and manage resources
// everything not in base/ is the "porcelain" part of the resource API

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

// see https://en.wikipedia.org/wiki/Birthday_problem#Probability_table
// - 128 bit hash
// - 10^10 objects in the database
// - 10^-18 probability of at least one collision
struct alignas(16) hash
{
    uint64_t w0;
    uint64_t w1;

    constexpr bool operator==(hash const& h) const { return w0 == h.w0 && w1 == h.w1; }
    constexpr bool operator!=(hash const& h) const { return w0 != h.w0 || w1 != h.w1; }
};

struct comp_hash : hash
{
};
struct res_hash : hash
{
};
struct content_hash : hash
{
};
struct invoc_hash : hash
{
};

enum class content_state
{
    // not valid content
    invalid = 0,
    // valid but the content is an error (e.g. exception during execution)
    error,
    // valid and the data is serialized
    serialized_data,
    // valid and the data is void* (unserializable)
    opaque_data,
};

struct opaque_data
{
    // TODO: invoc hash as content_hash?
    void* data_ptr;
    cc::function_ptr<void(void*)> deleter;
};

struct content_data
{
    content_hash hash;
    content_state state = content_state::invalid;

    // if this is a true, then the data is not necessarily the "most current"
    // it's still accessible but will change in the future
    bool is_outdated = false;

    union data_t
    {
        cc::span<std::byte const> serialized;
        opaque_data opaque;

        data_t() {}
    } data;
};

struct computation_desc
{
    cc::string name;
    hash algo_hash;

    cc::unique_function<void()> compute_resource;

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

struct invoc_result
{
    content_hash content;
    content_data data;
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
    // TODO: refcounting?
    res_hash define_resource(comp_hash const& computation, cc::span<res_hash const> args);

    // TODO: error states?
    // TODO: non-serialized type
    // TODO: access to outdated data
    cc::optional<content_data> try_get_resource_content(res_hash res, bool start_computation_if_not_found = true);

    // invalidates all impure resources such as file timestamps or tweakable data
    // this is an extremely cheap O(1) operation
    // it will cause gradual recompution of all dependent resources
    // though in practice, most will hit the content caches anyways
    void invalidate_impure_resources();

    // processing API
public:
    // NOTE: THIS IS DEBUG API
    void process_all();

    // internal core operations
    // TODO: does it make sense to expose these?
private:
    // TODO: error states?
    cc::optional<content_data> query_content(content_hash hash);

    // TODO: error states?
    // TODO: version that can move data?
    content_hash store_content(cc::span<std::byte const> data);

    // NOTE: this is really fast and does not need DB access
    invoc_hash define_invocation(comp_hash const& computation, cc::span<content_hash const> args);

    // TODO: error states?
    cc::optional<invoc_result> query_invocation(invoc_hash hash, bool try_query_data = true);

private:
    // all complex implementation is pimpl'd to keep the header clean
    struct impl;
    cc::unique_ptr<impl> m;

    // generation counter used for O(1) invalidation of locally cached content
    int volatile generation = 1000;
};

} // namespace res::base
