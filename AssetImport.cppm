module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

export module Kairo.Editor.AssetImport;
import Kairo.Editor;
import Kairo.Editor.SceneRenderBridge;
import Kairo.Assets;
import Kairo.Renderer;

export namespace kairo::editor
{
    namespace asset_import_detail
    {
        constexpr std::uintmax_t MaximumBatchBytes=512ull*1024ull*1024ull;
        constexpr std::size_t MaximumFiles=1024;
        enum class Kind { Mesh,Scene,Texture,Audio };
        inline Kind Classify(const std::filesystem::path& file)
        {
            auto extension=file.extension().string();
            std::transform(extension.begin(),extension.end(),extension.begin(),
                [](unsigned char c){return static_cast<char>(c>='A'&&c<='Z'?c+32:c);});
            if(extension==".obj")return Kind::Mesh;
            if(extension==".gltf"||extension==".glb")return Kind::Scene;
            if(extension==".png"||extension==".jpg"||extension==".jpeg"||extension==".tga"||
                extension==".bmp"||extension==".hdr")return Kind::Texture;
            if(extension==".wav")return Kind::Audio;
            throw std::invalid_argument("Unsupported import file extension: "+extension);
        }
        inline std::filesystem::path PortableRelative(const std::filesystem::path& path)
        {
            const auto normalized=kairo::assets::NormalizeAssetPath(path);
            for(const auto& part:normalized)
            {
                const auto name=part.generic_string();
                if(name.empty()||name.size()>240||name.back()=='.'||name.back()==' ')
                    throw std::invalid_argument("Nonportable import path component.");
                if(name.find_first_of("<>\"|?*")!=std::string::npos)
                    throw std::invalid_argument("Nonportable import filename.");
                auto stem=part.stem().generic_string();
                std::transform(stem.begin(),stem.end(),stem.begin(),
                    [](unsigned char c){return static_cast<char>(c>='a'&&c<='z'?c-32:c);});
                if(stem=="CON"||stem=="PRN"||stem=="AUX"||stem=="NUL"||
                    (stem.size()==4&&(stem.starts_with("COM")||stem.starts_with("LPT"))&&stem[3]>='1'&&stem[3]<='9'))
                    throw std::invalid_argument("Reserved device filename in import.");
            }
            return normalized;
        }
        inline std::vector<std::byte> Read(const std::filesystem::path& path,std::uintmax_t limit)
        {
            std::ifstream input(path,std::ios::binary|std::ios::ate);
            if(!input)throw std::runtime_error("Cannot open import source.");
            const auto end=input.tellg();
            if(end<=0||static_cast<std::uintmax_t>(end)>limit)
                throw std::length_error("Import source is empty or exceeds batch limit.");
            std::vector<std::byte> bytes(static_cast<std::size_t>(end));
            input.seekg(0);
            if(!input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())))
                throw std::runtime_error("Cannot read complete source.");
            return bytes;
        }
        inline void Copy(const std::filesystem::path& root,const std::filesystem::path& relative,
            const std::filesystem::path& target,std::uintmax_t& remaining,std::size_t& files)
        {
            auto source=root;
            for(const auto& part:relative)
            {
                source/=part;
                if(std::filesystem::is_symlink(source))throw std::invalid_argument("Linked import source rejected.");
            }
            if(!std::filesystem::is_regular_file(source))throw std::invalid_argument("Import source is not a regular file.");
            const auto size=std::filesystem::file_size(source);
            if(!size||size>remaining||files>=MaximumFiles)throw std::length_error("Import batch exceeds file or byte budget.");
            const auto before=kairo::assets::FingerprintFile(source);
            std::filesystem::create_directories(target.parent_path());
            if(!std::filesystem::copy_file(source,target)||
                kairo::assets::FingerprintFile(target)!=before||kairo::assets::FingerprintFile(source)!=before)
                throw std::runtime_error("Import source changed while copying.");
            remaining-=size;++files;
        }
    }

    // Import an entire batch as a single manifest transaction. Each primary
    // receives a private project-owned directory; glTF dependencies retain
    // relative paths, and no unrelated sibling files are copied.
    [[nodiscard]] inline std::vector<kairo::assets::AssetID> ImportAssetBatch(
        const ProjectSession& project,std::span<const std::filesystem::path> requested)
    {
        using namespace kairo::assets;
        using namespace asset_import_detail;
        if(!project.HasProject()||project.HasUnsavedChanges())
            throw std::logic_error("Save all project changes before importing.");
        if(requested.empty()||requested.size()>64)
            throw std::invalid_argument("Import between 1 and 64 primary files.");
        const auto manifest=ResolveExistingProjectFile(project.ProjectRoot(),project.Descriptor().AssetManifest,"asset manifest");
        AssetRegistry candidate;
        LoadAssetManifest(manifest,candidate);
        const auto baseline=SerializeAssetManifest(candidate);
        if(baseline!=SerializeAssetManifest(project.Assets()))
            throw std::runtime_error("Asset manifest changed on disk. Reopen project before importing.");
        std::vector<std::filesystem::path> reserved,sources;
        std::vector<AssetID> ids;
        reserved.reserve(requested.size());sources.reserve(requested.size());ids.reserve(requested.size());
        std::uintmax_t remaining=MaximumBatchBytes;
        std::size_t files=0;
        try
        {
            for(const auto& requestedPath:requested)
            {
                if(!requestedPath.is_absolute()||std::filesystem::is_symlink(requestedPath))
                    throw std::invalid_argument("Choose absolute paths to regular local assets.");
                const auto source=CanonicalExistingFile(requestedPath,"asset source");
                if(std::find(sources.begin(),sources.end(),source)!=sources.end())
                    throw std::invalid_argument("Duplicate primary file in import batch.");
                sources.push_back(source);
                const auto kind=Classify(source);
                const auto filename=PortableRelative(source.filename());
                const auto id=GenerateAssetID();
                const auto folder=std::filesystem::path("Assets/Imported")/id.ToString();
                const auto relative=folder/filename;
                const auto output=ResolveProjectOutput(project.ProjectRoot(),folder);
                std::filesystem::create_directories(output.parent_path());
                if(!std::filesystem::create_directory(output))throw std::runtime_error("Cannot reserve import directory.");
                reserved.push_back(output);
                Copy(source.parent_path(),filename,output/filename,remaining,files);
                ImportDatabase imports;
                const DerivedDataCache cache(output/".validation-cache");
                std::vector<AssetReference> dependencies;
                if(kind==Kind::Scene)
                {
                    const auto bytes=Read(output/filename,MaximumBatchBytes);
                    // Discover from original location, then validate the copied
                    // compound scene against its own project-local directory.
                    const auto external=DiscoverGltfExternalDependencies(bytes,source);
                    for(const auto& dep:external)
                    {
                        const auto path=PortableRelative(dep.Relative);
                        if(PortableAssetPathKey(path)==PortableAssetPathKey(filename)||
                            path.begin()->generic_string()==".validation-cache")
                            throw std::invalid_argument("glTF dependency conflicts with import staging.");
                        Copy(source.parent_path(),path,output/path,remaining,files);
                        const auto depID=GenerateAssetID();
                        const auto type=dep.Image?AssetType::Texture2D:AssetType::Other;
                        const std::string importer=dep.Image?StbTextureImporter{}.Identifier():PassthroughImporter{}.Identifier();
                        candidate.Insert({depID,type,AssetOrigin::SourceFile,folder/path,importer,1,{}});
                        dependencies.push_back({depID,type});
                        if(dep.Image)
                            (void)ImportRenderTexture(project.ProjectRoot(),{depID},{},candidate,imports,cache);
                    }
                    GltfSceneImporter importer;
                    candidate.Insert({id,AssetType::Scene,AssetOrigin::SourceFile,relative,importer.Identifier(),1,dependencies});
                    const auto externalTexture=[&](std::string_view uri,TextureSemantic semantic)
                    {
                        const auto metadata=candidate.FindByPath(folder/GltfLocalUriPath(uri));
                        if(!metadata||metadata->Type!=AssetType::Texture2D)
                            throw std::invalid_argument("Missing registered glTF texture dependency.");
                        (void)ImportRenderTexture(project.ProjectRoot(),{metadata->ID},GltfImageSettings(semantic),candidate,imports,cache);
                        return kairo::renderer::TextureHandle{1};
                    };
                    const auto embeddedTexture=[](const GltfTextureBinding& binding,TextureSemantic semantic)
                    {
                        (void)DecodeEmbeddedGltfTexture(binding,semantic);
                        return kairo::renderer::TextureHandle{1};
                    };
                    (void)ImportRenderGltfSceneWithSource(project.ProjectRoot(),{id},candidate,imports,cache,externalTexture,embeddedTexture);
                }
                else if(kind==Kind::Mesh)
                {
                    OBJMeshImporter importer;
                    candidate.Insert({id,AssetType::Mesh,AssetOrigin::SourceFile,relative,importer.Identifier(),1,{}});
                    (void)ImportRenderMesh(project.ProjectRoot(),{id},candidate,imports,cache);
                }
                else if(kind==Kind::Texture)
                {
                    StbTextureImporter importer;
                    candidate.Insert({id,AssetType::Texture2D,AssetOrigin::SourceFile,relative,importer.Identifier(),1,{}});
                    (void)ImportRenderTexture(project.ProjectRoot(),{id},{},candidate,imports,cache);
                }
                else
                {
                    WavAudioImporter importer;
                    candidate.Insert({id,AssetType::Audio,AssetOrigin::SourceFile,relative,importer.Identifier(),1,{}});
                    ImportRecord record{id,relative,importer.Identifier(),importer.Version(),{},{},1};
                    const auto result=ImportSourceAsset(project.ProjectRoot(),record,importer,candidate,imports,cache);
                    (void)ParseAudioDerivedArtifact(result.Artifact);
                }
                std::filesystem::remove_all(output/".validation-cache");
                ids.push_back(id);
            }
            AssetRegistry latest;
            LoadAssetManifest(manifest,latest);
            if(SerializeAssetManifest(latest)!=baseline)
                throw std::runtime_error("Asset manifest changed during import.");
            SaveAssetManifest(manifest,candidate);
        }
        catch(...)
        {
            for(const auto& output:reserved)
            {
                std::error_code ignored;
                std::filesystem::remove_all(output,ignored);
            }
            throw;
        }
        return ids;
    }

    // Existing callers retain the original one-file OBJ contract.
    [[nodiscard]] inline kairo::assets::AssetID ImportStandaloneOBJ(
        const ProjectSession& project,const std::filesystem::path& requested)
    {
        if(asset_import_detail::Classify(requested)!=asset_import_detail::Kind::Mesh)
            throw std::invalid_argument("Choose a standalone OBJ file.");
        return ImportAssetBatch(project,std::span<const std::filesystem::path>(&requested,1)).front();
    }
}
