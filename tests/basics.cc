#include <nexus/test.hh>

#include <resource-system/System.hh>
#include <resource-system/res.hh>

TEST("res handle types")
{
    {
        auto h = res::create(7);
        static_assert(std::is_same_v<decltype(h), res::handle<int>>);
    }
    {
        auto h = res::create(7.);
        static_assert(std::is_same_v<decltype(h), res::handle<double>>);
    }
    {
        auto h = res::create(cc::string("hello"));
        static_assert(std::is_same_v<decltype(h), res::handle<cc::string_view>>);
    }
    {
        auto h = res::create(cc::string_view("hello"));
        static_assert(std::is_same_v<decltype(h), res::handle<cc::string_view>>);
    }
    {
        auto h = res::create(cc::vector<int>{1, 2, 3, 4});
        static_assert(std::is_same_v<decltype(h), res::handle<cc::span<int const>>>);
    }
    {
        auto h = res::create(cc::array<int>{1, 2, 3, 4});
        static_assert(std::is_same_v<decltype(h), res::handle<cc::span<int const>>>);
    }
    {
        auto h = res::create(cc::span<int>());
        static_assert(std::is_same_v<decltype(h), res::handle<cc::span<int const>>>);
    }

    CHECK(true);
}

TEST("res explicit resource")
{
    auto h = res::create(7);

    // not loaded
    static_assert(std::is_same_v<decltype(h), res::handle<int>>);
    CHECK(h.is_valid());
    CHECK(!h.is_loaded());
    CHECK(h.try_get() == nullptr);

    res::system().process_all();

    CHECK(h.try_get() != nullptr);
    CHECK(h.is_loaded()); // after try_get
    CHECK(*h.try_get() == 7);
}

TEST("res simple define")
{
    auto h = res::define_runtime([](float a, float b) { return a + b; }, 1, 2.f);

    static_assert(std::is_same_v<decltype(h), res::handle<float>>);
    CHECK(h.is_valid());
    CHECK(!h.is_loaded());

    res::system().process_all();

    CHECK(!h.is_loaded()); // never requested
    h.try_get();
    CHECK(h.try_get() == nullptr); // still not loaded

    res::system().process_all();

    CHECK(*h.try_get() == 3);
    CHECK(h.is_loaded()); // after try_get
}

TEST("res dependent define")
{
    auto add = res::node_runtime([](float a, float b) { return a + b; });
    auto c3 = res::create(3.0f);
    auto h0 = res::define(add, 1.0f, 2.f);
    auto h1 = res::define(add, h0, 5.0f);
    auto h2 = res::define(add, h0, h1);
    auto h3 = res::define(add, h2, h2);
    auto h4 = res::define(add, c3, h3);

    CHECK(h4.try_get() == nullptr); // not loaded but now requested

    res::system().process_all();

    CHECK(*c3.try_get() == 3);
    CHECK(*h0.try_get() == 3);
    CHECK(*h1.try_get() == 8);
    CHECK(*h2.try_get() == 11);
    CHECK(*h3.try_get() == 22);
    CHECK(*h4.try_get() == 25);
}

TEST("res volatile")
{
    int x = 13;
    auto h = res::define_volatile([&x] { return x; });

    CHECK(h.try_get() == nullptr); // not loaded but now requested

    res::system().process_all();

    CHECK(*h.try_get() == 13);

    // change underlying var
    x = 19;

    CHECK(*h.try_get() == 13); // does not see any change

    res::system().process_all();

    CHECK(*h.try_get() == 13); // does not see any change either

    res::system().invalidate_volatile_resources();

    CHECK(*h.try_get() == 13); // returns the outdated version

    res::system().process_all();

    CHECK(*h.try_get() == 19); // now we have the new version
}

TEST("res propagate change")
{
    int x = 3;
    auto add = res::node_runtime([](float a, float b) { return a + b; });
    auto hi = res::define_volatile([&x] { return x; });
    auto h = res::define(add, hi, 2.f);

    CHECK(h.try_get() == nullptr); // not loaded but now requested

    res::system().process_all();

    CHECK(*hi.try_get() == 3);
    CHECK(*h.try_get() == 5);

    x = 10;

    res::system().invalidate_volatile_resources();
    res::system().process_all();

    CHECK(*h.try_get() == 5); // only at this call do we see that we need to recompute

    res::system().process_all();
    CHECK(*h.try_get() == 12);
}

TEST("res invoc caching")
{
    int eval_count = 0;
    auto f_identity = res::node_runtime(
        [&eval_count](float a)
        {
            eval_count++;
            return a;
        });

    auto add = res::node_runtime([](float a, float b) { return a + b; });

    auto c3 = res::create(3.f);
    auto h0 = res::define(add, 1.f, 2.f);

    // two resources defined with different paths but the same input content
    auto r0 = res::define(f_identity, c3);
    auto r1 = res::define(f_identity, h0);

    CHECK(eval_count == 0);

    r0.try_get();
    res::system().process_all();
    CHECK(eval_count == 1);

    r1.try_get();
    res::system().process_all();
    CHECK(eval_count == 1); // should have used cached invocation
}


// TODO: non-moveable types as args
// TODO: error handling
// TODO: MCT

// TODO: require static res::node and detect name clashes globally
//   static auto n_add = res::node("stuff/add", [](...) { ... });
//   anon nodes that are only runtime cached but not persisted?

// TODO: test that "baked-in" and handle params are cached equally
//   i.e.:
//     auto c = res::create(3.f);
//     auto n = res::node("...add", []...);
//     auto h0 = res::define(n, 3.f, c);
//     auto h1 = res::define(n, c, c);
//     -> should only be one invocation
