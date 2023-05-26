#include "api.hh"

#include <clean-core/experimental/ringbuffer.hh>
#include <clean-core/hash.sha1.hh>
#include <clean-core/indices_of.hh>
#include <clean-core/intrinsics.hh>
#include <clean-core/map.hh>
#include <clean-core/vector.hh>

#include <rich-log/log.hh>

#include <resource-system/detail/log.hh>

#include <shared_mutex>

namespace res::base
{
namespace
{
// TODO: flat?
struct res_desc
{
    comp_hash comp;
    cc::vector<res_hash> args;

    // cache for resources
    bool is_enqueued = false;
    int content_gen = -1;
    content_hash content_name;
    content_data content_data; // TODO: refcounting
};

struct content_desc
{
    cc::array<std::byte> data_backing_buffer;
    content_data data;
};

// TODO: flat?
struct invoc_desc
{
    // TODO: do we really need to store this?
    // comp_hash comp;
    // cc::vector<content_hash> args;

    content_hash content;

    // cached shortcut
    // is not persistet
    content_data data; // TODO: refcounting
};

struct res_hash_hasher
{
    size_t operator()(res::base::hash const& h) const { return h.w0; }
};

template <class HashT>
HashT finalize_as(cc::sha1_builder& b)
{
    auto sha1_value = b.finalize();
    static_assert(sizeof(sha1_value) >= sizeof(HashT));
    HashT hash;
    std::memcpy(&hash, &sha1_value, sizeof(hash));
    return hash;
}
} // namespace
} // namespace res::base

struct res::base::ResourceSystem::impl
{
    // TODO

    // for now, we need comp/res maps completely in memory
    // so we know how to compute every resource
    cc::map<comp_hash, computation_desc, res_hash_hasher> comp_desc_by_hash;
    cc::map<res_hash, res_desc, res_hash_hasher> res_desc_by_hash;

    // these two are the "data caches"
    cc::map<content_hash, content_desc, res_hash_hasher> content_desc_by_hash;
    cc::map<invoc_hash, invoc_desc, res_hash_hasher> invoc_desc_by_hash;

    // db sync
    std::shared_mutex comp_desc_by_hash_mutex;
    std::shared_mutex res_desc_by_hash_mutex;
    std::shared_mutex content_desc_by_hash_mutex;
    std::shared_mutex invoc_desc_by_hash_mutex;

    // queue
    // TODO: mpmc with grow?
    cc::ringbuffer<res_hash> queue_resource_to_compute;
    std::mutex queue_resource_to_compute_mutex;
};

res::base::ResourceSystem::ResourceSystem() { m = cc::make_unique<impl>(); }

res::base::ResourceSystem::~ResourceSystem() = default;

res::base::comp_hash res::base::ResourceSystem::define_computation(computation_desc desc)
{
    // make hash
    cc::sha1_builder sha1;
    sha1.add(cc::as_byte_span(desc.name));
    sha1.add(cc::as_byte_span(desc.algo_hash));
    // TODO: args?
    auto const hash = finalize_as<comp_hash>(sha1);

    auto& db = m->comp_desc_by_hash;
    auto& mutex = m->comp_desc_by_hash_mutex;

    // read: check if comp already known
    mutex.lock_shared();
    auto const has_value = db.contains_key(hash);
    // TODO: verify if stored desc is the same
    mutex.unlock_shared();

    // write: add to map
    if (!has_value)
    {
        mutex.lock();
        db[hash] = cc::move(desc);
        mutex.unlock();
    }

    return hash;
}

res::base::res_hash res::base::ResourceSystem::define_resource(comp_hash const& computation, cc::span<res_hash const> args)
{
    // make hash
    cc::sha1_builder sha1;
    sha1.add(cc::as_byte_span(computation));
    for (auto const& h : args)
        sha1.add(cc::as_byte_span(h));
    auto const hash = finalize_as<res_hash>(sha1);

    auto& db = m->res_desc_by_hash;
    auto& mutex = m->res_desc_by_hash_mutex;

    // read: check if comp already known
    mutex.lock_shared();
    auto const has_value = db.contains_key(hash);
    mutex.unlock_shared();

    // write: add to map
    if (!has_value)
    {
        res_desc desc;
        desc.comp = computation;
        desc.args.push_back_range(args);

        mutex.lock();
        db[hash] = cc::move(desc);
        mutex.unlock();
    }

    return hash;
}

cc::optional<res::base::content_data> res::base::ResourceSystem::try_get_resource_content(res_hash res, bool start_computation_if_not_found)
{
    cc::optional<content_data> result;
    auto need_compute = false;
    int const target_generation = generation;

    auto& db = m->res_desc_by_hash;
    auto& mutex = m->res_desc_by_hash_mutex;

    // 1. read-only res db lookup
    //    this is the fast path
    mutex.lock_shared();
    auto p_desc = db.get_ptr(res);
    if (p_desc)
    {
        // see if cached version found
        if (p_desc->content_gen == target_generation)
        {
            // we guarantee that this is the stored data
            result = p_desc->content_data;
        }
        else
        {
            // cached content is either :
            // - outdated
            // - computed but not cached
            // - not computed
            need_compute = true;
        }
    }
    mutex.unlock_shared();

    if (!p_desc)
    {
        LOG_ERROR("no resource known with id %s", cc::as_byte_span(res));
        return result;
    }

    // 2. if no cached data found, trigger computation
    if (need_compute && start_computation_if_not_found)
    {
        // set to enqueued in res db
        mutex.lock();
        p_desc = db.get_ptr(res);
        CC_ASSERT(p_desc && "overzealous GC?");
        auto need_enqueue = false;
        if (!p_desc->is_enqueued)
        {
            need_enqueue = true;
            p_desc->is_enqueued = true;
        }
        mutex.unlock();

        // actually enqueue
        if (need_enqueue)
        {
            auto& queue = m->queue_resource_to_compute;
            auto& queue_mutex = m->queue_resource_to_compute_mutex;

            queue_mutex.lock();
            queue.push_back(res);
            queue_mutex.unlock();
        }
    }

    return result;
}

void res::base::ResourceSystem::invalidate_impure_resources()
{
    // simple for now
    cc::intrin_atomic_add(&generation, 1);
}

void res::base::ResourceSystem::process_all()
{
    auto& queue = m->queue_resource_to_compute;
    auto& queue_mutex = m->queue_resource_to_compute_mutex;

    auto& res_db = m->res_desc_by_hash;
    auto& res_db_mutex = m->res_desc_by_hash_mutex;

    auto& invoc_db = m->invoc_desc_by_hash;
    auto& invoc_db_mutex = m->invoc_desc_by_hash_mutex;

    cc::vector<res_hash> args;
    cc::vector<content_data> args_content;
    cc::vector<content_hash> args_content_hashes;

    // not locked because this is fine to be approximative
    while (!queue.empty())
    {
        res_hash res;
        auto has_res = true;

        // get job
        queue_mutex.lock();
        if (!queue.empty())
            res = queue.pop_front();
        else
            has_res = false;
        queue_mutex.unlock();

        if (!has_res)
            break; // spurior non-empty

        // get job info job
        args.clear();
        comp_hash comp;
        auto gen = generation;

        // TODO: is it better to look up dependencies directly here?
        // TODO: do we need to check if it's already up to date here?
        res_db_mutex.lock_shared();
        auto p_desc = res_db.get_ptr(res);
        CC_ASSERT(p_desc && "overzealous GC?");
        args.push_back_range(p_desc->args);
        comp = p_desc->comp;
        res_db_mutex.unlock_shared();

        // query arg content
        // NOTE: we query all args, even if some are missing
        //       this reduces latency and improves parallelism
        auto has_all_args = true;
        args_content.resize(args.size());
        for (auto i : cc::indices_of(args))
        {
            // TODO: only content_hash required
            //       this makes a difference when loading content lazily from file
            auto content = this->try_get_resource_content(args[i], true);
            if (content.has_value())
                args_content[i] = content.value();
            else
                has_all_args = false;
        }

        // not all args available? requeue
        if (!has_all_args)
        {
            queue_mutex.lock();
            queue.push_back(res);
            queue_mutex.unlock();
            continue;
        }

        // we have all args here
        args_content_hashes.resize(args.size());
        for (auto i : cc::indices_of(args))
            args_content_hashes[i] = args_content[i].value().hash;

        // read cached invocation data
        auto const invoc = this->define_invocation(comp, args_content_hashes);
        // TODO: impure resource skip this
        content_data invoc_data;
        content_hash invoc_content_hash;
        invoc_db_mutex.lock_shared();
        auto p_invoc = invoc_db.get_ptr(invoc);
        if (p_invoc)
        {
            invoc_data = p_invoc->data;
            invoc_content_hash = p_invoc->content;
        }
        invoc_db_mutex.unlock_shared();

        // needs computation?
        if (invoc_data.is_invalid())
        {
            // TODO: foreign execution
        }

        // TODO
        // - make invoc
        // - check if invoc cached
        // - otherwise invoce comp fun
        //   and store content

        // done
        // TODO: store in res db
    }
}

cc::optional<res::base::content_data> res::base::ResourceSystem::query_content(content_hash hash)
{
    cc::optional<content_data> result;

    auto& db = m->content_desc_by_hash;
    auto& mutex = m->content_desc_by_hash_mutex;

    mutex.lock_shared();
    if (auto p_content = db.get_ptr(hash))
        result = p_content->data;
    mutex.unlock_shared();

    return result;
}

res::base::content_hash res::base::ResourceSystem::store_content(cc::span<const std::byte> data)
{
    cc::sha1_builder sha1;
    sha1.add(data);
    auto const hash = finalize_as<content_hash>(sha1);

    auto& db = m->content_desc_by_hash;
    auto& mutex = m->content_desc_by_hash_mutex;

    content_desc desc;
    desc.data_backing_buffer = cc::array<std::byte>(data); // copy
    desc.data.hash = hash;
    desc.data.state = content_state::serialized_data;
    desc.data.data.serialized = desc.data_backing_buffer;

    mutex.lock();
// TODO: must probably be deleted because we can get this easily in multithreaded context
#ifndef CC_RELEASE
    if (db.contains_key(hash))
        LOG_WARN("duplicated content store for %s", cc::as_byte_span(hash));
#endif
    db[hash] = cc::move(desc);
    mutex.unlock();

    return hash;
}

res::base::invoc_hash res::base::ResourceSystem::define_invocation(comp_hash const& computation, cc::span<content_hash const> args)
{
    cc::sha1_builder sha1;
    sha1.add(cc::as_byte_span(computation));
    for (auto const& h : args)
        sha1.add(cc::as_byte_span(h));
    return finalize_as<invoc_hash>(sha1);
}

cc::optional<res::base::invoc_result> res::base::ResourceSystem::query_invocation(invoc_hash hash, bool try_query_data)
{
    cc::optional<invoc_result> opt_result;

    auto& db = m->invoc_desc_by_hash;
    auto& mutex = m->invoc_desc_by_hash_mutex;

    // look up cached invocation
    mutex.lock_shared();
    if (auto p_desc = db.get_ptr(hash))
    {
        invoc_result result;
        result.content = p_desc->content;
        result.data = p_desc->data;
        opt_result = result;
    }
    mutex.unlock_shared();

    // check if content should be queried
    if (try_query_data &&         //
        opt_result.has_value() && //
        opt_result.value().data.state != content_state::invalid)
    {
        auto h_content = opt_result.value().content;
        auto d_content = this->query_content(h_content);

        // write back to db
        if (d_content.has_value())
        {
            opt_result.value().data = d_content.value();

            mutex.lock();
            auto& res = db[hash];
            res.content = h_content;
            res.data = d_content.value();
            mutex.unlock();
        }
    }

    return opt_result;
}
