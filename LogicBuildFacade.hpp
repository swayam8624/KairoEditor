#pragma once

#include <cstddef>
#include <filesystem>
#include <string_view>
#include <vector>

namespace kairo::editor
{
    /// Ordinary translation-unit facade for file-to-bytecode compilation.
    /// Kept outside the LogicDocumentCompiler BMI to avoid the MSVC 19.44
    /// C++ module frontend ICE while preserving the complete build contract.
    [[nodiscard]] std::vector<std::byte> CompileCoreLogicDocumentFile(
        const std::filesystem::path& sourcePath, std::string_view expectedDocumentID);
}
