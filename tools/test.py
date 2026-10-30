#!/usr/bin/env python3
"""Compile and run the host-side tests (needs g++). Hardware code is not covered here."""
from pathlib import Path
import shutil, subprocess

root = Path(__file__).resolve().parent.parent
sketch = root / 'Desk_Watch_CYD_V1'
gxx = shutil.which('g++')
if not gxx:
    raise SystemExit('Install g++ to run the host tests.')
(root / '.build').mkdir(exist_ok=True)
for name, sources in [('desk_logic_test', ['DeskLogic.cpp']), ('task_store_test', ['TaskStore.cpp']), ('improv_test', ['Improv.cpp']), ('tz_rule_test', [])]:
    binary = root / '.build' / name
    subprocess.run([gxx, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-I', str(root / 'tests' / 'mocks'), '-I', str(sketch),
                    str(root / 'tests' / (name + '.cpp')), *[str(sketch / s) for s in sources], '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

# Every time zone rule shipped to the watch must be accepted by the firmware's validator.
import json, re
data = json.loads((root / 'tools' / 'timezones.json').read_text(encoding='utf8'))
ok = re.compile(r"^(?:[A-Za-z]|<)[A-Za-z0-9+,.:/<>-]*$")
bad = [r for r in data['r'] if len(r) >= 48 or not ok.match(r)]
assert not bad, bad
assert all(c[3] for c in data['c']), 'a country without cities'
print('timezone data OK:', len(data['r']), 'rules,', len(data['c']), 'countries')
