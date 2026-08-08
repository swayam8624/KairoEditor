from pathlib import Path

p=Path('NativeGameplayAuthoring.cppm')
s=p.read_text()
if '#include <variant>' not in s:
    s=s.replace('#include <utility>\n', '#include <utility>\n#include <variant>\n')
p.write_text(s)

p=Path('tests/AdvancedProductionAuthoringTests.cpp')
s=p.read_text()
if '#include <variant>' not in s:
    s=s.replace('#include <string>\n', '#include <string>\n#include <variant>\n')
p.write_text(s)

p=Path('tests/ProductionSystemsAuthoringTests.cpp')
s=p.read_text()
if '#include <utility>' not in s:
    s=s.replace('#include <filesystem>\n', '#include <filesystem>\n#include <utility>\n')
p.write_text(s)

Path('.github/workflows/harden-phase6-12-editor-includes.yml').unlink()
Path('.github/scripts/harden_phase6_12_editor_includes.py').unlink()
