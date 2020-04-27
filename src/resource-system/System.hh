#pragma once

#include <clean-core/set.hh>

#include <resource-system/Node.hh>
#include <resource-system/detail/resource.hh>

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
    void trigger_load(detail::resource& r);

    /// processes all resources that must be loaded
    /// NOTE: this API is WIP
    void process_all();

private:
    cc::set<detail::resource*> resources_to_load;
};
}
