#pragma once

#include <clean-core/map.hh>
#include <clean-core/set.hh>
#include <clean-core/string.hh>
#include <clean-core/optional.hh>
#include <clean-core/unique_ptr.hh>
#include <clean-core/vector.hh>

#include <mutex>

#include <resource-system/base/hash.hh>
#include <resource-system/base/comp_result.hh>

// simple resource persistence layer for now
// we have to persist two stores:
//     invoc store:   invoc hash -> content hash
//   content store: content hash -> content
//
// the invoc store can have many more entries than the content store
// but the content store in general deals with larger data
//
//

namespace res::persistence
{
struct simple_persistence_config
{
    size_t max_content_size = 20uLL << 30;     // 20 GB
    size_t max_content_file_size = 1uLL << 30; // 1 GB
    size_t max_invoc_count = 1 << 20;          // 1 mio
};

/// very simple file-based persistent
/// - cache GC strategy is "random discard"
/// - no integrity promises
/// - simple compression
///
/// file layout
/// base_dir/
///   invocs.bin (span of invoc hash -> content hash)
///   contents.bin (span of content hash -> content desc)
///   content_data_<i>.bin (span of bytes)
class SimplePersistentStore
{
public:
    explicit SimplePersistentStore(cc::string base_dir, simple_persistence_config cfg = {});
    ~SimplePersistentStore();

    // loads persistence info from file and injects it into the resource system
    // returns false on error
    // non-existing store is currently also returning false (TODO)
    bool load();

    // saves persistence data to disk
    // returns false on error
    bool save();

    // tries to look up missing content
    cc::optional<res::base::computation_result> try_get_content(base::content_hash hash);

    // types
private:
    struct content_info
    {
        uint64_t file : 16;
        uint64_t offset : 48;
        uint64_t size;
    };
    static_assert(sizeof(content_info) == 16);
    struct content_data;

private:
    cc::string invoc_filename() const;
    cc::string content_filename() const;
    cc::string content_data_filename(int file) const;

    void close_open_data();
    void ensure_open_data(uint32_t file);

    res::base::computation_result get_content_from_info(content_info info);

    // config
private:
    simple_persistence_config _config;
    cc::string _base_dir;

    // mutable member
private:
    cc::set<res::base::invoc_hash> _cached_invocs;
    cc::map<res::base::content_hash, content_info> _content;

    // idx is file
    cc::vector<cc::unique_ptr<content_data>> _data;

    // for now simply mutex everything public...
    std::mutex _mutex;

    bool _is_loaded = false;
};

} // namespace res::persistence
