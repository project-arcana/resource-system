#pragma once

#include <clean-core/hash.sha1.hh>

#include <resource-system/base/hash.hh>

namespace res::detail
{
template <class HashT>
HashT finalize_as(cc::sha1_builder& b)
{
    auto sha1_value = b.finalize();
    static_assert(sizeof(sha1_value) >= sizeof(HashT));
    HashT hash;
    std::memcpy(&hash, &sha1_value, sizeof(hash));
    return hash;
}
} // namespace res::detail
