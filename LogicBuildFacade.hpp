#pragma once

#include <cstddef>
#include <filesystem>
#include <string_view>
#include <vector>

namespace kairo::editor
{
    [[nodiscard]] std::vector<std::byte> CompileCoreLogicDocumentFile(
        const std::filesystem::path& sourcePath, std::string_view expectedDocumentID);
}
