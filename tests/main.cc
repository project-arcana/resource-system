#include <nexus/run.hh>

#include <glow-extras/glfw/GlfwContext.hh>

int main(int argc, char** argv)
{
    glow::glfw::GlfwContext ctx;

    return nx::run(argc, argv);
}
