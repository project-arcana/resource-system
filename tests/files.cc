#include <nexus/test.hh>

#include <resource-system/file.hh>
#include <resource-system/System.hh>

#include <babel-serializer/file.hh>

TEST("file basics", disabled)
{
    auto const filename = "_test_res_file";
    babel::file::write(filename, "hello world!");

    auto f = res::load(res::file, filename, res::text);

    res::system().process_all();

    CHECK(*f.try_get() == "hello world!");
}
