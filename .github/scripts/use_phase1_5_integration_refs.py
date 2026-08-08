from pathlib import Path
p=Path('CMakeLists.txt')
s=p.read_text()
s=s.replace('set(KAIRO_EDITOR_CORE_REVISION a3cf25acb7ae26125c9d40df29b487d244c6ea83)','set(KAIRO_EDITOR_CORE_REVISION agent/phase1-5-portability-hardening)')
s=s.replace('set(KAIRO_EDITOR_RENDERER_REVISION c50d9ab8452c5baa8edca730238838034e3afa1f)','set(KAIRO_EDITOR_RENDERER_REVISION agent/phase1-5-cross-platform-ci)')
p.write_text(s)
Path('.github/workflows/use-phase1-5-integration-refs.yml').unlink()
Path('.github/scripts/use_phase1_5_integration_refs.py').unlink()
