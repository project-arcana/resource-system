#include <nexus/app.hh>

#include <clean-core/invoke.hh>
#include <type_traits>

#include <resource-system/define.hh>
#include <resource-system/handle.hh>

namespace
{
// debug
namespace pv
{
template <class F>
void interactive(F&&)
{
}
int grid() { return 0; }
template <class... Args>
void view(Args const&...)
{
}
} // namespace pv
} // namespace

APP("resource API")
{
    // features:
    // - identify via hash(es)
    // - reloading / parameter changes -> regenerate
    // - bake partial caches
    // - garbage collection
    // - serve via resource server / file / network / ...
    // - streaming
    // - suitable for procedural generation
    // - deduplication
    // - usable for "tweakable parameters"
    // - attach debug information to resources (e.g. a type-erased object, a string, or something)
    // - parametric resources

    // architecture:
    // - resource handles
    //   - identify resource (name)
    //   - are lightweight value types
    //   - are similar to a std::future and a std::shared_ptr

    // caching system:
    // - via seperate hash -> content system

    // open questions:
    // - how do changes propagate?
    // - how can reachability be checked?
    // - lifetimes? (GC + ref count + explicit release?)
    //   (not immediately releasing unused resources has benefits for computation graphs)
    // - on change/load events? or polling? or both? versions? -> content hash?
    // - handles typed or untyped? automatic conversion?
    // - how to compute hash for lambdas? (maybe those are non-cacheable?)
    // - how to do threads and load balancing?
    // - how to do streaming?

    // misc:
    // - no string names (can be done externally by the user)
    // - use Mesh&& to directly consume/unload dependency
    // - nodes can be heterogeneously available (not in shipping, only on current pc, only on special server)
    //   (can be used to make sure certain nodes / resource compilers are not shipped)
    // - handle.diagnose_not_loaded() -> print why is not loaded (e.g. which dependencies are missing, are their nodes/systems executed?)
    // - reflection system for resources

    // define resources

    // deferred resource
    {
        // return promise<T> with callback
    }

    // viewer computation graph
    {
        struct Mesh
        {
            // ...
        };
        struct Renderable
        {
            // ...
        };

        auto make_renderable = [](Mesh const&) -> Renderable
        {
            return {}; // create renderable
        };
        static_assert(std::is_trivially_copyable_v<decltype(make_renderable)>);

        res::handle<float> param = res::create(0.0f);

        res::handle<Mesh> meshA = res::define(
            "meshA",
            [](float p)
            {
                (void)p;       // use parameter
                return Mesh(); // create some mesh
            },
            param);

        res::handle<Mesh> meshB = res::define(
            "meshB",
            [](Mesh const&)
            {
                return Mesh(); // do something with input
            },
            meshA);

        res::handle<Renderable> rA = res::define("make_renderable", make_renderable, meshA);
        res::handle<Renderable> rB = res::define("make_renderable", make_renderable, meshB);

        pv::interactive(
            [&]
            {
                // si::slider("parameter", param); // si support for res

                [[maybe_unused]] auto g = pv::grid();
                if (auto r = rA.try_get())
                    pv::view(*r);
                if (auto r = rB.try_get())
                    pv::view(*r);
            });
    }
}
