#pragma once

#include <clean-core/string.hh>
#include <clean-core/vector.hh>

#include <resource-system/fwd.hh>

namespace res
{
enum class error_type
{
    unknown,
    user,
    missing_resource,
    exception,
};

constexpr char const* to_string(error_type t)
{
    switch (t)
    {
    case error_type::unknown:
        return "unknown";
    case error_type::user:
        return "user";
    case error_type::missing_resource:
        return "missing_resource";
    case error_type::exception:
        return "exception";
    }
    return "<invalid>";
}

struct error
{
    error_type type = error_type::unknown;
    cc::string description;
    cc::vector<detail::resource*> deps; // for missing resources

    static error from_user(cc::string desc)
    {
        error e;
        e.type = error_type::user;
        e.description = cc::move(desc);
        return e;
    }
    static error from_exception(cc::string desc)
    {
        error e;
        e.type = error_type::exception;
        e.description = cc::move(desc);
        return e;
    }
};
}