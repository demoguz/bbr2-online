#!/bin/bash
# Release and source packages. Usage: bash tools/package.sh   (run after build.sh and uisrc/build.py)
# Only whitelisted files go into the zips: no logs, no online.ini, no game data.
set -e
cd "$(dirname "$0")/.."
python3 - <<'EOF'
import os, re, zipfile, glob
V = re.search(r'#define BBR2NET_SEMVER "([^"]+)"', open('src/core.h').read()).group(1)
os.makedirs('dist', exist_ok=True)
enet = open('third_party/enet/LICENSE', encoding='utf-8').read().strip()
def text(path):
    s = open(path, encoding='utf-8').read().replace('{VERSION}', V).replace('{ENET_LICENSE}', enet)
    return s.replace('\r\n', '\n').replace('\n', '\r\n') if path.endswith('.txt') else s
def add(z, arc, path=None, data=None):
    zi = zipfile.ZipInfo(arc, date_time=(2026, 1, 1, 0, 0, 0)); zi.compress_type = zipfile.ZIP_DEFLATED
    zi.external_attr = 0o644 << 16
    z.writestr(zi, data if data is not None else open(path, 'rb').read())
R = 'docs/release/'
# --- release ---
rel = 'dist/BBR2Online_%s.zip' % V
with zipfile.ZipFile(rel, 'w') as z:
    add(z, 'version.dll', 'build/version.dll')
    for f in sorted(os.listdir('ui')):
        if f.endswith(('.fc', '.db')) or f in ('overrides.txt', 'tracks.txt'): add(z, 'bbr2mod/ui/' + f, 'ui/' + f)
    add(z, 'bbr2mod/steam/bbr2steam.exe', 'build/bbr2steam.exe')
    add(z, 'bbr2mod/steam/steam_appid.txt', data=b'480\r\n')
    for f in ('README.txt', 'KURULUM.txt', 'THIRD_PARTY_NOTICES.txt'): add(z, f, data=text(R + f))
    add(z, 'LICENSE.txt', data=text(R + 'LICENSE').replace('\n', '\r\n'))
# --- source ---
src = 'dist/BBR2Online_%s_source.zip' % V
P = 'bbr2online-%s/' % V
files = sorted(['docs/release/LICENSE', 'docs/release/RELEASE_NOTES.md'] + glob.glob('src/*.[ch]') + glob.glob('src/*.def') + glob.glob('uisrc/*.py') + glob.glob('tests/*.c')
               + glob.glob('third_party/enet/*.c') + glob.glob('third_party/enet/include/enet/*.h')
               + ['third_party/enet/LICENSE', 'build.sh', 'tools/package.sh', 'docs/dev/bbr2net.example.ini']
               + ['tools/%s.py' % t for t in ('apf', 'bbre', 'fc', 'fcdump', 'peinfo', 'uitree', 'outline')])
with zipfile.ZipFile(src, 'w') as z:
    for f in files: add(z, P + f, f)
    add(z, P + 'README.md', data=text('docs/README.md'))
    add(z, P + 'docs/TECHNICAL.md', data=text('docs/TECHNICAL.md'))
    for f in ('README.txt', 'KURULUM.txt', 'THIRD_PARTY_NOTICES.txt'): add(z, P + 'docs/release/' + f, R + f)
    add(z, P + 'THIRD_PARTY_NOTICES.txt', data=text(R + 'THIRD_PARTY_NOTICES.txt'))
    add(z, P + 'LICENSE', R + 'LICENSE')
    add(z, P + '.gitignore', data='build/\nui/\ndist/\ntoolchain/\n_to_delete/\n__pycache__/\ntools/.cache/\n*.log\nnetlog*.txt\nonline.ini\nbbr2net.ini\n')
# --- GitHub folder: the source zip unpacked, ready to upload ---
import shutil
G = 'dist/github'
if os.path.isdir(G):   # replace the old copy (moved aside, in case deleting is not allowed)
    import time; old = '_to_delete/github_%d' % time.time(); os.makedirs('_to_delete', exist_ok=True)
    try: shutil.rmtree(G)
    except OSError: shutil.move(G, old)
with zipfile.ZipFile(src) as z:
    for n in z.namelist():
        d = os.path.join(G, n[len(P):]); os.makedirs(os.path.dirname(d), exist_ok=True); open(d, 'wb').write(z.read(n))
for p in (rel, src): print(p, os.path.getsize(p), 'bytes,', len(zipfile.ZipFile(p).namelist()), 'files')
EOF
