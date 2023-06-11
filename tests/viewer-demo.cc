#include <nexus/app.hh>

#include <rich-log/log.hh>

#include <resource-system/System.hh>
#include <resource-system/persistence/simple.hh>
#include <resource-system/res.hh>

#include <glow-extras/viewer/canvas.hh>

#include <imgui/imgui.h>

APP("viewer resource demo")
{
    //
    auto res_cache = res::persistence::SimplePersistentStore(".res-cache");
    res_cache.load();

    int cnt_make_grid = 0;
    int cnt_make_renderables = 0;

    struct grid_params
    {
        int size_x = 8;
        int size_y = 8;
        float step_x = 1.f;
        float step_y = 1.f;
    } gparams;
    struct viewer_params
    {
        bool emit_lines = true;
    } vparams;

    auto make_grid_data = res::node("demo/grid_data", 1000,
                                    [&cnt_make_grid](grid_params p)
                                    {
                                        LOG("build grid data");
                                        cnt_make_grid++;

                                        cc::vector<tg::triangle3> tris;

                                        auto pos_at = [&](int x, int y)
                                        {
                                            tg::rng rng;
                                            rng.seed(1311 + x * 9123 + y * 1117);
                                            return tg::pos3(x * p.step_x, uniform(rng, 0.f, 1.f), y * p.step_y);
                                        };

                                        for (auto x = 0; x < p.size_x; ++x)
                                            for (auto y = 0; y < p.size_y; ++y)
                                            {
                                                auto p00 = pos_at(x + 0, y + 0);
                                                auto p01 = pos_at(x + 0, y + 1);
                                                auto p10 = pos_at(x + 1, y + 0);
                                                auto p11 = pos_at(x + 1, y + 1);

                                                auto tA = tg::triangle3(p00, p01, p11);
                                                auto tB = tg::triangle3(p00, p11, p10);

                                                {
                                                    auto ttA = tg::triangle3(p00, p01, p10);
                                                    auto ttB = tg::triangle3(p11, p10, p01);

                                                    if (area_of(tA) + area_of(tB) > area_of(ttA) + area_of(ttB))
                                                    {
                                                        tA = ttA;
                                                        tB = ttB;
                                                    }
                                                }

                                                tris.push_back(tA);
                                                tris.push_back(tB);
                                            }

                                        return tris;
                                    });

    auto make_renderables = res::node_runtime(
        [&cnt_make_renderables](cc::span<tg::triangle3 const> tris, viewer_params p)
        {
            LOG("make renderables");
            cnt_make_renderables++;
            auto c = gv::canvas_data();

            c.add_faces(tris);
            if (p.emit_lines)
                c.add_lines(tris, tg::color3::black).scale_size(0.2f);

            return c.create_renderables();
        });

    auto h_grid = res::define(make_grid_data, res::create_volatile_ref(gparams));
    auto h_renderables = res::load(make_renderables, h_grid, res::create_volatile_ref(vparams));

    gv::interactive(
        [&]
        {
            // UI
            auto changed = false;
            changed |= ImGui::SliderFloat("step x", &gparams.step_x, 0.1f, 2.f);
            changed |= ImGui::SliderFloat("step y", &gparams.step_y, 0.1f, 2.f);
            changed |= ImGui::SliderInt("size x", &gparams.size_x, 1, 32);
            changed |= ImGui::SliderInt("size y", &gparams.size_y, 1, 32);
            changed |= ImGui::Checkbox("show lines", &vparams.emit_lines);
            if (changed)
                res::system().invalidate_volatile_resources();

            ImGui::Separator();
            ImGui::Text("make_grid:        %d", cnt_make_grid);
            ImGui::Text("make_renderables: %d", cnt_make_renderables);

            // process graph
            res::system().process_all();

            // viewer
            auto v = gv::view();
            if (auto rs = h_renderables.try_get())
                for (auto const& r : *rs)
                    gv::view(r);
        });

    res_cache.save();
}
