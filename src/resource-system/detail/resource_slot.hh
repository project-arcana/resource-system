#pragma once

#include <cstddef>

#include <resource-system/base/api.hh>
#include <resource-system/fwd.hh>

namespace res::detail
{
struct resource_slot
{
    // CAUTION: must be first location
    int volatile ref_count = 1;

    int cached_gen = -1;

    // backreference to resource system
    System* system = nullptr;

    base::res_hash resource;

    void const* cached_value = nullptr;

    base::ref_count* resource_ref_count = nullptr;

    template <class T>
    handle<T> create_handle()
    {
        return handle<T>(this);
    }
};
static_assert(offsetof(resource_slot, ref_count) == 0, "we assume opaquely that the first member is the ref count");
} // namespace res::detail
