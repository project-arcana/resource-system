#include "node.hh"

#include <rich-log/log.hh>

#include <resource-system/detail/log.hh>

#include <mutex>

#include <clean-core/set.hh>
#include <clean-core/string.hh>

res::base::hash res::detail::make_name_version_algo_hash(cc::string_view name, int version)
{
    cc::sha1_builder sha1;
    sha1.add(cc::as_byte_span(0x6FA2D8E4B7C90A1F));
    sha1.add(cc::as_byte_span(version));
    sha1.add(cc::as_byte_span(name));
    return res::detail::finalize_as<base::hash>(sha1);
}

void res::detail::register_node_name(cc::string_view name)
{
    static cc::set<cc::string> names;
    static std::mutex names_mutex;

    auto _ = std::lock_guard(names_mutex);
    if (!names.add(name))
        LOG_ERROR("node name '%s' was already registered! (res::node must have a globally unique name)", name);
}
