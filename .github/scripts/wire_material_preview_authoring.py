from pathlib import Path
p=Path('CMakeLists.txt')
s=p.read_text().replace('OfflineRenderAuthoring.cppm ProductionSystemsAuthoring.cppm', 'OfflineRenderAuthoring.cppm MaterialPreviewAuthoring.cppm ProductionSystemsAuthoring.cppm')
p.write_text(s)
p=Path('KairoEditor.cppm')
s=p.read_text().replace('export import Kairo.Editor.OfflineRenderAuthoring;\n', 'export import Kairo.Editor.OfflineRenderAuthoring;\nexport import Kairo.Editor.MaterialPreviewAuthoring;\n')
p.write_text(s)
Path('.github/workflows/wire-material-preview-authoring.yml').unlink()
Path('.github/scripts/wire_material_preview_authoring.py').unlink()
