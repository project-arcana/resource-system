#include <nexus/run.hh>

#include <glow-extras/glfw/GlfwContext.hh>

int main(int argc, char** argv)
{
    if (std::getenv("ARCANA_NO_OPENGL"))
        return nx::run(argc, argv);
    
    glow::glfw::GlfwContext ctx;

    return nx::run(argc, argv);
}
