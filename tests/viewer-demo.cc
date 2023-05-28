#include <nexus/app.hh>

#include <rich-log/log.hh>

#include <resource-system/System.hh>
#include <resource-system/res.hh>

#include <glow-extras/viewer/canvas.hh>

#include <imgui/imgui.h>

APP("viewer resource demo")
{
    //

    struct grid_params
    {
        int size_x = 8;
        int size_y = 8;
        float step_x = 1.f;
        float step_y = 1.f;

        bool emit_lines = true;
    } params;

    auto renderables = res::load(
        "make_grid_renderables",
        [](grid_params p)
        {
            auto c = gv::canvas();

            LOG("build grid");

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

                    c.add_face(tA);
                    c.add_face(tB);
                    if (p.emit_lines)
                    {
                        c.add_lines(tA, tg::color3::black).scale_size(0.2f);
                        c.add_lines(tB, tg::color3::black).scale_size(0.2f);
                    }
                }

            return c.create_renderables();
        },
        res::define_impure("params", [&params] { return params; }));

    gv::interactive(
        [&]
        {
            // UI
            auto changed = false;
            changed |= ImGui::SliderFloat("step x", &params.step_x, 0.1f, 2.f);
            changed |= ImGui::SliderFloat("step y", &params.step_y, 0.1f, 2.f);
            changed |= ImGui::SliderInt("size x", &params.size_x, 1, 32);
            changed |= ImGui::SliderInt("size y", &params.size_y, 1, 32);
            changed |= ImGui::Checkbox("show lines", &params.emit_lines);
            if (changed)
                res::system().invalidate_impure_resources();

            // process graph
            res::system().process_all();

            // viewer
            auto v = gv::view();
            if (auto rs = renderables.try_get())
                for (auto const& r : *rs)
                    gv::view(r);
        });
}
