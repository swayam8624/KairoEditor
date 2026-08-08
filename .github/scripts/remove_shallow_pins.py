from pathlib import Path
p = Path('CMakeLists.txt')
s = p.read_text()
s = s.replace('\n            GIT_SHALLOW TRUE)', ')')
p.write_text(s)
Path('.github/workflows/remove-shallow-pins.yml').unlink()
Path('.github/scripts/remove_shallow_pins.py').unlink()
