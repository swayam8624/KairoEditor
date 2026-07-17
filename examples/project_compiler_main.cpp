#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>

import Kairo.Editor.ProjectLogicBuild;

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2)
            throw std::invalid_argument("Usage: KairoProjectCompiler <Project.kproject>");
        const auto built = kairo::editor::BuildProjectLogic(std::filesystem::path(argv[1]));
        std::cout << "Built " << built.size() << " attached logic artifact(s).\n";
        for (const auto& artifact : built)
            std::cout << "  " << artifact.SourcePath.filename().string() << " -> "
                      << artifact.ArtifactPath.string() << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "KairoProjectCompiler: " << error.what() << '\n';
        return 1;
    }
}
