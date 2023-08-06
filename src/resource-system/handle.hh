#pragma once

#include <clean-core/assert.hh>

#include <resource-system/base/hash.hh>
#include <resource-system/fwd.hh>

// TODO: how expensive if this header?
#include <clean-core/intrinsics.hh>

namespace res
{
namespace detail
{
bool resource_is_loaded_no_error(detail::resource_slot const& r);
bool resource_try_load(detail::resource_slot& r);
void const* resource_try_get(detail::resource_slot& r);
res::base::res_hash resource_get_hash(detail::resource_slot& r);

template <class T>
detail::resource_slot* resource_from_handle(handle<T> const& h)
{
    return h.resource;
}
} // namespace detail

// TODO: should handle<T> be convertiable in handle<BaseOfT>?
// TODO: api to inspect errors
template <class T>
struct handle
{
    using resource_t = T;

    /// true if it points to a proper definition, i.e. not moved-out state
    bool is_valid() const { return resource != nullptr; }

    /// returns the resource hash identifying this resource
    /// NOTE: must be a valid handle
    base::res_hash get_hash() const
    {
        CC_ASSERT(is_valid());
        return detail::resource_get_hash(*resource);
    }

    /// true if the resource can be used immediately
    /// NOTE: also implies error-free
    /// NOTE: this will only be updated after a try_get!
    /// TODO: this will also be true if an outdated version is cached
    bool is_loaded() const
    {
        CC_ASSERT(is_valid());
        return detail::resource_is_loaded_no_error(*resource);
    }

    /// Usage:
    ///   if (auto d = my_handle.try_get())
    ///       use(*d);
    /// NOTE: will return outdated cached values
    ///      (but recomputation is still triggered)
    /// NOTE: using this on an invalid handle is fine (and returns nullptr)
    T const* try_get() const
    {
        if (!resource)
            return nullptr;
        auto data = detail::resource_try_get(*resource);
        return static_cast<T const*>(data);
    }

    handle() = default;
    handle(handle const& rhs) noexcept
    {
        resource = rhs.resource;
        acquire();
    }
    handle(handle&& rhs) noexcept
    {
        resource = rhs.resource;
        rhs.resource = nullptr;
        // no acquire because we steal from rhs
    }
    handle& operator=(handle const& rhs) noexcept
    {
        if (this != &rhs)
        {
            resource = rhs.resource;
            acquire();
        }
        return *this;
    }
    handle& operator=(handle&& rhs) noexcept
    {
        release();
        resource = rhs.resource;
        rhs.resource = nullptr;
        // no acquire because we steal from rhs
        return *this;
    }
    ~handle() { release(); }

private:
    friend detail::resource_slot;
    template <class U>
    friend detail::resource_slot* detail::resource_from_handle(handle<U> const& h);

    explicit handle(detail::resource_slot* r) : resource(r) { acquire(); }

    void acquire()
    {
        if (resource)
        {
            cc::intrin_atomic_add((int volatile*)resource, 1);
        }
    }
    void release()
    {
        if (resource)
        {
            // TODO: cleanup is performed by system GC
            cc::intrin_atomic_add((int volatile*)resource, -1);
            resource = nullptr;
        }
    }

private:
    // System has ownership and manages refcounts
    detail::resource_slot* resource = nullptr;
    // TODO: think about caching T* in here
};
} // namespace res
