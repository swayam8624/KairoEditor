module;

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Kairo.Editor.ShippingAuthoring;

import Kairo.EngineCore.ShippingRuntime;

export namespace kairo::editor
{
    /// Task: provide Editor-safe authoring and deterministic auditioning without
    /// binding project data to a platform audio device.
    class RuntimeAudioAuthoringWorkspace final
    {
    public:
        void AddBus(kairo::engine::AudioBus bus) { m_Mixer.AddBus(std::move(bus)); }
        [[nodiscard]] std::uint64_t Audition(kairo::engine::AudioVoice voice)
        { return m_Mixer.Play(std::move(voice)); }
        void Stop(std::uint64_t voice) { m_Mixer.Stop(voice); }
        [[nodiscard]] kairo::engine::AudioMixFrame Preview(double deltaSeconds)
        { return m_Mixer.Step(deltaSeconds); }
        [[nodiscard]] const kairo::engine::RuntimeAudioMixer& Mixer() const noexcept { return m_Mixer; }
    private: kairo::engine::RuntimeAudioMixer m_Mixer;
    };

    /// Task: keep localization and widget authoring data rebuildable so every
    /// preview runs the same hierarchy validation as Player.
    class RuntimeUIAuthoringWorkspace final
    {
    public:
        void SetTranslation(std::string locale,std::string key,std::string value)
        { m_Catalog.Set(std::move(locale),std::move(key),std::move(value)); }
        void AddWidget(kairo::engine::RuntimeWidget widget)
        {
            auto candidate=m_Widgets; candidate.push_back(std::move(widget));
            (void)Build(candidate); m_Widgets=std::move(candidate);
        }
        [[nodiscard]] kairo::engine::RuntimeUIScene Preview() const { return Build(m_Widgets); }
    private:
        kairo::engine::LocalizationCatalog m_Catalog;
        std::vector<kairo::engine::RuntimeWidget> m_Widgets;
        [[nodiscard]] kairo::engine::RuntimeUIScene Build(
            const std::vector<kairo::engine::RuntimeWidget>& widgets) const
        { kairo::engine::RuntimeUIScene result(m_Catalog); for(const auto& widget:widgets) result.Add(widget); return result; }
    };

    /// Task: author typed save data and inspect replication deltas through the
    /// exact canonical state contract consumed by Player.
    class RuntimeStateAuthoringWorkspace final
    {
    public:
        void SetTick(std::uint64_t tick) noexcept { m_State.Tick=tick; }
        void Set(std::string key,kairo::engine::RuntimeStateValue value)
        { if(key.empty()) throw std::invalid_argument("Runtime state key cannot be empty."); m_State.Values[std::move(key)]=std::move(value); }
        void Remove(std::string_view key) { m_State.Values.erase(std::string(key)); }
        [[nodiscard]] const kairo::engine::RuntimeStateSnapshot& State() const noexcept { return m_State; }
        [[nodiscard]] kairo::engine::RuntimeStateDelta Diff(
            const kairo::engine::RuntimeStateSnapshot& baseline) const
        { return kairo::engine::DiffRuntimeState(baseline,m_State); }
        void Save(const std::filesystem::path& path) const { kairo::engine::SaveRuntimeState(path,m_State); }
        void Load(const std::filesystem::path& path) { m_State=kairo::engine::LoadRuntimeState(path); }
    private: kairo::engine::RuntimeStateSnapshot m_State;
    };
}
