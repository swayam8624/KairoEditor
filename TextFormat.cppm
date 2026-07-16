export module Kairo.Editor.TextFormat;
export import Kairo.EngineCore.TextFormat;

export namespace kairo::editor
{
    using kairo::engine::FormatToken;
    using kairo::engine::TokenizeFormatLine;
    using kairo::engine::RequireTokenCount;
    using kairo::engine::QuoteFormatText;
    using kairo::engine::LoadBoundedTextFile;
    using kairo::engine::SaveTextFileAtomically;
}
