#!/usr/bin/env python3
import re,os,glob,csv,subprocess
W='/workspace/research/wine'
PE='/usr/lib/x86_64-linux-gnu/wine/x86_64-windows'
OUT=os.path.dirname(os.path.abspath(__file__))
dlls='''ntdll kernel32 kernelbase win32u user32 gdi32 gdi32full advapi32 sechost ole32 combase oleaut32 rpcrt4 msvcrt ucrtbase msvcp140 vcruntime140 xaudio2_9 xaudio2_7 xactengine3_7 x3daudio1_7 mmdevapi winmm dsound dinput dinput8 xinput1_3 xinput1_4 hid setupapi imm32 shell32 shlwapi shcore comctl32 comdlg32 version d3d9 d3d11 dxgi wined3d d3d12 d3dcompiler_47 opengl32 dwmapi dwrite gdiplus windowscodecs ws2_32 iphlpapi bcrypt crypt32 secur32 winhttp wininet uxtheme avrt psapi dbghelp mscoree mfplat'''.split()
rows=[]
for d in dlls:
    dd=f'{W}/dlls/{d}'
    mk=f'{dd}/Makefile.in'
    if not os.path.exists(mk): rows.append([d,'NO_SOURCE_DIR']+['']*7); continue
    t=open(mk).read()
    unixlib=re.search(r'^UNIXLIB\s*=\s*(\S+)',t,re.M)
    imports=re.search(r'^IMPORTS\s*=\s*(.*)',t,re.M)
    unixlibs=re.search(r'^UNIX_LIBS\s*=\s*(.*)',t,re.M)
    srcs=glob.glob(f'{dd}/*.c')
    ucalls=sum(open(f,errors='replace').read().count('WINE_UNIX_CALL') for f in srcs)
    # imports from ntdll / win32u in the Debian-built PE (Wine 10)
    nt=u32=0; pe=f'{PE}/{d}.dll'
    if os.path.exists(pe):
        o=subprocess.run(['llvm-readobj-19','--coff-imports',pe],capture_output=True,text=True).stdout
        for m in re.finditer(r'Symbol: (\w+) \(',o): pass
        cur=None
        for ln in o.split('\n'):
            m=re.match(r'\s*Import \{',ln)
            m2=re.match(r'\s*Name: (\S+\.dll)',ln,re.I)
            if m2: cur=m2.group(1).lower()
            m3=re.match(r'\s*Symbol: (\w+) \(\d+\)',ln)
            if m3 and cur=='ntdll.dll' and re.match(r'(Nt|Zw)[A-Z]',m3.group(1)): nt+=1
            if m3 and cur=='win32u.dll': u32+=1
    ext=[x for x in re.findall(r'\$\((\w+)_PE_LIBS\)',imports.group(1) if imports else '')]
    if d in('winhttp','secur32','bcrypt','crypt32','ws2_32'): pass
    verdict='UNIX_SIDE_REQUIRED' if (unixlib or ucalls) else ('PE_ONLY_NEEDS_SYSCALLS' if (nt or u32) else 'PE_ONLY')
    rows.append([d,verdict,'yes' if unixlib else 'no',ucalls,nt,u32,(imports.group(1).strip() if imports else ''),' '.join(ext),(unixlibs.group(1).strip() if unixlibs else '')])
with open(f'{OUT}/wine-dll-table.csv','w',newline='') as f:
    w=csv.writer(f); w.writerow(['dll','verdict','has_unixlib','WINE_UNIX_CALL_sites','nt_syscall_imports(Wine10 PE)','win32u_imports','makefile_IMPORTS','bundled_PE_libs','UNIX_LIBS'])
    w.writerows(rows)
for r in rows: print(r[:6])

# ---- transitive closure over Debian Wine-10 PE import tables (static imports only) ----
UNIXDLLS={r[0] for r in rows if r[2]=='yes'}
def imps(d):
    pe=f'{PE}/{d}.dll'
    if not os.path.exists(pe): return set()
    o=subprocess.run(['llvm-readobj-19','--coff-imports',pe],capture_output=True,text=True).stdout
    return {m.lower()[:-4] for m in re.findall(r'Name: (\S+\.dll)',o,re.I)}
cache={}
def closure(d):
    seen=set(); st=[d]
    while st:
        x=st.pop()
        if x in seen: continue
        seen.add(x)
        if x not in cache: cache[x]=imps(x)
        st.extend(cache[x]-seen)
    return seen
out=[]
for r in rows:
    d=r[0]; c=closure(d)-{d}
    needs=sorted((c&UNIXDLLS)|(c&{'win32u','user32','gdi32'}))
    out.append(r+[' '.join(needs)])
with open(f'{OUT}/wine-dll-table.csv','w',newline='') as f:
    w=csv.writer(f); w.writerow(['dll','verdict','has_unixlib','WINE_UNIX_CALL_sites','nt_syscall_imports(Wine10 PE)','win32u_imports','makefile_IMPORTS','bundled_PE_libs','UNIX_LIBS','static_import_closure_hits(unix-side or window-system dlls)'])
    w.writerows(out)
for r in out:
    if r[0] in('xaudio2_9','winmm','dinput8','xinput1_3','msvcrt','ucrtbase','d3d11','d3d9','wined3d','ole32','shell32','mmdevapi','kernel32','user32','advapi32','winhttp','dsound'): print(r[0],'|',r[-1])
