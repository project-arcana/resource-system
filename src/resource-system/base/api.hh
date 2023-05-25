#pragma once

#include <cstddef>
#include <cstdint>

#include <clean-core/polymorphic.hh>
#include <clean-core/span.hh>
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

// TODO: other size? 128 bit? 256 bit?
struct alignas(16) hash
{
    uint64_t w0;
    uint64_t w1;
};
// static_assert(sizeof(hash) == 20);

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

// TODO: lifetimes / refcounting
class KeyValueStorageBase : public cc::polymorphic
{
public:
    virtual cc::span<std::byte const> get(hash const& h) const = 0;
    virtual void set(hash const& h, cc::span<std::byte const> data) = 0;
};

struct computation_desc
{
    hash algo_hash;

    // TODO: where can this be computed?
    // TODO: is this immediate or multipart?
};

/// a resource system manages access / computation / lifetimes of resources
/// the comp_hash key-value-storage usually must be recreated on startup and cannot be persistet
/// but all other key-value-storages are customizable and "POD"
///
/// NOTE: this version implements the base API
///       in particular, there are very few safety rails on purpose
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
    comp_hash define_computation(computation_desc desc);
    res_hash define_resource(comp_hash const& computation, cc::span<res_hash const> args);


private:
    // all complex implementation is pimpl'd to keep the header clean
    struct impl;
    cc::unique_ptr<impl> m;
};

}
