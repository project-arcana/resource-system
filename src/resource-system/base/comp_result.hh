#pragma once

#include <clean-core/function_ptr.hh>
#include <clean-core/optional.hh>
#include <clean-core/string.hh>
#include <clean-core/vector.hh>

#include <resource-system/base/hash.hh>

namespace res::base
{

struct content_serialized_data
{
    cc::vector<std::byte> blob;
};
struct content_runtime_data
{
    void* data_ptr;
    cc::function_ptr<void(void*)> deleter; // can be nullptr if data_ptr just points into serialized data
};
struct content_error_data
{
    cc::string message;
};

struct computation_result
{
    cc::optional<content_serialized_data> serialized_data;
    cc::optional<content_runtime_data> runtime_data;
    cc::optional<content_error_data> error_data;
};

// TODO: proper states
//       in memory but not serialized
//       hash only
//       serialized but not deserialized
//       ...
// TODO: refcounting here?
struct content_ref
{
    content_hash hash;

    // generation that this content was computed for
    int generation = -1;

    // if this is a true, then the data is not necessarily the "most current"
    // it's still accessible but will change in the future
    bool is_outdated = false;

    void const* data_ptr = nullptr;

    // can be empty even if data_ptr is non-null
    cc::span<std::byte const> serialized_data;

    // TODO: more elaborate error type?
    cc::string_view error_msg;

    bool has_value() const { return data_ptr != nullptr; }
    bool has_error() const { return data_ptr == nullptr; }
};
} // namespace res::base