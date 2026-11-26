module;
#include <algorithm>
#include <ranges>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

export module Kairo.Editor.ScenePrefab;
import Kairo.Editor.SceneFragment;
import Kairo.Editor.SceneCommands;
import Kairo.Editor.CommandHistory;
import Kairo.Editor.ProjectSession;
import Kairo.Editor.ProjectPaths;
import Kairo.EngineCore;
import Kairo.Assets;

export namespace kairo::editor
{
    // The canonical scene serializer is also the prefab serialization
    // authority. Assets retain their project UUIDs; no runtime handles cross
    // the boundary. Definitions are independent snapshots, not live links.
    inline constexpr std::size_t MaximumPrefabEntities=512;
    inline constexpr std::uintmax_t MaximumPrefabBytes=4u*1024u*1024u;

    [[nodiscard]] inline std::string ValidatePrefabName(std::string_view name)
    {
        if(name.empty()||name.size()>64||name.front()==' '||name.back()==' ')
            throw std::invalid_argument("Prefab name must contain 1–64 portable characters.");
        for(char c:name)
            if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c==' '))
                throw std::invalid_argument("Prefab name accepts letters, digits, spaces, hyphens and underscores.");
        std::string lower(name);
        for(char& c:lower)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
        if(lower=="con"||lower=="prn"||lower=="aux"||lower=="nul"||
            (lower.size()==4&&(lower.starts_with("com")||lower.starts_with("lpt"))&&lower[3]>='1'&&lower[3]<='9'))
            throw std::invalid_argument("Reserved prefab name.");
        return std::string(name);
    }

    [[nodiscard]] inline PrefabTemplate CaptureProjectPrefab(
        const ProjectSession& project,kairo::engine::Entity root,std::string_view name)
    {
        PrefabTemplate result;
        result.Name=ValidatePrefabName(name);
        result.Fragment=CaptureSceneFragment(project.Scene(),{root});
        if(result.Fragment.Entities.size()>MaximumPrefabEntities)
            throw std::length_error("Prefab exceeds entity budget.");
        // Make the detached root a reusable local-space object. Position is
        // chosen when instantiating, not inherited from the source scene.
        for(auto& entity:result.Fragment.Entities)
            if(entity.Parent<0)entity.Transform.Local.Translation={};
        return result;
    }

    [[nodiscard]] inline std::filesystem::path SaveProjectPrefab(
        const ProjectSession& project,kairo::engine::Entity root,std::string_view name)
    {
        if(project.HasUnsavedChanges())throw std::logic_error("Save the project before creating a prefab.");
        auto prefab=CaptureProjectPrefab(project,root,name);
        kairo::engine::Scene detached;
        (void)InstantiatePrefab(detached,std::move(prefab));
        const auto bytes=kairo::engine::SerializeScene(detached,project.Assets());
        if(bytes.size()>MaximumPrefabBytes)throw std::length_error("Prefab exceeds byte budget.");
        const auto relative=std::filesystem::path("Prefabs")/ValidatePrefabName(name);
        if(std::filesystem::is_symlink(project.ProjectRoot()/"Prefabs"))throw std::invalid_argument("Linked prefab directories are unsupported.");
        const auto parent=ResolveProjectOutput(project.ProjectRoot(),"Prefabs");
        std::filesystem::create_directories(parent);
        const auto key=kairo::assets::PortableAssetPathKey(relative);
        for(const auto& entry:std::filesystem::directory_iterator(parent))
            if(kairo::assets::PortableAssetPathKey(std::filesystem::path("Prefabs")/entry.path().filename())==key)
                throw std::runtime_error("A prefab with this name already exists.");
        const auto output=ResolveProjectOutput(project.ProjectRoot(),relative);
        // Reserve a directory atomically and never replace an existing prefab.
        if(!std::filesystem::create_directory(output))
            throw std::runtime_error("A prefab with this name already exists.");
        try {kairo::engine::SaveScene(output/"Prefab.kscene",detached,project.Assets());}
        catch(...)
        {
            std::error_code ec;std::filesystem::remove_all(output,ec);throw;
        }
        return relative/"Prefab.kscene";
    }

    [[nodiscard]] inline std::vector<std::string> ListProjectPrefabs(const ProjectSession& project)
    {
        if(std::filesystem::is_symlink(project.ProjectRoot()/"Prefabs"))throw std::invalid_argument("Linked prefab directories are unsupported.");
        const auto folder=ResolveProjectOutput(project.ProjectRoot(),"Prefabs");
        std::vector<std::string> result;
        if(!std::filesystem::exists(folder))return result;
        for(const auto& entry:std::filesystem::directory_iterator(folder))
        {
            if(entry.is_symlink()||!entry.is_directory())continue;
            const auto name=entry.path().filename().string();
            try{(void)ValidatePrefabName(name);}catch(const std::invalid_argument&){continue;}
            const auto file=entry.path()/"Prefab.kscene";
            if(!std::filesystem::is_symlink(file)&&std::filesystem::is_regular_file(file))
            {
                if(result.size()>=512)throw std::length_error("Prefab library exceeds 512 entries.");
                result.push_back(name);
            }
        }
        std::ranges::sort(result);
        return result;
    }

    // A revision token is the exact bounded source, not a timestamp: edits
    // with the same length or filesystem timestamp still invalidate a review.
    [[nodiscard]] inline std::filesystem::path ExistingPrefabPath(
        const ProjectSession& project,std::string_view name)
    {
        const auto valid=ValidatePrefabName(name);
        const auto library=ListProjectPrefabs(project);
        if(std::ranges::find(library,valid)==library.end())
            throw std::invalid_argument("Select an existing prefab using its exact name.");
        const auto relative=std::filesystem::path("Prefabs")/valid/"Prefab.kscene";
        const auto file=ResolveExistingProjectFile(project.ProjectRoot(),relative,"prefab");
        if(std::filesystem::is_symlink(project.ProjectRoot()/"Prefabs")||
            std::filesystem::is_symlink(project.ProjectRoot()/relative)||
            std::filesystem::is_symlink((project.ProjectRoot()/relative).parent_path()))
            throw std::invalid_argument("Linked prefab files are unsupported.");
        return file;
    }

    [[nodiscard]] inline std::string ReadProjectPrefabRevision(
        const ProjectSession& project,std::string_view name)
    {
        const auto file=ExistingPrefabPath(project,name);
        std::ifstream stream(file,std::ios::binary);
        if(!stream)throw std::runtime_error("Cannot read prefab.");
        std::string bytes(MaximumPrefabBytes+1,'\0');
        stream.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));
        if(stream.bad()||stream.gcount()>static_cast<std::streamsize>(MaximumPrefabBytes))
            throw std::runtime_error("Cannot read bounded prefab.");
        bytes.resize(static_cast<std::size_t>(stream.gcount()));
        return bytes;
    }

    [[nodiscard]] inline PrefabTemplate LoadProjectPrefab(const ProjectSession& project,std::string_view name)
    {
        const auto valid=ValidatePrefabName(name);
        const auto bytes=ReadProjectPrefabRevision(project,valid);
        const auto scene=kairo::engine::ParseScene(bytes,project.Assets());
        const auto roots=scene.RootEntities();
        if(roots.size()!=1||scene.Size()>MaximumPrefabEntities)
            throw std::invalid_argument("Prefab must have one root and at most 512 entities.");
        return {valid,1u,CaptureSceneFragment(scene,{roots.front()})};
    }

    // Replace only a reviewed definition, never an instance or the active
    // scene. SaveScene stages and atomically replaces a single file. The
    // revision check detects prior external changes, not simultaneous writers.
    inline void ReplaceProjectPrefab(const ProjectSession& project,
        kairo::engine::Entity root,std::string_view name,std::string_view expectedRevision)
    {
        if(project.HasUnsavedChanges())
            throw std::logic_error("Save the project before replacing a prefab.");
        const auto output=ExistingPrefabPath(project,name);
        auto prefab=CaptureProjectPrefab(project,root,name);
        kairo::engine::Scene detached;
        (void)InstantiatePrefab(detached,std::move(prefab));
        const auto bytes=kairo::engine::SerializeScene(detached,project.Assets());
        if(bytes.size()>MaximumPrefabBytes)throw std::length_error("Prefab exceeds byte budget.");
        if(ReadProjectPrefabRevision(project,name)!=expectedRevision)
            throw std::runtime_error("Prefab changed since review. Review the current definition again.");
        kairo::engine::SaveScene(output,detached,project.Assets());
    }

    // Commands validate on a candidate scene before changing ProjectSession.
    // Snapshot only the created subtree for stable redo, not the whole scene.
    class PlaceProjectPrefabCommand final:public EditorCommand
    {
    public:
        PlaceProjectPrefabCommand(ProjectSession& project,PrefabTemplate prefab,
            std::vector<PrefabOverride> overrides={},
            std::optional<kairo::engine::Entity> parent=std::nullopt)
            :m_Project(project),m_Prefab(std::move(prefab)),m_Overrides(std::move(overrides)),m_Parent(parent)
        {
            if(m_Prefab.Fragment.Entities.size()>MaximumPrefabEntities||
                std::ranges::count_if(m_Prefab.Fragment.Entities,[](const auto& e){return e.Parent<0;})!=1)
                throw std::invalid_argument("Prefab placement requires one bounded root.");
        }
        [[nodiscard]] std::string_view Name() const noexcept override{return "Instantiate Prefab";}
        [[nodiscard]] kairo::engine::Entity Root() const
        {
            if(m_Snapshots.empty())throw std::logic_error("Prefab has not been placed.");
            return m_Snapshots.front().ID;
        }
        void Execute() override
        {
            auto candidate=m_Project.Scene();
            if(m_Snapshots.empty())
            {
                // Never take the game's primary camera/listener from an
                // existing scene just because a reusable object contains one.
                auto prefab=m_Prefab;
                for(auto& item:prefab.Fragment.Entities)
                {
                    if(item.Camera)item.Camera->Primary=false;
                    if(item.AudioListener)item.AudioListener->Primary=false;
                }
                const auto created=InstantiatePrefab(candidate,std::move(prefab),m_Overrides,m_Parent);
                const auto it=std::ranges::find_if(m_Prefab.Fragment.Entities,[](const auto& e){return e.Parent<0;});
                const auto index=static_cast<std::size_t>(it-m_Prefab.Fragment.Entities.begin());
                auto snapshots=CaptureEntitySubtree(candidate,created.at(index));
                (void)kairo::engine::SerializeScene(candidate,m_Project.Assets());
                m_Snapshots=std::move(snapshots);
            }
            else
            {
                RestoreEntitySubtree(candidate,m_Snapshots);
                (void)kairo::engine::SerializeScene(candidate,m_Project.Assets());
            }
            m_Project.EditScene()=std::move(candidate);
        }
        void Undo() override
        {
            auto candidate=m_Project.Scene();
            candidate.DestroyEntity(Root());
            m_Project.EditScene()=std::move(candidate);
        }
    private:
        ProjectSession& m_Project;
        PrefabTemplate m_Prefab;
        std::vector<PrefabOverride> m_Overrides;
        std::optional<kairo::engine::Entity> m_Parent;
        std::vector<EntitySnapshot> m_Snapshots;
    };
}
