#pragma once

#include <clean-core/unique_ptr.hh>

#include <resource-system/base/api.hh>
#include <resource-system/fwd.hh>

namespace res::detail
{
detail::resource_slot* get_or_create_resource_slot(res::base::computation_desc desc, cc::span<res::base::res_hash const> args, bool is_volatile, bool is_persisted);
}

namespace res
{
/// global resource system singleton
System& system();

/**
 * Resource System (management code)
 */
class System
{
public:
    /// processes all resources that must be loaded
    /// NOTE: this API is WIP
    void process_all();

    base::ResourceSystem& base() { return base_system; }
    base::ResourceSystem const& base() const { return base_system; }

    void invalidate_volatile_resources();

    System();
    ~System();

private:
    base::ResourceSystem base_system;

    // TODO: pimpl?
    struct pimpl;
    cc::unique_ptr<pimpl> m;

    friend detail::resource_slot* detail::get_or_create_resource_slot(res::base::computation_desc desc, cc::span<res::base::res_hash const> args, bool is_volatile);
};
} // namespace res
