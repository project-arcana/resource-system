#include "System.hh"

#include <clean-core/assert.hh>
#include <clean-core/vector.hh>

#include <resource-system/define.hh>
#include <resource-system/detail/resource.hh>
#include <resource-system/file.hh>
#include <resource-system/handle.hh>

res::System& res::system()
{
    static System system;
    return system;
}

bool res::detail::resource_is_loaded_no_error(resource const& r) { return r.is_loaded && !r.is_error(); }

bool res::detail::resource_try_load(resource& r)
{
    if (r.is_loaded)
        return true;

    system().trigger_load(r);
    return false;
}

void const* res::detail::resource_get(resource const& r)
{
    CC_ASSERT(r.is_loaded && "resource not loaded. did you forget to call try_load? or do you want to use try_get?");
    return r.data;
}

void const* res::detail::resource_try_get(resource& r)
{
    resource_try_load(r);
    return r.data; // is nullptr if not loaded
}

void res::detail::resource_invalidate_dependers(detail::resource& r)
{
    // TODO: threadsafe!
    for (auto rr : r.dependers)
    {
        if (!rr->is_loaded)
            continue;

        rr->is_loaded = false;
        resource_invalidate_dependers(*rr);
    }
}

void res::System::trigger_load(res::detail::resource& r)
{
    // TODO: threadsafe!
    resources_to_load.add(&r);
}

void res::System::process_all()
{
    // TODO: threadsafe!

    // check for changes
    res::file.check_hot_reloading();

    // check for need-to-loads
    cc::vector<detail::resource*> res;

    while (!resources_to_load.empty())
    {
        // copy all resources to local vector
        res.clear();
        res.reserve(resources_to_load.size());

        for (auto r : resources_to_load)
            if (!r->is_loaded) // can already be loaded due to dependency chains
                res.push_back(r);

        resources_to_load.clear();

        // process resources (might create new ones to load)
        for (auto r : res)
        {
            // CC_ASSERT(!r->data && "should never happen"); --- can happen during reload
            CC_ASSERT(r->node && "must have a node to load resource");

            // check if deps are available
            auto can_load = true;
            for (auto d : r->dependencies)
                if (!d->is_loaded)
                {
                    resources_to_load.add(d);
                    can_load = false;
                }

            // .. if not: try next round
            if (!can_load)
            {
                resources_to_load.add(r);
                continue;
            }

            // TODO: some nodes might have their own execution
            r->node->load(*r);

            // TODO: failable resource load?
            CC_ASSERT(r->is_loaded && "node load should never fail");
        }
    }
}
