export module Kairo.Editor.ProjectDescriptor;
export import Kairo.EngineCore.ProjectDescriptor;

export namespace kairo::editor
{
    using kairo::engine::ProjectBuildKind;
    using kairo::engine::ProjectBuildProfile;
    using kairo::engine::ProjectDescriptor;
    using kairo::engine::ProjectFormatError;
    using kairo::engine::ValidateProjectDescriptor;
    using kairo::engine::ParseProjectDescriptor;
    using kairo::engine::SerializeProjectDescriptor;
    using kairo::engine::LoadProjectDescriptor;
    using kairo::engine::SaveProjectDescriptor;
}
