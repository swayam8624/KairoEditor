from pathlib import Path
p = Path('CMakeLists.txt')
s = p.read_text()
s = s.replace('set(KAIRO_EDITOR_CORE_REVISION 10db0f120c158e3a34c4897c7eec4e519bf81cd2)', 'set(KAIRO_EDITOR_CORE_REVISION a3cf25acb7ae26125c9d40df29b487d244c6ea83)')
s = s.replace('set(KAIRO_EDITOR_RENDERER_REVISION d59d0c7293e379e64e8f9aef198ca302fa87558b)', 'set(KAIRO_EDITOR_RENDERER_REVISION c50d9ab8452c5baa8edca730238838034e3afa1f)')
p.write_text(s)
Path('.github/workflows/refresh-phase1-5-pins.yml').unlink()
Path('.github/scripts/refresh_phase1_5_pins.py').unlink()
