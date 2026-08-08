from pathlib import Path

cmake = Path('CMakeLists.txt')
s = cmake.read_text()
s = s.replace('set(KAIRO_EDITOR_CORE_REVISION agent/phase1-5-portability-hardening)',
              'set(KAIRO_EDITOR_CORE_REVISION agent/phases6-12-runtime)')
s = s.replace('set(KAIRO_EDITOR_RENDERER_REVISION agent/phase1-5-cross-platform-ci)',
              'set(KAIRO_EDITOR_RENDERER_REVISION df5e2fb145c45f072e9d5750f2ac8660ce306ad7)')
s = s.replace('ProjectLifecycle.cppm ProductionAuthoring.cppm\n    AIEditorTools.cppm',
              'ProjectLifecycle.cppm ProductionAuthoring.cppm AdvancedProductionAuthoring.cppm NativeGameplayAuthoring.cppm OfflineRenderAuthoring.cppm\n    AIEditorTools.cppm')
s = s.replace('add_executable(KairoEditorTests tests/EditorTests.cpp tests/ProductionAuthoringTests.cpp)',
              'add_executable(KairoEditorTests tests/EditorTests.cpp tests/ProductionAuthoringTests.cpp tests/AdvancedProductionAuthoringTests.cpp)')
cmake.write_text(s)

advanced = Path('AdvancedProductionAuthoring.cppm')
s = advanced.read_text()
qualified = 'std::set<kairo::assets::EditableFaceID>(result.begin(), result.end())'
s = s.replace('m_Faces = { result.begin(), result.end() };', f'm_Faces = {qualified};')
advanced.write_text(s)

native = Path('NativeGameplayAuthoring.cppm')
s = native.read_text().replace('#include <map>\n#include <stdexcept>', '#include <map>\n#include <optional>\n#include <stdexcept>')
native.write_text(s)

Path('.github/workflows/wire-phases6-12-editor.yml').unlink()
Path('.github/scripts/wire_phases6_12_editor.py').unlink()
