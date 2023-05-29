#include "System.hh"

#include <shared_mutex>

#include <clean-core/assert.hh>
#include <clean-core/map.hh>
#include <clean-core/vector.hh>

#include <resource-system/define.hh>
#include <resource-system/detail/resource_slot.hh>
#include <resource-system/file.hh>
#include <resource-system/handle.hh>

struct res::System::pimpl
{
    std::shared_mutex res_slots_mutex;
    cc::map<base::res_hash, detail::resource_slot> res_slots;
};

res::System& res::system()
{
    static System system;
    return system;
}

bool res::detail::resource_is_loaded_no_error(resource_slot const& r) { return r.cached_value != nullptr; }

res::base::res_hash res::detail::resource_get_hash(resource_slot& r) { return r.resource; }

res::detail::resource_slot* res::detail::get_or_create_resource_slot(res::base::computation_desc desc, cc::span<res::base::res_hash const> args, bool is_impure)
{
    auto& system = res::system();
    auto& base = system.base();

    auto comp = base.define_computation(cc::move(desc));

    base::resource_desc rdesc;
    rdesc.computation = comp;
    rdesc.args = args;
    rdesc.is_impure = is_impure;
    auto [res, counter] = base.define_resource(rdesc);

    auto& mutex = system.m->res_slots_mutex;
    auto& slots = system.m->res_slots;

    mutex.lock_shared();
    auto p_slot = slots.get_ptr(res);
    mutex.unlock_shared();

    // found slot
    if (p_slot)
        return p_slot;

    // prepare resource
    resource_slot slot;
    slot.system = &system;
    slot.resource = res;
    slot.resource_ref_count = counter;

    // write to db
    mutex.lock();
    // need to check again
    p_slot = slots.get_ptr(res);
    if (!p_slot)
    {
        p_slot = &slots[res];
        *p_slot = slot;
    }
    mutex.unlock();

    return p_slot;
}

void const* res::detail::resource_try_get(resource_slot& r)
{
    // TODO: does this function need special threadsafety considerations?
    //    -> we are working with basically immutable values

    auto& base = r.system->base();

    // fastest path: valid cached value
    if (base.is_up_to_date(r.cached_gen))
        return r.cached_value; // might be nullptr

    // otherwise, try to get data
    auto data = base.try_get_resource_content(r.resource);
    if (data.has_value())
    {
        r.cached_value = data.value().data_ptr;
        r.cached_gen = data.value().generation;
    }

    return r.cached_value; // is nullptr if not loaded
}

void res::System::process_all() { base_system.process_all(); }

void res::System::invalidate_impure_resources() { base_system.invalidate_impure_resources(); }

res::System::System() { m = cc::make_unique<pimpl>(); }

res::System::~System()
{
    // TODO
}
