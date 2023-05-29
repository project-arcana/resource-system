#pragma once

#include <cstdint>

#include <clean-core/hash.hh>

namespace res::base
{
// see https://en.wikipedia.org/wiki/Birthday_problem#Probability_table
// - 128 bit hash
// - 10^10 objects in the database
// - 10^-18 probability of at least one collision
struct alignas(16) hash
{
    uint64_t w0;
    uint64_t w1;

    constexpr bool operator==(hash const& h) const { return w0 == h.w0 && w1 == h.w1; }
    constexpr bool operator!=(hash const& h) const { return w0 != h.w0 || w1 != h.w1; }
};

struct comp_hash : hash
{
};
struct res_hash : hash
{
};
struct content_hash : hash
{
};
struct invoc_hash : hash
{
};
} // namespace res::base

template <>
struct cc::hash<res::base::hash>
{
    constexpr size_t operator()(res::base::hash const& h) const { return h.w0; }
};
template <>
struct cc::hash<res::base::comp_hash>
{
    constexpr size_t operator()(res::base::comp_hash const& h) const { return h.w0; }
};
template <>
struct cc::hash<res::base::res_hash>
{
    constexpr size_t operator()(res::base::res_hash const& h) const { return h.w0; }
};
template <>
struct cc::hash<res::base::content_hash>
{
    constexpr size_t operator()(res::base::content_hash const& h) const { return h.w0; }
};
template <>
struct cc::hash<res::base::invoc_hash>
{
    constexpr size_t operator()(res::base::invoc_hash const& h) const { return h.w0; }
};