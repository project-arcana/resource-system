#pragma once

// type metamodel for resources

namespace res
{

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
