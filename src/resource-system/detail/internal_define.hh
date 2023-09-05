#pragma once

#include <clean-core/allocate.hh>
#include <clean-core/always_false.hh>
#include <clean-core/apply.hh>
#include <clean-core/forward.hh>
#include <clean-core/move.hh>
#include <clean-core/tuple.hh>

#include <clean-core/hash.sha1.hh>

// TODO: slim down to the api part we need
#include <resource-system/base/api.hh>

#include <resource-system/handle.hh>
#include <resource-system/meta.hh>

// currently we need to include this
// which includes cc::string
#include <resource-system/result.hh>

#include <resource-system/detail/hash_helper.hh>
#include <resource-system/detail/resource_slot.hh>

// used for serialized typeinfo right now
#include <typeinfo>

namespace res::detail
{
resource_slot* get_or_create_resource_slot(
    res::base::computation_desc desc, cc::span<res::base::res_hash const> args, bool is_volatile, bool is_persisted, base::deserialize_fun_ptr deserialize);

enum class res_type
{
    normal,
    volatile_,
    runtime,
};

template <class T>
T const& get_resource_arg(base::content_ref const& content)
{
    CC_ASSERT(content.has_runtime_data());
    return resource_traits<T>::from_content_data_ptr(content.data_ptr);
}

template <class>
struct res_evaluator;

template <size_t... I>
struct res_evaluator<std::integer_sequence<size_t, I...>>
{
    template <class F, class... ResArgs>
    static auto eval(F const& f, cc::span<base::content_ref const> res_args)
    {
        CC_ASSERT(res_args.size() == sizeof...(ResArgs));
        // TODO: support res::result<T> in args

        return cc::invoke(f, detail::get_resource_arg<ResArgs>(res_args[I])...);
    }
};

template <class ResourceT, class ResultT>
base::computation_result make_comp_result(ResultT value)
{
    static_assert(!is_handle<ResultT>::value, "TODO: implement indirect resources");

    // result<ResourceT>
    if constexpr (is_result<ResultT>::value)
    {
        if (value.has_value())
        {
            return detail::make_comp_result<ResourceT>(cc::move(value.value()));
        }
        else
        {
            base::computation_result res;
            base::content_error_data err;
            err.message = value.error().to_string();
            res.error_data = cc::move(err);
            return res;
        }
    }
    else // just ResourceT
    {
        base::computation_result res;
        resource_traits<ResourceT>::make_comp_result(res, cc::move(value));
        return res;
    }
}

template <class T>
auto define_constant(T value) -> handle<const_to_resource<T>>
{
    using ResourceT = const_to_resource<T>;

    // TODO: if we have a concept of "injected resources / content", we could do with a single computation

    base::computation_desc comp_desc;
    comp_desc.algo_hash = base::make_random_unique_hash<base::comp_hash>();
    comp_desc.compute_resource =
        // ensure we make a copy of "view types"
        [value = arg_traits<T>::make_const_val(cc::move(value))] //
        (cc::span<base::content_ref const> res_args) -> base::computation_result
    {
        CC_ASSERT(res_args.empty());

        // serialize result
        return detail::make_comp_result<ResourceT>(value);
    };

    auto is_volatile = false;
    auto is_persisted = false;
    auto slot = detail::get_or_create_resource_slot(cc::move(comp_desc), {}, is_volatile, is_persisted, resource_traits<ResourceT>::make_deserialize());
    return slot->template create_handle<ResourceT>();
}

template <class T>
auto wrap_to_handle(T&& v)
{
    if constexpr (is_handle<std::decay_t<T>>::value)
        return v;
    else
        return detail::define_constant(cc::forward<T>(v));
}

template <class... ResArgs>
base::hash get_arg_type_hash()
{
    static base::hash hash = []
    {
        cc::sha1_builder sha1;
        (sha1.add(cc::as_byte_span(base::get_type_hash<ResArgs>())), ...);
        return detail::finalize_as<base::hash>(sha1);
    }();
    return hash;
}

template <class FunT, class... Args>
auto define_res_via_lambda(base::hash algo_hash, res_type type, FunT&& fun, Args&&... args)
{
    static_assert(std::is_trivially_copyable_v<std::decay_t<FunT>>,                 //
                  "callables must be POD-like, i.e. all captures must be trivial. " //
                  "non-trivial captures should be provided as function argument.");

    static_assert(std::is_invocable_v<FunT, arg_to_resource<Args> const&...>, //
                  "function is not callable with the correct argument types");

    using ResultT = std::decay_t<std::invoke_result_t<FunT, arg_to_resource<Args> const&...>>;
    static_assert(!is_handle<ResultT>::value, "TODO: implement indirect resources");
    using ResourceT = result_to_resource<ResultT>;

    // collect resource handles
    auto constexpr res_arg_count = sizeof...(args);
    base::res_hash res_args[res_arg_count > 0 ? res_arg_count : 1]; // the max(1,..) is to prevent zero sized arrays
    base::res_hash* p_res_args = res_args;
    ((*p_res_args++ = detail::wrap_to_handle(cc::forward<Args>(args)).get_hash()), ...);

    base::computation_desc comp_desc;
    comp_desc.algo_hash = algo_hash;
    comp_desc.type_hash = detail::get_arg_type_hash<arg_to_resource<Args>...>();
    comp_desc.compute_resource = [fun = cc::move(fun)] //
        (cc::span<base::content_ref const> res_args) -> base::computation_result
    {
        CC_ASSERT(res_args.size() == sizeof...(Args) && "wrong number of inputs");

        // if any arg is missing, result is error
        for (auto const& arg : res_args)
            if (arg.has_error())
            {
                base::computation_result res;
                base::content_error_data err;
                err.message = "at least one dependency had an error";
                res.error_data = cc::move(err);
                return res;
            }

        // actual eval
        auto eval_res = res_evaluator<std::make_index_sequence<sizeof...(Args)>> //
            ::template eval<std::decay_t<FunT>, arg_to_resource<Args>...>(fun, res_args);

        // unpack / convert / serialize result
        return detail::make_comp_result<ResourceT>(eval_res);
    };

    auto is_volatile = type == res_type::volatile_;
    auto is_persisted = type == res_type::normal;
    auto slot = detail::get_or_create_resource_slot(cc::move(comp_desc), cc::span<base::res_hash>(res_args, res_arg_count), is_volatile, is_persisted,
                                                    resource_traits<ResourceT>::make_deserialize());
    return slot->template create_handle<ResourceT>();
}
} // namespace res::detail
