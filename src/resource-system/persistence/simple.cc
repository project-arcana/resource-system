#include "simple.hh"

#include <filesystem>
#include <fstream>

#include <clean-core/format.hh>
#include <clean-core/utility.hh>

#include <rich-log/log.hh>

#include <resource-system/System.hh>
#include <resource-system/detail/log.hh>

#include <babel-serializer/compression/zstd.hh>
#include <babel-serializer/file.hh>

namespace res
{
namespace
{
bool append_to_file_or_create(cc::string_view filename, cc::span<std::byte const> data)
{
    if (data.empty())
        return true;

    auto path = std::filesystem::path(cc::string(filename).c_str());
    auto file = std::ofstream(path, std::ios::binary | std::ios::app);
    if (!file.is_open())
    {
        std::filesystem::create_directories(path.parent_path());
        LOG("creating '%s'", filename);
        babel::file::write(filename, data);
        return true;
    }

    file.write((char const*)data.data(), data.size());
    file.close();
    return true;
}
} // namespace
} // namespace res

struct res::persistence::SimplePersistentStore::content_data
{
    explicit content_data(cc::string_view path) { data = babel::file::make_memory_mapped_file_readonly(path); }

    babel::file::memory_mapped_file<std::byte const> data;
};

res::persistence::SimplePersistentStore::SimplePersistentStore(cc::string base_dir, simple_persistence_config cfg) : _config(cfg), _base_dir(base_dir)
{
}

res::persistence::SimplePersistentStore::~SimplePersistentStore() = default;

bool res::persistence::SimplePersistentStore::load()
{
    auto lock = std::unique_lock(_mutex);
    CC_ASSERT(!_is_loaded && "cannot load twice for now");
    _is_loaded = true;

    _cached_invocs.clear();
    _content.clear();
    _data.clear();

    auto const invoc_file = invoc_filename();
    auto const content_file = content_filename();

    if (!babel::file::exists(invoc_file) || !babel::file::exists(content_file))
    {
        LOG("no existing persistency data found in '%s'", _base_dir);
        return false;
    }

    // register as fallback provider
    res::system().base().inject_content_provider([this](base::content_hash hash) { return this->try_get_content(hash); });

    // read and add invocation cache data
    auto invoc_data = babel::file::make_memory_mapped_file_readonly(invoc_file);
    auto invocs = cc::span(invoc_data).reinterpret_as<cc::pair<base::invoc_hash, base::content_hash> const>();
    res::system().base().inject_invoc_cache(invocs);
    for (auto const& [invoc, content] : invocs)
        _cached_invocs.add(invoc);

    // read and add content cache data
    auto content_data = babel::file::make_memory_mapped_file_readonly(content_file);
    auto contents = cc::span(content_data).reinterpret_as<cc::pair<base::content_hash, content_info> const>();
    uint64_t max_file = 0;
    for (auto [content, info] : contents)
    {
        _content[content] = info;
        max_file = cc::max(max_file, info.file);
    }
    if (max_file > 200)
    {
        LOG_ERROR("too many content files referenced in '%s'. indicating corruption", content_file);
        return false;
    }

    // accumulate file sizes
    size_t content_data_size = 0;
    for (auto i = 0u; i <= max_file; ++i)
    {
        auto filename = content_data_filename(i);
        if (babel::file::exists(filename))
            content_data_size += babel::file::size_of(filename);
    }

    LOG("using persistency cache (%s invocs, %s contents, %.2f MB)", invocs.size(), contents.size(), content_data_size / 1024. / 1024.);
    return true;
}

bool res::persistence::SimplePersistentStore::save()
{
    auto lock = std::unique_lock(_mutex);

    // close open mmapped files
    _data.clear();

    // TODO: GC + limits

    // save invocs
    auto new_invocs = res::system().base().collect_all_persistent_invocations(_cached_invocs);
    append_to_file_or_create(invoc_filename(), cc::as_byte_span(new_invocs));
    // LOG("written %s new cached invocs to '%s'", new_invocs.size(), invoc_filename());

    // compute new content
    cc::vector<base::content_hash> content_to_query;
    for (auto const& [invoc, content] : new_invocs)
    {
        if (_content.contains_key(content))
            continue; // already cached

        content_to_query.push_back(content);
    }
    auto content_res = res::system().base().collect_all_persistent_content(content_to_query);

    // write data and collect contents update
    cc::vector<cc::pair<base::content_hash, content_info>> new_contents;
    struct file_writer
    {
        explicit file_writer(int idx, cc::string_view filename, size_t max_size)
        {
            this->idx = idx;
            bytes_left = max_size;

            // create file and dir
            auto path = std::filesystem::path(cc::string(filename).c_str());
            if (!babel::file::exists(filename))
            {
                std::filesystem::create_directories(path.parent_path());
                LOG("creating '%s'", filename);
            }
            else
            {
                auto curr_size = babel::file::size_of(filename);
                if (curr_size > max_size)
                    bytes_left = 0;
                else
                    bytes_left = max_size - curr_size;
            }

            file = std::ofstream(path, std::ios::binary | std::ios::app);
            CC_ASSERT(file.is_open());
        }

        // returns nullif not enough space
        cc::optional<content_info> write(base::content_ref const& content)
        {
            if (bytes_left == 0)
                return cc::nullopt;

            content_info info;
            info.file = idx;
            info.offset = file.tellp();

            auto write_error = [&]
            {
                CC_ASSERT(content.has_error());

                char const type = 'E';
                file.write(&type, 1);

                file.write(content.error_msg.data(), content.error_msg.size());
            };
            auto write_content_raw = [&]
            {
                CC_ASSERT(content.has_value());

                char const type = 'V';
                file.write(&type, 1);

                file.write((char const*)content.serialized_data.data(), content.serialized_data.size());
            };
            auto write_content_compressed = [&](cc::span<std::byte const> cdata)
            {
                CC_ASSERT(content.has_value());

                char const type = 'v';
                file.write(&type, 1);

                file.write((char const*)cdata.data(), cdata.size());
            };

            if (content.has_value())
            {
                // no compression below 1 kb
                if (content.serialized_data.size() <= 1024)
                {
                    write_content_raw();
                }
                else
                {
                    auto compr = babel::zstd::compress(content.serialized_data);
                    auto saved_bytes = int64_t(compr.size()) - int64_t(content.serialized_data.size());
                    // need to save at least 10% and 1kb
                    auto min_saved_bytes = cc::max(int64_t(1024), int64_t(content.serialized_data.size() / 10));
                    if (saved_bytes < min_saved_bytes)
                        write_content_raw();
                    else
                        write_content_compressed(compr);
                }
            }
            else
            {
                write_error();
            }

            info.size = int64_t(file.tellp()) - info.offset;
            bytes_left -= info.size;
            return info;
        }

        int idx;
        std::ofstream file;
        size_t bytes_left;
    };
    cc::vector<file_writer> writers;
    for (auto const& content : content_res)
    {
        auto needs_new = true;

        for (auto& writer : writers)
            if (auto info = writer.write(content); info.has_value())
            {
                needs_new = false;
                new_contents.emplace_back(content.hash, info.value());
                break;
            }

        if (needs_new)
        {
            auto fidx = int(writers.size());
            writers.emplace_back(fidx, content_data_filename(fidx), _config.max_content_file_size);
            auto info = writers.back().write(content);
            if (info.has_value())
            {
                new_contents.emplace_back(content.hash, info.value());
            }
            else
                LOG_WARN("could not write content to '%s'", content_data_filename(fidx));
        }
    }

    size_t new_content_total_size = 0;
    for (auto&& [content, info] : new_contents)
        new_content_total_size += info.size;

    // save contents
    append_to_file_or_create(content_filename(), cc::as_byte_span(new_contents));
    // LOG("written %s new cached contents to '%s' (%.2f MB)", new_contents.size(), content_filename(), new_content_total_size / 1024. / 1024.);

    if (!new_invocs.empty() || !new_contents.empty())
        LOG("updated persistency cache (+%s invocs, +%s contents, +%.2f MB)", new_invocs.size(), new_contents.size(), new_content_total_size / 1024. / 1024.);

    return true;
}

cc::optional<res::base::computation_result> res::persistence::SimplePersistentStore::try_get_content(base::content_hash hash)
{
    // TODO: less locking?
    auto lock = std::unique_lock(_mutex);

    // look up info
    auto p_info = _content.get_ptr(hash);
    if (!p_info)
        return cc::nullopt; // not found

    return this->get_content_from_info(*p_info);
}

res::base::computation_result res::persistence::SimplePersistentStore::get_content_from_info(content_info info)
{
    CC_ASSERT(info.size > 1);

    // ensure file is mmapped
    this->ensure_open_data(info.file);

    auto raw_data = cc::span(_data[info.file]->data).subspan(info.offset, info.size);
    auto type = char(raw_data[0]);

    switch (type)
    {
    case 'V':
    {
        res::base::computation_result res;
        res::base::content_serialized_data data;
        data.blob = cc::vector<std::byte>(raw_data.subspan(1));
        res.serialized_data = cc::move(data);
        return res;
    }
    case 'v':
    {
        res::base::computation_result res;
        res::base::content_serialized_data data;
        data.blob = babel::zstd::uncompress(raw_data.subspan(1));
        res.serialized_data = cc::move(data);
        return res;
    }
    case 'E':
    {
        res::base::computation_result res;
        res::base::content_error_data data;
        data.message = cc::string_view((char const*)raw_data.data() + 1, raw_data.size() - 1);
        res.error_data = cc::move(data);
        return res;
    }
    }

    CC_UNREACHABLE("unknown type. corrupted file?");
    return {};
}

cc::string res::persistence::SimplePersistentStore::invoc_filename() const { return _base_dir + "/invocs.bin"; }

cc::string res::persistence::SimplePersistentStore::content_filename() const { return _base_dir + "/contents.bin"; }

cc::string res::persistence::SimplePersistentStore::content_data_filename(int file) const
{
    return cc::format("%s/content_data_%s.bin", _base_dir, file);
}

void res::persistence::SimplePersistentStore::close_open_data() { _data.clear(); }

void res::persistence::SimplePersistentStore::ensure_open_data(uint32_t file)
{
    // make place for slots
    while (file >= _data.size())
        _data.push_back(nullptr);

    // already open?
    if (_data[file] != nullptr)
        return;

    _data[file] = cc::make_unique<content_data>(content_data_filename(file));
}
