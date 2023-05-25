#include "file.hh"

#include <clean-core/experimental/filewatch.hh>
#include <clean-core/map.hh>

#include <rich-log/log.hh>

#include <babel-serializer/file.hh>

#include <resource-system/define.hh>

res::FileNode res::file;

struct res::FileNode::state
{
    struct reload_info
    {
        cc::filewatch filewatch;
        cc::string filename;
    };

    cc::map<detail::resource*, reload_info> reloads;
};

res::FileNode::FileNode() { _state = cc::make_unique<state>(); }

void res::FileNode::set_hot_reloading(bool enabled) { _hot_reload_enabled = enabled; }

void res::FileNode::check_hot_reloading()
{
    for (auto const& [r, ri] : _state->reloads)
        if (ri.filewatch.has_changed())
        {
            LOG("file '%s' has changed and is invalidated", ri.filename);

            // invalidate
            r->is_loaded = false;
            detail::resource_invalidate_dependers(*r);

            // clear filewatch
            ri.filewatch.set_unchanged();
        }
}

res::result<cc::array<std::byte>> res::FileNode::execute(detail::resource& r, cc::string_view filename) const
{
    //
    return this->execute(r, filename, res::binary);
}

res::result<cc::array<std::byte>> res::FileNode::execute(detail::resource& r, cc::string_view filename, binary_tag) const
{
    LOG("loading binary file '%s'", filename);

    if (!babel::file::exists(filename))
    {
        LOG_WARN("file '%s' does not exist", filename);
        return error::from_user(cc::format("file '%s' does not exist", filename));
    }

    enable_hot_reloading_for(r, filename);
    return babel::file::read_all_bytes(filename);
}

res::result<cc::string> res::FileNode::execute(detail::resource& r, cc::string_view filename, text_tag) const
{
    LOG("loading ascii file '%s'", filename);

    if (!babel::file::exists(filename))
    {
        LOG_WARN("file '%s' does not exist", filename);
        return error::from_user(cc::format("file '%s' does not exist", filename));
    }

    enable_hot_reloading_for(r, filename);
    return babel::file::read_all_text(filename);
}

void res::FileNode::enable_hot_reloading_for(detail::resource& r, cc::string_view filename) const
{
    if (!_hot_reload_enabled)
        return;

    auto& ri = _state->reloads[&r];
    if (ri.filewatch.is_valid())
        return;

    ri.filename = filename;
    ri.filewatch = cc::filewatch::create(filename);
}
