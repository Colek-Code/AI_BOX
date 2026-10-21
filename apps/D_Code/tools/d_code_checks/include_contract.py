"""Static declared includes, not a C preprocessor or a feature-matrix proof."""
from __future__ import annotations
import json
import os
from pathlib import Path
import re

STANDARD_HEADERS = frozenset('assert.h ctype.h errno.h float.h inttypes.h limits.h math.h stdarg.h stdbool.h stddef.h stdint.h stdio.h stdlib.h string.h time.h wchar.h'.split())


def inside(path, root):
    return path == root or root in path.parents


def owner(path, root):
    if not inside(path,root):return 'external'
    parts=path.relative_to(root).parts
    return '/'.join(parts[:3] if parts[:1]==('modules',) else parts[:1])


def strip_comments(text):
    pattern=r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*'
    return re.sub(pattern,lambda m: ''.join('\n' if c=='\n' else ' ' for c in m[0]) if m[0].startswith(('/*','//')) else m[0],text,flags=re.S)


def make_search_roots(makefile):
    """Read declared -I entries only; never execute Make or compiler recipes."""
    text=makefile.read_text(encoding='utf-8')
    values={'ROOT_PREFIX':'../../../..','MAKEFILE_DIR':makefile.parent.as_posix()+'/'}
    for key in ('ROOT_PREFIX','VENDOR_ROOT'):
        match=re.search(r'^'+key+r'\s*[:?]?=\s*([^\r\n#]+)',text,re.M)
        if match:values[key]=match[1].strip()
    values.setdefault('VENDOR_ROOT','$(ROOT_PREFIX)')
    def expand(value):
        for _ in range(4):
            changed=re.sub(r'\$\(([^)]+)\)',lambda m:values.get(m[1],m[0]),value)
            if changed==value:break
            value=changed
        return value
    lines=text.splitlines();block=None;index=0;unknown=[]
    while index<len(lines):
        line=lines[index];index+=1
        match=re.match(r'^INCLUDES\s*([:+?]?=)[ \t]*(.*)',line)
        if not match:continue
        value=match[2]
        while value.rstrip().endswith('\\') and index<len(lines):
            value=value.rstrip()[:-1]+' '+lines[index];index+=1
        substitution=re.fullmatch(r'\$\(subst\s+(.+?),(.+?),\$\(INCLUDES\)\)',value.strip())
        if substitution and block is not None:
            old,new=expand(substitution[1]),expand(substitution[2])
            if '$' in old+new:unknown.append(value)
            else:block=block.replace(old,new)
        elif match[1]=='+=':block=(block or '')+' '+expand(value)
        elif expand(value).strip().startswith('$('):unknown.append(value)
        else:block=expand(value)
    if block is None:return [],unknown+['No declared INCLUDES block']
    paths=[]
    for value in re.findall(r'-I([^\s\\]+)',block):
        if '$' in value:unknown.append(value);continue
        path=(makefile.parent/value).resolve()
        if path not in paths:paths.append(path)
    return paths,unknown


def declarations(text):
    conditions=[]
    # Keep physical line numbers when a directive has a continuation.
    lines=strip_comments(text).splitlines();index=0
    while index<len(lines):
        line=index+1;value=lines[index];index+=1
        while value.rstrip().endswith('\\') and index<len(lines):
            value=value.rstrip()[:-1]+lines[index];index+=1
        directive=re.match(r'^\s*#\s*(\w+)\b(.*)',value)
        if not directive:continue
        kind,tail=directive[1],directive[2].strip()
        if kind in ('if','ifdef','ifndef'):conditions.append(f'{line}: #{kind} {tail}')
        elif kind in ('elif','else') and conditions:conditions[-1]=f'{line}: #{kind} {tail}'
        elif kind=='endif' and conditions:conditions.pop()
        elif kind=='include':yield line,tail,list(conditions)


def inspect(root, search_roots, *, files=None, unresolved_roots=()):
    root=root.resolve();search_roots=[p.resolve() for p in search_roots]
    sources=list(files) if files is not None else []
    if files is None:
        for directory,children,names in os.walk(root,followlinks=False):
            base=Path(directory)
            children[:]=[name for name in children if name not in ('sdk','generated','.git','__pycache__')
                         and (base/name).relative_to(root).parts[:2]!=('build','out')]
            sources.extend(base/name for name in names if Path(name).suffix in ('.c','.h'))
    sources=sorted(sources)
    rows=[]
    project_names={p.name for p in sources if p.suffix=='.h'}
    for source in sources:
        source=source.resolve();source_owner=owner(source,root)
        for line,raw,conditions in declarations(source.read_text(encoding='utf-8',errors='replace')):
            row={'source':source.relative_to(root).as_posix(),'line':line,'include':raw,'source_owner':source_owner,
                 'target':None,'target_owner':None,'conditions':conditions,'activation':'not_evaluated' if conditions else 'unconditional'}
            literal=re.fullmatch(r'(["<])([^">]+)[">]',raw)
            if not literal or (literal[1]=='"' and not raw.endswith('"')) or (literal[1]=='<' and not raw.endswith('>')):
                rows.append({**row,'status':'unresolved','reason':'macro_or_nonliteral_include'});continue
            value=literal[2].replace('\\','/')
            path=Path(value)
            if path.is_absolute() or ':' in value:
                rows.append({**row,'status':'unresolved','reason':'absolute_include_not_portable'});continue
            roots=([source.parent] if literal[1]=='"' else [])+search_roots
            # Existence probes preserve search order; canonicalize only the match,
            # not every nonexistent path through every SDK include directory.
            target=next((directory/path for directory in roots if (directory/path).is_file()),None)
            if target is not None:target=target.resolve()
            if target is None:
                external=literal[1]=='<' and value in STANDARD_HEADERS
                rows.append({**row,'status':'external' if external else 'unresolved',
                    'reason':'standard_header' if external else 'project_header_not_on_search_path' if path.name in project_names else 'target_not_resolved'});continue
            target_owner=owner(target,root)
            row.update(target=target.relative_to(root).as_posix() if inside(target,root) else str(target),target_owner=target_owner)
            if inside(target,root) and 'sdk' in target.relative_to(root).parts:
                parts=target.relative_to(root).parts;sdk=root.joinpath(*parts[:parts.index('sdk')+1])
                if any(inside(directory,sdk) and inside(target,directory) for directory in search_roots):
                    rows.append({**row,'status':'external','reason':'declared_vendor_sdk_header'});continue
            if target_owner=='external':
                declared=any(inside(target,directory) for directory in search_roots)
                rows.append({**row,'status':'external' if declared else 'unresolved','reason':'declared_external_root' if declared else 'escaped_search_roots'});continue
            if source_owner.startswith('modules/services/') and target_owner.startswith(('modules/applications/','modules/integrations/','modules/diagnostics/')):
                status,reason='violation','service_to_product_or_integration'
            elif source_owner!=target_owner and any(part in ('internal','private') for part in target.relative_to(root).parts[3:]):
                status,reason='violation','private_owner_boundary'
            elif '/' not in value and source.parent!=target.parent:
                status,reason='violation','cross_directory_short_include'
            else:status,reason='resolved','declared_owner_reference'
            rows.append({**row,'status':status,'reason':reason})
    counts={name:sum(row['status']==name for row in rows) for name in ('resolved','external','violation','unresolved')}
    incomplete=counts['unresolved']>0 or bool(unresolved_roots)
    return {'version':1,'boundary':'Declared source dependencies only; conditional activation and macro expansion are not proved.',
            'sources':len(sources),'counts':counts,'unresolved_search_roots':list(unresolved_roots),'findings':rows,
            'exit_code':1 if counts['violation'] else 2 if incomplete else 0}


def check(root, makefile, report_path=None):
    roots,unknown=make_search_roots(makefile)
    result=inspect(root,roots,unresolved_roots=unknown)
    if report_path:
        report_path.parent.mkdir(parents=True,exist_ok=True)
        report_path.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return result
