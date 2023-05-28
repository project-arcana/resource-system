#include <nexus/test.hh>

#include <resource-system/System.hh>
#include <resource-system/res.hh>

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
    auto h = res::define(
        "add", [](float a, float b) { return a + b; }, 1, 2.f);

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
    auto add = [](float a, float b) { return a + b; };
    auto c3 = res::create(3.0f);
    auto h0 = res::define("add", add, 1.0f, 2.f);
    auto h1 = res::define("add", add, h0, 5.0f);
    auto h2 = res::define("add", add, h0, h1);
    auto h3 = res::define("add", add, h2, h2);
    auto h4 = res::define("add", add, c3, h3);

    CHECK(h4.try_get() == nullptr); // not loaded but now requested

    res::system().process_all();

    CHECK(*c3.try_get() == 3);
    CHECK(*h0.try_get() == 3);
    CHECK(*h1.try_get() == 8);
    CHECK(*h2.try_get() == 11);
    CHECK(*h3.try_get() == 22);
    CHECK(*h4.try_get() == 25);
}

TEST("res propagate change")
{
    auto add = [](float a, float b) { return a + b; };
    auto c3 = res::create(3.0f);
    auto h = res::define("add", add, c3, 2.f);

    CHECK(h.try_get() == nullptr); // not loaded but now requested

    res::system().process_all();

    CHECK(*c3.try_get() == 3);
    CHECK(*h.try_get() == 5);
}
