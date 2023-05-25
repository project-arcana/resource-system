#include "api.hh"

#include <clean-core/map.hh>
#include <clean-core/vector.hh>

namespace
{
struct res_hash_hasher
{
    size_t operator()(res::base::hash const& h) const { return reinterpret_cast<size_t const&>(h); }
};
}

struct res::base::ResourceSystem::impl
{
    // TODO

    struct comp_desc
    {
        // comp metadata
        // arg size and types
        // return type
        // executor stuff
    };

    // TODO: flat?
    struct res_desc
    {
        comp_hash comp;
        cc::vector<res_hash> args;
    };

    struct content_desc
    {
        cc::array<std::byte> data;
    };

    // TODO: flat?
    struct invoc_desc
    {
        comp_hash comp;
        cc::vector<content_hash> args;
    };

    cc::map<comp_hash, comp_desc, res_hash_hasher> comp_desc_by_hash;
    cc::map<res_hash, res_desc, res_hash_hasher> res_desc_by_hash;
    cc::map<content_hash, content_desc, res_hash_hasher> content_desc_by_hash;
    cc::map<invoc_hash, invoc_desc, res_hash_hasher> invoc_desc_by_hash;
};

res::base::ResourceSystem::ResourceSystem() { m = cc::make_unique<impl>(); }

res::base::ResourceSystem::~ResourceSystem() = default;
