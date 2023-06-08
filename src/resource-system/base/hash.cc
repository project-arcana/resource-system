#include "hash.hh"

#include <chrono>
#include <thread>

#include <clean-core/hash.sha1.hh>
#include <clean-core/intrinsics.hh>
#include <clean-core/string_view.hh>

#include <resource-system/detail/hash_helper.hh>

res::base::type_hash res::base::detail::make_type_hash_from_name(char const* name)
{
    cc::sha1_builder sha1;
    sha1.add(cc::as_byte_span(cc::string_view(name)));
    return res::detail::finalize_as<type_hash>(sha1);
}

res::base::hash res::base::detail::make_random_unique_hash()
{
    static hash prev_hash = make_type_hash_from_name("globally unique random hash seed");
    static thread_local uint64_t counter = 0;
    cc::sha1_builder sha1;
    sha1.add(cc::as_byte_span(prev_hash));
    sha1.add(cc::as_byte_span(counter));
    sha1.add(cc::as_byte_span(std::hash<std::thread::id>{}(std::this_thread::get_id())));
    sha1.add(cc::as_byte_span(cc::intrin_rdtsc()));
    sha1.add(cc::as_byte_span(std::chrono::high_resolution_clock::now().time_since_epoch().count()));

    auto h = res::detail::finalize_as<hash>(sha1);

    // NOTE: no sync required
    //       teared reads/writes still provide entropy
    prev_hash = h;

    counter++;

    return h;
}
