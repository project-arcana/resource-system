#include "file.hh"

#include <clean-core/experimental/filewatch.hh>
#include <clean-core/map.hh>

#include <rich-log/log.hh>

#include <babel-serializer/file.hh>

#include <resource-system/System.hh>
#include <resource-system/define.hh>

res::FileNode res::file;

struct res::FileNode::state
{
    cc::map<cc::string, cc::filewatch> reloads;
};

res::FileNode::FileNode() { _state = cc::make_unique<state>(); }

void res::FileNode::set_hot_reloading(bool enabled) { _hot_reload_enabled = enabled; }

void res::FileNode::check_hot_reloading()
{
    for (auto&& [fname, fwatch] : _state->reloads)
        if (fwatch.has_changed())
        {
            LOG("file '%s' has changed and is invalidated", fname);

            // invalidate
            res::system().base().invalidate_impure_resources();

            // clear filewatch
            fwatch.set_unchanged();
        }
}

res::result<cc::array<std::byte>> res::FileNode::execute(cc::string_view filename) const
{
    //
    return this->execute(filename, res::binary);
}

res::result<cc::array<std::byte>> res::FileNode::execute(cc::string_view filename, binary_tag) const
{
    LOG("loading binary file '%s'", filename);

    if (!babel::file::exists(filename))
    {
        LOG_WARN("file '%s' does not exist", filename);
        return error::from_user(cc::format("file '%s' does not exist", filename));
    }

    enable_hot_reloading_for(filename);
    return babel::file::read_all_bytes(filename);
}

res::result<cc::string> res::FileNode::execute(cc::string_view filename, text_tag) const
{
    LOG("loading ascii file '%s'", filename);

    if (!babel::file::exists(filename))
    {
        LOG_WARN("file '%s' does not exist", filename);
        return error::from_user(cc::format("file '%s' does not exist", filename));
    }

    enable_hot_reloading_for(filename);
    return babel::file::read_all_text(filename);
}

void res::FileNode::enable_hot_reloading_for(cc::string_view filename) const
{
    if (!_hot_reload_enabled)
        return;

    auto& fwatch = _state->reloads[filename];
    if (fwatch.is_valid())
        return;

    fwatch = cc::filewatch::create(filename);
}
