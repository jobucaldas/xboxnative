"""Approximate Wine source graph, adapted from wine-coverage/analyze.py.

Not a proof of runtime portability; unresolved indirect calls remain invisible.
"""
import re, os, glob, collections
from pathlib import Path
W = os.environ.get("WINE_SOURCE", "/workspace/research/wine")
# ---------- 2. spec parsing ----------
def parse_spec(path):
    d = {}
    if not os.path.exists(path): return d
    for ln in Path(path).read_text(errors='replace').splitlines():
        ln = ln.strip()
        if ln.startswith('#') or not ln: continue
        m = re.match(r'(@|\d+)\s+(\w+)\s+(.*)', ln)
        if not m: continue
        conv, rest = m.group(2), m.group(3)
        flags = []
        while rest.startswith('-'):
            f, _, rest = rest.partition(' '); flags.append(f)
        if conv in ('stub', 'extern') and conv=='stub':
            nm = rest.split('(')[0].strip(); d[nm] = ('stub', None, flags); continue
        m2 = re.match(r'([^\s(]+)\s*(\([^)]*\))?\s*(\S+)?', rest)
        if not m2: continue
        nm, _, tgt = m2.groups()
        if conv in ('extern', 'variable'):
            d[nm] = ('data', tgt, flags); continue
        if tgt and '.' in tgt: d[nm] = ('fwd', tgt, flags)
        elif '-import' in flags: d[nm] = ('import', tgt, flags)
        else: d[nm] = ('impl', tgt or nm, flags)
    # Ordinals are valid import names even when Wine also provides a name.
    for ln in Path(path).read_text(errors='replace').splitlines():
        m = re.match(r'\s*(\d+)\s+\w+\s+((?:-\S+\s+)*)([^\s(]+)', ln)
        if m and m.group(3) in d:
            d['#' + m.group(1)] = d[m.group(3)]
    return d
specs = {}
def spec(dll):
    dll = dll.replace('.dll','')
    if dll not in specs: specs[dll] = parse_spec(f'{W}/dlls/{dll}/{dll}.spec')
    return specs[dll]

# ---------- 3. syscall tables ----------
def sysnames(path):
    return set(re.findall(r'SYSCALL_ENTRY\(\s*0x[0-9a-f]+,\s*(\w+)', Path(path).read_text()))
NT = sysnames(f'{W}/dlls/ntdll/ntsyscalls.h')
W32 = sysnames(f'{W}/dlls/win32u/win32syscalls.h')
ALLSYS = NT | W32

# ---------- 4. crude call graph ----------
KW = set('if while for switch return sizeof defined else case do'.split())
graph = collections.defaultdict(set)   # (dir,name) -> {callee names}
unixcall = set()
DEPS = {'kernel32':['kernelbase','ntdll'],'kernelbase':['ntdll'],'user32':['win32u','gdi32','ntdll'],'gdi32':['win32u','ntdll'],'winmm':['kernelbase','ntdll','user32'],'ntdll':[],'win32u':['ntdll']}
def deps(d): return [d]+DEPS.get(d,['kernelbase','ntdll','user32'])
dirs = sorted(os.path.basename(p) for p in glob.glob(f'{W}/dlls/*') if os.path.isdir(p))
_wp = Path(f'{W}/dlls/user32/winproc.c').read_text()
_m = re.search(r'struct wow_handlers16 wow_handlers =\s*\{(.*?)\};', _wp, re.S)
WOWH = set(re.findall(r'\b([A-Za-z_]\w+)\b', _m.group(1))) if _m else set()
def strip(s):
    s = re.sub(r'/\*.*?\*/', ' ', s, flags=re.S)
    s = re.sub(r'//[^\n]*', ' ', s)
    return re.sub(r'"(\\.|[^"\\])*"', '""', s)
for d in dirs:
    files = glob.glob(f'{W}/dlls/{d}/*.c')
    if d == 'win32u': files = [f'{W}/dlls/win32u/syscall.c']
    for f in files:
        if f.endswith('_test.c') : continue
        lines = strip(Path(f).read_text(errors='replace')).split('\n')
        i = 0
        while i < len(lines):
            if lines[i].startswith('{'):
                j = i-1; hdr=[]
                while j >= 0 and lines[j].strip() and not lines[j].rstrip().endswith((';','}')) and not lines[j].startswith('#'):
                    hdr.insert(0, lines[j]); j -= 1
                    if len(hdr) > 6: break
                m = re.search(r'\b(\w+)\s*\(', ' '.join(hdr))
                k = i+1
                while k < len(lines) and not lines[k].startswith('}'): k += 1
                if m:
                    name = m.group(1); body = '\n'.join(lines[i:k])
                    for c in re.findall(r'\b([A-Za-z_]\w*)\s*\(', body):
                        if c not in KW and c != name: graph[(d,name)].add(c)
                    if 'wow_handlers.' in body: graph[(d,name)].update(WOWH)
                    if 'WINE_UNIX_CALL' in body or '__wine_unix_call' in body or 'UNIX_CALL(' in body: unixcall.add((d,name))
                i = k
            i += 1
def canon(n):
    return 'Nt'+n[2:] if n.startswith('Zw') and ('Nt'+n[2:]) in ALLSYS else n
def reach(node):
    seen, sc, ux, stack = set(), set(), False, [node]
    while stack:
        f = stack.pop()
        if f in seen: continue
        seen.add(f)
        if f in unixcall: ux = True
        for c in graph.get(f, ()):
            c = canon(c)
            if c in ALLSYS: sc.add(c); continue
            for dd in deps(f[0]):
                if (dd, c) in graph:
                    if (dd, c) not in seen: stack.append((dd, c))
                    break
    return sc, ux, len(seen)

def resolve(dll, sym, depth=0):
    """-> (class, impl_dll, cname)"""
    dll = dll.replace(".dll","")
    s = spec(dll).get(sym)
    if s is None: return ('NOT_IN_WINE', dll, None)
    kind, tgt, flags = s
    if kind == 'stub': return ('WINE_STUB', dll, None)
    if kind == 'fwd':
        d2, n2 = tgt.rsplit('.',1); d2 = d2.lower()
        if depth<6: return resolve(d2, n2, depth+1)
    if kind == 'import':
        for d2 in ('kernelbase','ntdll','win32u','user32','gdi32full','advapi32','sechost'):
            if (sym in spec(d2) or (tgt and tgt in spec(d2))) and d2 != dll and depth<6:
                r = resolve(d2, tgt if tgt in spec(d2) else sym, depth+1)
                if r[0] != 'NOT_IN_WINE': return r
        if tgt: return ('IMPL', 'ntdll', tgt)
        return ('IMPORT_UNRESOLVED', dll, sym)
    if kind == 'impl': return ('IMPL', dll, tgt)
    return ('DATA', dll, tgt)

