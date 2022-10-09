#pragma once

#include <clean-core/string.hh>
#include <clean-core/string_view.hh>
#include <clean-core/unique_ptr.hh>

#include <resource-system/Node.hh>
#include <resource-system/define.hh>
#include <resource-system/result.hh>
#include <resource-system/tags.hh>

namespace res
{

class FileNode;

/// a File node type with auto-reload via watcher
/// - define(res::file, ...)
/// - load(res::file, ...)
///
/// usage:
///
///   auto f0 = res::define(res::file, "/path/to/res");              -- returns array of bytes
///   auto f1 = res::define(res::file, "/path/to/res", res::binary); -- same as before
///   auto f2 = res::define(res::file, "/path/to/res", res::ascii);  -- returns a string
///
/// similarly, can be used to access resources from a virtual file system
///
extern FileNode file;

class FileNode : public Node
{
public:
    FileNode();

    // NOTE: only applies to files loaded after this call
    void set_hot_reloading(bool enabled);

    // Checks for files to hot-reload
    // This is usually called by the resource system and manual calls are not required
    void check_hot_reloading();

public:
    // TODO:
    //  - support for virtual files
    //  - support for search paths
    result<cc::array<std::byte>> execute(detail::resource& r, cc::string_view filename) const;
    result<cc::array<std::byte>> execute(detail::resource& r, cc::string_view filename, binary_tag) const;
    result<cc::string> execute(detail::resource& r, cc::string_view filename, ascii_tag) const;

private:
    bool _hot_reload_enabled =
#ifdef CC_RELEASE
        false
#else
        true
#endif
        ;

    struct state;
    cc::unique_ptr<state> _state;

    // TODO: disable hot reloading for deleted resources
    void enable_hot_reloading_for(detail::resource& r, cc::string_view filename) const;
};
}
