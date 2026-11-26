module;
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>

export module Kairo.Editor.AssetImport;
import Kairo.Editor;
import Kairo.Editor.SceneRenderBridge;
import Kairo.Assets;

export namespace kairo::editor
{
    // Explicit first import is a saved-project operation. Stage one uniquely
    // named project-owned source; validate with the production OBJ importer
    // against a candidate registry before publishing the atomic manifest.
    // The host then reopens the project and rebuilds its render bindings.
    [[nodiscard]] inline kairo::assets::AssetID ImportStandaloneOBJ(
        const ProjectSession& project,const std::filesystem::path& requested)
    {
        if(!project.HasProject()||project.HasUnsavedChanges())
            throw std::logic_error("Save all project changes before importing.");
        if(requested.extension()!=".obj"||std::filesystem::is_symlink(requested))
            throw std::invalid_argument("Choose a regular standalone .obj file.");
        const auto source=CanonicalExistingFile(requested,"OBJ source");
        constexpr std::uintmax_t limit=32u*1024u*1024u;
        const auto bytes=std::filesystem::file_size(source);
        if(!bytes||bytes>limit)throw std::invalid_argument("OBJ must be between 1 byte and 32 MiB.");
        const auto manifest=ResolveExistingProjectFile(project.ProjectRoot(),project.Descriptor().AssetManifest,"asset manifest");
        kairo::assets::AssetRegistry candidate;
        kairo::assets::LoadAssetManifest(manifest,candidate);
        const auto baseline=kairo::assets::SerializeAssetManifest(candidate);
        if(baseline!=kairo::assets::SerializeAssetManifest(project.Assets()))
            throw std::runtime_error("Asset manifest changed on disk. Reopen project before importing.");
        const auto id=kairo::assets::GenerateAssetID();
        const auto folder=std::filesystem::path("Assets/Imported")/id.ToString();
        // Only a portable filename is copied. No external material or texture
        // dependencies are imported or silently registered.
        auto filename=source.filename().string();
        for(char c:filename)
            if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))
            {filename="mesh.obj";break;}
        if(filename.size()>96)filename="mesh.obj";
        const auto relative=folder/filename;
        const auto parent=ResolveProjectOutput(project.ProjectRoot(),folder.parent_path());
        std::filesystem::create_directories(parent);
        const auto output=ResolveProjectOutput(project.ProjectRoot(),folder);
        if(!std::filesystem::create_directory(output))
            throw std::runtime_error("Cannot reserve unique import directory.");
        try
        {
            const auto target=output/filename;
            if(!std::filesystem::copy_file(source,target))
                throw std::runtime_error("Cannot copy OBJ source.");
            if(std::filesystem::file_size(target)!=bytes)
                throw std::runtime_error("OBJ changed while copying.");
            kairo::assets::OBJMeshImporter importer;
            candidate.Insert({id,kairo::assets::AssetType::Mesh,kairo::assets::AssetOrigin::SourceFile,
                relative,importer.Identifier(),1u,{}});
            kairo::assets::ImportDatabase imports;
            const kairo::assets::DerivedDataCache cache(output/".validation-cache");
            (void)ImportRenderMesh(project.ProjectRoot(),{id},candidate,imports,cache);
            std::filesystem::remove_all(output/".validation-cache");
            // Recheck external edits immediately before publication. This is
            // not a multi-process lock; concurrent writers are unsupported.
            kairo::assets::AssetRegistry latest;
            kairo::assets::LoadAssetManifest(manifest,latest);
            if(kairo::assets::SerializeAssetManifest(latest)!=baseline)
                throw std::runtime_error("Asset manifest changed during import.");
            kairo::assets::SaveAssetManifest(manifest,candidate);
        }
        catch(...)
        {
            std::error_code ignored;
            std::filesystem::remove_all(output,ignored);
            throw;
        }
        return id;
    }
}
