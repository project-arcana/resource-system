#pragma once

#include <clean-core/variant.hh>

#include <resource-system/error.hh>

namespace res
{
/// a result type for resources
/// conceptually really similar to cc::optional
/// however, the empty state is semantically "error" (thus, maybe a variant<T, error>)
/// TODO: maybe cc::result<T, error>?
template <class T>
struct result
{
    result() = default;
    result(T v) : _data(cc::move(v)) {}
    result(res::error e) : _data(cc::move(e)) {}

    bool has_value() const { return _data.template is<T>(); }
    bool has_error() const { return _data.template is<res::error>(); }

    T& value()
    {
        CC_ASSERT(has_value());
        return _data.template get<T>();
    }
    T const& value() const
    {
        CC_ASSERT(has_value());
        return _data.template get<T>();
    }

    res::error& error()
    {
        CC_ASSERT(has_error());
        return _data.template get<res::error>();
    }
    res::error const& error() const
    {
        CC_ASSERT(has_error());
        return _data.template get<res::error>();
    }

private:
    // error first because it might get default constructed
    cc::variant<res::error, T> _data;
};
}