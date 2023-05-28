#pragma once

#include <resource-system/detail/log.hh>

namespace res
{
template <class T>
struct handle;

template <class T>
struct result;

struct error;

class Node;
class System;

namespace detail
{
struct resource_slot;
}

namespace base
{
struct comp_hash;
struct res_hash;
struct content_hash;
struct invoc_hash;

class ResourceSystem;
} // namespace base
} // namespace res
