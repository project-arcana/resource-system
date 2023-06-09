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

template <class... Args>
struct count_resource_handles
{
    static constexpr int value = ((int(is_handle<std::decay_t<Args>>::value)) + ... + 0);
};

template <class T>
void add_non_resource_to_hash(cc::sha1_builder& sha1, T const& val)
{
    if constexpr (is_handle<T>::value)
    {
        // nothing, is part of resource desc
    }
    else
    {
        static_assert(std::is_trivially_copyable_v<T>, "non-resource arguments must be POD currently. TODO: use meta serialization system");
        sha1.add(cc::as_byte_span(val));
    }
}

template <class T>
void add_resource_to_args(base::res_hash*& p_args, T const& val)
{
    if constexpr (is_handle<T>::value)
    {
        *p_args++ = val.get_hash();
    }
    else
    {
        // nothing
    }
}

resource_slot* get_or_create_resource_slot(res::base::computation_desc desc, cc::span<res::base::res_hash const> args, bool is_volatile, bool is_persisted);

enum class res_type
{
    normal,
    volatile_,
    runtime,
};

template <class T>
decltype(auto) res_eval_arg_from_ptr(void const* p)
{
    // is reference to other resource?
    // -> need to use runtime repr type
    if constexpr (is_handle<T>::value)
    {
        return resource_traits<typename T::resource_t>::from_content_data_ptr(p);
    }
    // otherwise is direct arg
    // -> just use it as-is
    else
    {
        return *reinterpret_cast<T const*>(p);
    }
}

template <class>
struct res_evaluator;

template <size_t... I>
struct res_evaluator<std::integer_sequence<size_t, I...>>
{
    template <class F, class DirectArgTuple, class... Args>
    static auto eval(F const& f, DirectArgTuple const& direct_args, cc::span<base::content_ref const> res_args)
    {
        // TODO: support res::result<T> in args

        void const* ptr_args[sizeof...(Args) + 1]; // the +1 only prevents empty arrays
        void const** p_args = ptr_args;
        auto p_res_args = res_args.data();
        ((*p_args++ =                            // we set each arg in order
          is_handle<std::decay_t<Args>>::value   // if it's a handle
              ? (*p_res_args++).data_ptr         // then we use the next res_arg
              : &direct_args.template get<I>()), // otherwise we take the value from the tuple
         ...);                                   // NOTE: (,...) is needed for guaranteed order

        return cc::invoke(f, detail::res_eval_arg_from_ptr<std::decay_t<Args>>(ptr_args[I])...);
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
    auto constexpr res_arg_count = int(count_resource_handles<Args...>::value);
    base::res_hash res_args[res_arg_count > 0 ? res_arg_count : 1]; // the max(1,..) is to prevent zero sized arrays
    base::res_hash* p_res_args = res_args;
    (detail::add_resource_to_args(p_res_args, args), ...);

    base::computation_desc comp_desc;
    comp_desc.algo_hash = algo_hash;
    comp_desc.compute_resource = [fun = cc::move(fun),                                                      //
                                  arg_tuple = cc::tuple<std::decay_t<Args>...>(cc::forward<Args>(args)...)] //
        (cc::span<base::content_ref const> res_args) -> base::computation_result
    {
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
            ::template eval<std::decay_t<FunT>, decltype(arg_tuple), Args...>(fun, arg_tuple, res_args);

        // unpack / convert / serialize result
        return detail::make_comp_result<ResourceT>(eval_res);
    };

    auto is_volatile = type == res_type::volatile_;
    auto is_persisted = type == res_type::normal;
    auto slot = detail::get_or_create_resource_slot(cc::move(comp_desc), cc::span<base::res_hash>(res_args, res_arg_count), is_volatile, is_persisted);
    return slot->template create_handle<ResourceT>();
}

template <class T>
auto define_constant(T value) -> handle<result_to_resource<T>>
{
    using ResourceT = result_to_resource<T>;

    // TODO: if we have a concept of "injected resources / content", we could do with a single computation

    base::computation_desc comp_desc;
    comp_desc.algo_hash = base::make_random_unique_hash<base::comp_hash>();
    comp_desc.compute_resource = [value = cc::move(value)](cc::span<base::content_ref const> res_args) -> base::computation_result
    {
        CC_ASSERT(res_args.empty());

        // serialize result
        return detail::make_comp_result<ResourceT>(value);
    };

    auto is_volatile = false;
    auto is_persisted = false;
    auto slot = detail::get_or_create_resource_slot(cc::move(comp_desc), {}, is_volatile, is_persisted);
    return slot->template create_handle<ResourceT>();
}

template <class T>
auto wrap_to_handle(T&& v)
{
    if constexpr (is_handle<std::remove_reference_t<T>>::value)
        return v;
    else
        return detail::define_constant(cc::forward<T>(v));
}
}
