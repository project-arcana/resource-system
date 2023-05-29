#include "api.hh"

#include <clean-core/experimental/ringbuffer.hh>
#include <clean-core/function_ref.hh>
#include <clean-core/hash.sha1.hh>
#include <clean-core/indices_of.hh>
#include <clean-core/intrinsics.hh>
#include <clean-core/map.hh>
#include <clean-core/vector.hh>

#include <rich-log/log.hh>

#include <resource-system/detail/hash_helper.hh>
#include <resource-system/detail/log.hh>

#include <shared_mutex>

#define ENABLE_VERBOSE_LOG 0

#if ENABLE_VERBOSE_LOG
#define LOG_VERBOSE(...) LOG(__VA_ARGS__)
#else
#define LOG_VERBOSE(...) CC_FORCE_SEMICOLON
#endif

namespace res::base
{
namespace
{
cc::string shorthash(hash const& h)
{
    auto p = reinterpret_cast<uint8_t const*>(&h);
    auto constexpr size = 6;
    static constexpr auto hex = "0123456789ABCDEF";
    auto s = cc::string::uninitialized(size * 2 + 2);
    for (auto i = 0; i < size; ++i)
    {
        auto v = *p++;
        s[i * 2 + 1] = hex[v / 16];
        s[i * 2 + 2] = hex[v % 16];
    }
    s.front() = '[';
    s.back() = ']';
    return s;
}

// TODO: flat?
struct res_desc
{
    comp_hash comp;
    cc::vector<res_hash> args;

    bool is_impure = false;

    // NOTE: this only tracks external references
    //       internal references are part of the GC process
    ref_count* ref_counter = nullptr;

    // [cache for resources]
    // TODO: store this in a separate store?
    //       -> reduces contention a lot in traversal-during-computation scenarios
    // content is considered up-to-date if content_gen == current_gen
    // in that case, content_name is always valid
    // however, content_data might be nullopt if it wasn't required
    int enqueued_for_name_gen = -1;    // what is gen after queue finished?
    int enqueued_for_content_gen = -1; // what is gen after queue finished?
    int content_gen = -1;
    content_hash content_name;
    cc::optional<content_ref> content_data; // TODO: refcounting
};

struct content_desc
{
    // TODO: refcounting

    computation_result content;

    bool has_data() const { return content.serialized_data.has_value() || content.runtime_data.has_value() || content.error_data.has_value(); }

    content_ref make_ref(int gen) const
    {
        CC_ASSERT(has_data() && "how does this happen?");
        content_ref r;
        r.generation = gen;
        if (content.error_data.has_value())
            r.error_msg = content.error_data.value().message;
        else
        {
            CC_ASSERT(content.runtime_data.has_value() && "TODO: implement deserialization");
            // either here or directly on load
            // NOTE: the computation knows how to deserialize values so the actual location is not that clear
            r.data_ptr = content.runtime_data.value().data_ptr;
        }
        return r;
    }
};

// TODO: flat?
struct invoc_desc
{
    // TODO: do we really need to store this?
    // comp_hash comp;
    // cc::vector<content_hash> args;

    content_hash content;
};

content_hash make_content_hash(computation_result const& res, invoc_hash invoc, cc::function_ptr<content_hash(void const*)> make_hash, bool is_impure)
{
    cc::sha1_builder sha1;
    if (res.serialized_data.has_value()) // normal case
    {
        sha1.add(cc::as_byte_span(uint32_t(1000)));
        sha1.add(cc::as_byte_span(res.serialized_data.value().type));
        sha1.add(res.serialized_data.value().blob);
    }
    else if (res.error_data.has_value()) // error case
    {
        sha1.add(cc::as_byte_span(uint32_t(2000)));
        sha1.add(cc::as_byte_span(res.error_data.value().message));
    }
    else if (res.runtime_data.has_value() && make_hash) // non-serializable BUT hashable case
    {
        sha1.add(cc::as_byte_span(uint32_t(4000)));
        sha1.add(cc::as_byte_span(make_hash(res.runtime_data.value().data_ptr)));
    }
    else // non-serializable + non-hashable case
    {
        CC_ASSERT(res.runtime_data.has_value());
        sha1.add(cc::as_byte_span(uint32_t(4000)));
        sha1.add(cc::as_byte_span(invoc));

        // impure + non-serializable means we have no idea what the content is
        // thus we add a basically random value to the hash
        if (is_impure)
            sha1.add(cc::as_byte_span(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    }
    return res::detail::finalize_as<content_hash>(sha1);
}

// NOTE: these are threadsafe via reader/writer lock
template <class HashT, class ValueT>
struct MemoryStore
{
    // returns an optional of get_f(ValueT const&)
    // if get_f is void, returns a bool that is true iff get_f was called (i.e. the key exists)
    // NOTE: get_f is called within a reader-lock
    //       thus, there should be no expensive computation within
    template <class GetF>
    auto get(HashT hash, GetF&& get_f)
    {
        using T = std::decay_t<decltype(get_f(_data.get(std::declval<ValueT const&>())))>;

        if constexpr (std::is_same_v<T, void>)
        {
            auto has_value = false;

            {
                auto lock = std::shared_lock{_mutex};
                if (auto p_val = _data.get_ptr(hash))
                {
                    get_f(static_cast<ValueT const&>(*p_val));
                    has_value = true;
                }
            }

            return has_value;
        }
        else
        {
            cc::optional<T> res;

            {
                auto lock = std::shared_lock{_mutex};
                if (auto p_val = _data.get_ptr(hash))
                    res = get_f(static_cast<ValueT const&>(*p_val));
            }

            return res;
        }
    }
    void set(HashT hash, ValueT value)
    {
        auto lock = std::unique_lock{_mutex};
        _data[hash] = cc::move(value);
    }
    void set_if_new(HashT hash, ValueT value)
    {
        auto lock = std::unique_lock{_mutex};
        _data.get_or_create(hash, [&] { return cc::move(value); });
    }
    template <class MutF>
    bool modify(HashT hash, MutF&& mut_f)
    {
        auto lock = std::unique_lock{_mutex};
        auto p_data = _data.get_ptr(hash);
        if (p_data)
        {
            mut_f(*p_data);
            return true;
        }
        else
            return false;
    }

private:
    cc::map<HashT, ValueT> _data;
    std::shared_mutex _mutex;
};
} // namespace
} // namespace res::base

struct res::base::ResourceSystem::impl
{
    // TODO

    // for now, we need comp/res maps completely in memory
    // so we know how to compute every resource
    MemoryStore<comp_hash, computation_desc> comp_store;
    MemoryStore<res_hash, res_desc> res_store;

    // these two are the "data caches"
    MemoryStore<content_hash, content_desc> content_store;
    MemoryStore<invoc_hash, invoc_desc> invoc_store;

    // queue
    // TODO: mpmc with grow?
    // NOTE: we have to guarantee that once a job lands in one of these queues
    //       that eventually the stores will contain updated data
    cc::ringbuffer<res_hash> queue_compute_content_of_resource;
    std::mutex queue_compute_content_of_resource_mutex;
    cc::ringbuffer<res_hash> queue_compute_content_hash_of_resource;
    std::mutex queue_compute_content_hash_of_resource_mutex;
};

res::base::ResourceSystem::ResourceSystem() { m = cc::make_unique<impl>(); }

res::base::ResourceSystem::~ResourceSystem() = default;

res::base::comp_hash res::base::ResourceSystem::define_computation(computation_desc desc)
{
    // make hash
    cc::sha1_builder sha1;
    sha1.add(cc::as_byte_span(desc.name));
    sha1.add(cc::as_byte_span(desc.algo_hash));
    auto const hash = res::detail::finalize_as<comp_hash>(sha1);

    // read: check if comp already known
    auto has_val = m->comp_store.get(hash,
                                     [&](computation_desc const& prev_desc)
                                     {
                                         // TODO: more?
                                         if (prev_desc.algo_hash != desc.algo_hash)
                                             LOG_WARN("computation with inconsistent algo hash");
                                     });

    // write: add to map
    if (!has_val)
    {
        LOG_VERBOSE("comp %s defined", shorthash(hash));
        m->comp_store.set(hash, cc::move(desc));
    }

    return hash;
}

cc::pair<res::base::res_hash, res::base::ref_count*> res::base::ResourceSystem::define_resource(resource_desc const& desc)
{
    ref_count* counter = nullptr;

    // make hash
    cc::sha1_builder sha1;
    sha1.add(cc::as_byte_span(desc.computation));
    for (auto const& h : desc.args)
        sha1.add(cc::as_byte_span(h));
    auto const hash = res::detail::finalize_as<res_hash>(sha1);

    // read: check if comp already known
    auto has_val = m->res_store.get(hash,
                                    [&](res_desc const& prev_desc)
                                    {
                                        CC_ASSERT(desc.computation == prev_desc.comp && "res_hash collision");
                                        CC_ASSERT(desc.args.equals_content(prev_desc.args) && "res_hash collision");

                                        counter = prev_desc.ref_counter;
                                    });

    // write: add to map
    if (!has_val)
    {
        res_desc rdesc;
        rdesc.comp = desc.computation;
        rdesc.args.push_back_range(desc.args);
        rdesc.is_impure = desc.is_impure;
        rdesc.ref_counter = cc::alloc<ref_count>();
        counter = rdesc.ref_counter;

#if ENABLE_VERBOSE_LOG
        cc::string deps;
        for (auto a : desc.args)
        {
            if (!deps.empty())
                deps += ", ";
            deps += shorthash(a);
        }
        LOG_VERBOSE("res %s defined, deps [%s]", shorthash(hash), deps);
#endif

        m->res_store.set(hash, cc::move(rdesc));
    }

    CC_ASSERT(counter != nullptr);
    return {hash, counter};
}

cc::optional<res::base::content_ref> res::base::ResourceSystem::try_get_resource_content(res_hash res, bool enqueue_if_not_found)
{
    cc::optional<content_ref> result;
    auto need_compute = false;
    int const target_generation = generation;

    // 1. read-only res db lookup
    //    this is the fast path
    auto has_resource = m->res_store.get(res,
                                         [&](res_desc const& desc)
                                         {
                                             // see if cached version found
                                             // NOTE: nullopt content_data means only content_hash, not actual data is known
                                             if (desc.content_gen == target_generation && desc.content_data.has_value())
                                             {
                                                 // we guarantee that this is the stored data
                                                 CC_ASSERT(desc.content_data.has_value());
                                                 result = desc.content_data;
                                                 return;
                                             }
                                             else
                                             {
                                                 // cached content is either :
                                                 // - outdated
                                                 // - computed but not cached
                                                 // - not computed
                                                 if (desc.enqueued_for_content_gen != target_generation)
                                                     need_compute = true;
                                             }

                                             // try to return outdated data
                                             if (desc.content_data.has_value())
                                             {
                                                 auto content = desc.content_data.value();
                                                 LOG_VERBOSE("returning outdated content for %s", shorthash(res));
                                                 content.is_outdated = true;
                                                 result = content;
                                             }
                                         });

    if (!has_resource)
    {
        LOG_ERROR("no resource known with id %s", shorthash(res));
        return result;
    }

    // 2. if no cached data found, trigger computation
    if (need_compute && enqueue_if_not_found)
    {
        // set to enqueued in res db
        auto need_enqueue = false;
        auto ok = m->res_store.modify(res,
                                      [&](res_desc& desc)
                                      {
                                          if (desc.enqueued_for_content_gen == target_generation)
                                              return; // already enqueued

                                          need_enqueue = true;
                                          desc.enqueued_for_content_gen = target_generation;
                                      });
        CC_ASSERT(ok && "overzealous GC?");

        // actually enqueue
        if (need_enqueue)
        {
            LOG_VERBOSE("res %s enqueued for content", shorthash(res));
            auto lock = std::lock_guard{m->queue_compute_content_of_resource_mutex};
            m->queue_compute_content_of_resource.push_back(res);
        }
    }

    // NOTE: result can be outdated
    return result;
}

cc::optional<res::base::content_hash> res::base::ResourceSystem::try_get_resource_content_hash(res_hash res, bool enqueue_if_not_found)
{
    cc::optional<content_hash> result;
    auto need_compute = false;
    int const target_generation = generation;

    // 1. read-only res db lookup
    //    this is the fast path
    auto has_resource = m->res_store.get(res,
                                         [&](res_desc const& desc)
                                         {
                                             // see if cached version found
                                             if (desc.content_gen == target_generation)
                                                 result = desc.content_name;
                                             // otherwise check if already enqueued
                                             else if (desc.enqueued_for_content_gen != target_generation && //
                                                      desc.enqueued_for_name_gen != target_generation)
                                                 need_compute = true;
                                         });

    if (!has_resource)
    {
        LOG_ERROR("no resource known with id %s", shorthash(res));
        return result;
    }

    // 2. if no cached data found, trigger computation
    if (need_compute && enqueue_if_not_found)
    {
        // set to enqueued in res db
        auto need_enqueue = false;
        auto ok = m->res_store.modify(res,
                                      [&](res_desc& desc)
                                      {
                                          // NOTE: enqueued for content will also set name
                                          if (desc.enqueued_for_name_gen == target_generation || //
                                              desc.enqueued_for_content_gen == target_generation)
                                              return; // already enqueued

                                          need_enqueue = true;
                                          desc.enqueued_for_name_gen = target_generation;
                                      });
        CC_ASSERT(ok && "overzealous GC?");

        // actually enqueue
        if (need_enqueue)
        {
            LOG_VERBOSE("res %s enqueued for hash", shorthash(res));
            auto lock = std::lock_guard{m->queue_compute_content_hash_of_resource_mutex};
            m->queue_compute_content_hash_of_resource.push_back(res);
        }
    }

    // NOTE: result can be outdated
    return result;
}

bool res::base::ResourceSystem::impl_process_queue_res(bool need_content)
{
    res_hash res;

    // local caches
    static thread_local cc::vector<res_hash> args;
    static thread_local cc::vector<content_hash> args_content_hashes;
    static thread_local cc::vector<content_ref> args_content;

    auto& queue = need_content ? m->queue_compute_content_of_resource : m->queue_compute_content_hash_of_resource;
    auto& queue_mutex = need_content ? m->queue_compute_content_of_resource_mutex : m->queue_compute_content_hash_of_resource_mutex;

    // get job
    {
        auto lock = std::lock_guard{queue_mutex};
        if (queue.empty())
            return false;

        res = queue.pop_front();
    }

    // process
    //   we have a resource "res"
    //   and want to know the content hash for it
    // LOG("res %s processing...", shorthash(res));

    // 1. get comp + arg res_hashes
    //   (can check if resource already up to date)
    int const gen = generation;
    auto is_up_to_date = false;
    auto is_impure = false;
    comp_hash comp;
    auto found_res = m->res_store.get(res,
                                      [&](res_desc const& desc)
                                      {
                                          if (desc.content_gen == gen && (!need_content || desc.content_data.has_value()))
                                          {
                                              is_up_to_date = true;
                                              return;
                                          }

                                          comp = desc.comp;
                                          args = desc.args; // copy
                                          is_impure = desc.is_impure;
                                      });
    CC_ASSERT(found_res && "overzealous GC?");

    // early out: someone already updated the content
    if (is_up_to_date)
        return true;

    // 2. query content hashes for all args
    auto has_all_arg_hashes = true;
    args_content_hashes.resize(args.size());
    for (auto i : cc::indices_of(args))
    {
        if (auto arg_hash = this->try_get_resource_content_hash(args[i]); arg_hash.has_value())
            args_content_hashes[i] = arg_hash.value();
        else
            has_all_arg_hashes = false;
    }

    // not all args available? requeue
    if (!has_all_arg_hashes)
    {
        LOG_VERBOSE("res %s requeue because not all arg hashes are available", shorthash(res));
        auto lock = std::lock_guard{queue_mutex};
        queue.push_back(res);
        return true;
    }

    // read cached invocation data
    auto const invoc = this->define_invocation(comp, args_content_hashes);

    // impure resources might change their content with each invocation
    // so we cannot rely on the invoc_store for them
    if (!is_impure)
    {
        auto invoc_res = m->invoc_store.get(invoc, [&](invoc_desc const& desc) { return desc.content; });

        // easy path: invoc is cached, aka we immediately have the result
        if (invoc_res.has_value())
        {
            auto content_hash = invoc_res.value();

            // if we also need the content, we can ask the content store
            // if this fails, we need to actually compute the content
            // TODO: or ask someone who knows
            cc::optional<content_ref> content_data;
            if (need_content)
                content_data = this->query_content(content_hash);

            if (!need_content || content_data.has_value())
            {
                LOG_VERBOSE("res %s found invoc %s (%s content %s) in cache", shorthash(res), shorthash(invoc), need_content ? "and" : "hash",
                            shorthash(content_hash));
                auto ok = m->res_store.modify(res,
                                              [&](res_desc& desc)
                                              {
                                                  if (desc.content_gen == gen)
                                                      return; // already up to date

                                                  desc.content_gen = gen;
                                                  desc.content_name = content_hash;
                                                  desc.content_data = content_data;
                                              });
                CC_ASSERT(ok && "overzealous GC?");
                return true;
            }
        }
    }

    // 3. hard path: invoc is not cached, so we need to fetch arg content and compute it
    //               this also include the case where invoc is cached but we need the content and it's not in the content store

    // 3.1. query content for all args
    auto has_all_arg_content = true;
    args_content.resize(args.size());
    for (auto i : cc::indices_of(args))
    {
        // NOTE: outdated counts as invalid here
        if (auto arg_content = this->try_get_resource_content(args[i]); //
            arg_content.has_value() && !arg_content.value().is_outdated)
            args_content[i] = arg_content.value();
        else
            has_all_arg_content = false;
    }

    // not all args available? requeue
    if (!has_all_arg_content)
    {
        LOG_VERBOSE("res %s requeue because not all arg contents are available", shorthash(res));
        auto lock = std::lock_guard{queue_mutex};
        queue.push_back(res);
        return true;
    }

    // 3.2 we have all args -> compute
    {
        // TODO: keep comp alive once this becomes an issue
        cc::function_ref<computation_result(cc::span<content_ref const>)> compute_resource;
        cc::function_ptr<content_hash(void const*)> make_hash;
        auto has_comp = m->comp_store.get(comp,
                                          [&](computation_desc const& desc)
                                          {
                                              compute_resource = desc.compute_resource;
                                              make_hash = desc.make_runtime_content_hash;
                                          });
        CC_ASSERT(has_comp && "overzealous GC?");

        // actual resource computation
        // TODO: indirection
        // TODO: split computation, ...
        LOG_VERBOSE("res %s compute content ...", shorthash(res));
        auto comp_result = compute_resource(args_content);
        CC_ASSERT(comp_result.error_data.has_value() || comp_result.runtime_data.has_value());

        auto content_hash = make_content_hash(comp_result, invoc, make_hash, is_impure);

        if (comp_result.serialized_data.has_value())
            LOG_VERBOSE("content %s is serialized %s", shorthash(content_hash), comp_result.serialized_data.value().blob);

        // store result in content store
        // CAUTION: this must only be set if the content is new
        //          otherwise we're invalidating previously valid references to the data
        m->content_store.set_if_new(content_hash, content_desc{cc::move(comp_result)});

        // NOTE: we always have to do this second lookup to ensure we get the reference that is actually in the db
        // TODO: could be merged with previous set into more complex op
        auto content_data = m->content_store.get(content_hash, [gen](content_desc const& desc) { return desc.make_ref(gen); });

        // store result in invoc store
        // we always set this
        // due to environment non-determinism, this might not be the same hash as before
        m->invoc_store.set(invoc, invoc_desc{content_hash});

        // store result in res store
        auto res_ok = m->res_store.modify(res,
                                          [&](res_desc& desc)
                                          {
                                              desc.content_gen = gen;
                                              desc.content_name = content_hash;
                                              desc.content_data = content_data;
                                          });
        CC_ASSERT(res_ok && "overzealous GC?");
        LOG_VERBOSE("res %s has fully defined content %s", shorthash(res), shorthash(content_hash));
    }

    return true;
}


void res::base::ResourceSystem::invalidate_impure_resources()
{
    // simple for now
    cc::intrin_atomic_add(&generation, 1);
}

void res::base::ResourceSystem::process_all()
{
    // DEBUG
    // auto max_tries = 100;

    // not locked because this is fine to be approximative
    while (!m->queue_compute_content_of_resource.empty() || !m->queue_compute_content_hash_of_resource.empty())
    {
        // compute content hashes first where required
        if (!m->queue_compute_content_hash_of_resource.empty())
            impl_process_queue_res(false);

        // then compute actual contents
        if (!m->queue_compute_content_of_resource.empty())
            impl_process_queue_res(true);

        // if (max_tries-- < 0)
        //     break;
    }
}

cc::optional<res::base::content_ref> res::base::ResourceSystem::query_content(content_hash hash)
{
    return m->content_store.get(hash, [gen = int(generation)](content_desc const& desc) { return desc.make_ref(gen); });
}

res::base::invoc_hash res::base::ResourceSystem::define_invocation(comp_hash const& computation, cc::span<content_hash const> args)
{
    cc::sha1_builder sha1;
    sha1.add(cc::as_byte_span(computation));
    for (auto const& h : args)
        sha1.add(cc::as_byte_span(h));
    return res::detail::finalize_as<invoc_hash>(sha1);
}
