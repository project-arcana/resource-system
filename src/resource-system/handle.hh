#pragma once

#include <clean-core/assert.hh>

#include <resource-system/fwd.hh>

namespace res
{
namespace detail
{
bool resource_is_loaded_no_error(detail::resource const& r);
bool resource_try_load(detail::resource& r);
void const* resource_get(detail::resource const& r);
void const* resource_try_get(detail::resource& r);
void resource_invalidate_dependers(detail::resource& r);

template <class T>
detail::resource* resource_from_handle(handle<T> const& h)
{
    return h.resource;
}
}

// TODO: should handle<T> be convertiable in handle<BaseOfT>?
template <class T>
struct handle
{
    /// true if it points to a proper definition, i.e. not moved-out state
    bool is_valid() const { return resource != nullptr; }

    /// true if the resource can be used immediately
    /// NOTE: also implies error-free
    bool is_loaded() const
    {
        CC_ASSERT(is_valid());
        return detail::resource_is_loaded_no_error(*resource);
    }

    /// triggers resource loading if not already loaded
    /// returns is_loaded()
    bool request() const
    {
        CC_ASSERT(is_valid());
        return detail::resource_try_load(*resource);
    }

    /// returns the resource
    /// NOTE: requires "is_loaded()"
    /// TODO: make name more verbose so it's less encouraged?
    // T const& get() const
    // {
    //     CC_ASSERT(is_loaded());
    //     auto data = detail::resource_get(*resource);
    //     CC_ASSERT(data && "data should be loaded");
    //     return *static_cast<T const*>(data);
    // }

    /// equivalent to "return try_load() ? &get() : nullptr"
    /// Usage:
    ///   if (auto d = my_handle.try_get())
    ///       use(*d);
    T const* try_get() const
    {
        auto data = detail::resource_try_get(*resource);
        return static_cast<T const*>(data);
    }

    /// F : (T&) -> bool
    /// returns true on actual change
    template <class F>
    void try_update(F&& update_f)
    {
        // TODO: cleaner
        auto data = (T*)detail::resource_try_get(*resource);
        if (data && update_f(*data))
            detail::resource_invalidate_dependers(*resource);
    }

    handle() = default;
    handle(handle const& rhs)
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
    handle& operator=(handle const& rhs)
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
    friend detail::resource;
    template <class U>
    friend detail::resource* detail::resource_from_handle(handle<U> const& h);

    explicit handle(detail::resource& r) : resource(&r) { acquire(); }

    void acquire()
    {
        if (resource)
        {
            // TODO: ref count stuff
        }
    }
    void release()
    {
        if (resource)
        {
            // TODO: ref count stuff
            resource = nullptr;
        }
    }

private:
    // System has ownership and manages refcounts
    detail::resource* resource = nullptr;
    // TODO: think about caching T* in here
};
}
