#pragma once

#include <type_traits>

#include <clean-core/allocate.hh>
#include <clean-core/always_false.hh>
#include <clean-core/collection_traits.hh>
#include <clean-core/fwd.hh>
#include <clean-core/is_contiguous_range.hh>
#include <clean-core/span.hh>

// needed for res_traits
#include <resource-system/base/comp_result.hh>
#include <resource-system/fwd.hh>

// type metamodels for resources

namespace res
{

namespace detail
{
template <class T>
struct is_handle : std::false_type
{
};
template <class T>
struct is_handle<handle<T>> : std::true_type
{
};

template <class T>
struct is_result : std::false_type
{
};
template <class T>
struct is_result<result<T>> : std::true_type
{
};

template <class T>
struct is_span : std::false_type
{
};
template <class T>
struct is_span<cc::span<T>> : std::true_type
{
};
} // namespace detail

/// metamodel for result types
/// results are what a fun in res::define("...", fun, ...) can return
/// resources are what handle<T> hold
template <class T, class = void>
struct result_traits
{
    static_assert(!std::is_reference_v<T>, "forgot to call decay_t?");

    static auto impl_to_resource(T* p)
    {
        if constexpr (detail::is_handle<T>::value)
        {
            return result_traits<typename T::resource_t>::impl_to_resource(nullptr);
        }
        else if constexpr (detail::is_result<T>::value)
        {
            return result_traits<typename T::value_t>::impl_to_resource(nullptr);
        }
        else if constexpr (std::is_pointer_v<T>)
        {
            static_assert(cc::always_false<T>, "pointers are not allowed as result types as it's too easy to make mistakes");
        }
        else if constexpr (std::is_same_v<T, cc::string>)
        {
            return cc::string_view{};
        }
        else if constexpr (cc::is_any_contiguous_range_of_pods<T>)
        {
            using ElementT = std::decay_t<decltype(*std::declval<T>().data())>;
            return cc::span<ElementT const>{};
        }
        else if constexpr (std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T>)
        {
            return T{};
        }
        else // return as-is
        {
            return *p;
        }
    }

    using resource_t = std::decay_t<decltype(impl_to_resource(nullptr))>;
};

namespace detail
{
// int -> int
// handle<float> -> float
// result<bool> -> bool
// array<char> -> span<char>
// string -> string_view
// ...
template <class T>
using result_to_resource = typename result_traits<std::decay_t<T>>::resource_t;

template <class T>
struct arg_to_resource_t
{
    using type = T;
};
template <class T>
struct arg_to_resource_t<handle<T>>
{
    using type = typename arg_to_resource_t<T>::type;
};
template <class T>
using arg_to_resource = typename arg_to_resource_t<std::decay_t<T>>::type;
} // namespace detail

/// metamodel for resource types
/// includes:
///  - serialization
///  - hashing
///  - pretty printing
template <class T, class = void>
struct resource_traits
{
    // reads the raw resource from a void ptr
    // concretely:
    //   takes content_ref::data_ptr
    //   and converts it to something that can be passed to the computation function
    // TODO: if we pass serialized span, we could skip some allocs
    static auto from_content_ref(base::content_ref const& content) { return *reinterpret_cast<T const*>(content.data_ptr); }

    template <class ResultT>
    static void make_comp_result(base::computation_result& res, ResultT value)
    {
        if constexpr (detail::is_span<T>::value)
        {
            static_assert(cc::is_any_contiguous_range_of_pods<ResultT>);

            // serialize
            {
                base::content_serialized_data ser;
                auto bindata = cc::as_byte_span(value);
                ser.blob = cc::vector<std::byte>::uninitialized(bindata.size());
                std::memcpy(ser.blob.data(), bindata.data(), bindata.size());
                res.serialized_data = cc::move(ser);
            }

            // runtime data
            {
                using element_t = cc::collection_element_t<ResultT>;
                base::content_runtime_data data;
                data.data_ptr = cc::alloc<T>(cc::as_byte_span(res.serialized_data.value().blob).reinterpret_as<element_t const>());
                data.deleter = [](void* p) { cc::free(reinterpret_cast<T*>(p)); };
                res.runtime_data = cc::move(data);
            }
        }
        else if constexpr (std::is_same_v<T, cc::string_view>)
        {
            // serialize
            {
                base::content_serialized_data ser;
                auto bindata = cc::as_byte_span(cc::string_view(value));
                ser.blob = cc::vector<std::byte>::uninitialized(bindata.size());
                std::memcpy(ser.blob.data(), bindata.data(), bindata.size());
                res.serialized_data = cc::move(ser);
            }

            // runtime data
            {
                base::content_runtime_data data;
                auto const& blob = res.serialized_data.value().blob;
                data.data_ptr = cc::alloc<cc::string_view>(cc::string_view((char const*)blob.data(), blob.size()));
                data.deleter = [](void* p) { cc::free(reinterpret_cast<cc::string_view*>(p)); };
                res.runtime_data = cc::move(data);
            }
        }
        else if constexpr (std::is_trivially_copyable_v<T> && std::is_same_v<ResultT, T>)
        {
            // serialize
            {
                base::content_serialized_data ser;
                auto bindata = cc::as_byte_span(value);
                ser.blob = cc::vector<std::byte>::uninitialized(bindata.size());
                std::memcpy(ser.blob.data(), bindata.data(), bindata.size());
                res.serialized_data = cc::move(ser);
            }

            // runtime data
            {
                base::content_runtime_data data;
                data.data_ptr = res.serialized_data.value().blob.data();
                data.deleter = nullptr; // point directly into serialized data
                res.runtime_data = cc::move(data);
            }
        }
        else // non-serializable
        {
            base::content_runtime_data data;
            data.data_ptr = cc::alloc<T>(cc::move(value));
            data.deleter = [](void* p) { cc::free(reinterpret_cast<T*>(p)); };
            res.runtime_data = cc::move(data);
        }
    }
};

/// metamodel for the opt-in node protocol
///
/// usage:
///
///   struct my_node
///   {
///       template <class... Args>
///       auto define_resource(cc::string_view name, Args&&... args)
///       {
///           // ...
///           // e.g. use res::detail::define_res_via_lambda
///       }
///
///       // or overload exactly what you need
///   };
///
///   template <>
///   struct res::node_traits<my_node>
///   {
///       static constexpr bool is_node = true;
///   };
template <class T, class = void>
struct node_traits
{
    static constexpr bool is_node = false;
};

} // namespace res
