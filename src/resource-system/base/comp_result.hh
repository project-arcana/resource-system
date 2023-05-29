#pragma once

#include <clean-core/function_ptr.hh>
#include <clean-core/optional.hh>
#include <clean-core/string.hh>
#include <clean-core/vector.hh>

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
} // namespace res::base
