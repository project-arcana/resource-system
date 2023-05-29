#include "hash.hh"

#include <clean-core/hash.sha1.hh>
#include <clean-core/string_view.hh>

#include <resource-system/detail/hash_helper.hh>

res::base::type_hash res::base::detail::make_type_hash_from_name(char const* name)
{
    cc::sha1_builder sha1;
    sha1.add(cc::as_byte_span(cc::string_view(name)));
    return res::detail::finalize_as<type_hash>(sha1);
}
