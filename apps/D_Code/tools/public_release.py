"""Prepare sanitized public Git trees while retaining local credentials and build outputs."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from public_credentials import PublicationError, excluded, load_policy, redact_repeated_literals, sanitize, scan

ROOT=Path(__file__).resolve().parents[3]
POLICY=Path(__file__).with_name('public_credentials_policy.json')
BINARY_SUFFIXES={'.a','.lib','.bin','.fw','.ufw','.elf','.bc','.o','.exe','.dll','.png','.jpg','.otf','.ttf','.gz','.zip','.pdf','.xlsx','.xmind'}

def git(root,*args,data=None,index=None):
    env={k:v for k,v in os.environ.items() if not k.startswith('GIT_')}
    env['GIT_NO_REPLACE_OBJECTS']='1'
    if index:env['GIT_INDEX_FILE']=str(index)
    result=subprocess.run(['git',*args],cwd=root,input=data,capture_output=True,env=env)
    if result.returncode:
        raise PublicationError(f'Git operation failed: {args[0]}')
    return result.stdout

def sha(data):
    return hashlib.sha256(data).hexdigest()

def is_text(path,data):
    return Path(path).suffix.lower() not in BINARY_SUFFIXES and b'\0' not in data[:8192]

def prepare(root:Path,files:dict,output:Path,policy:dict)->dict:
    root=root.resolve();output=output.resolve()
    if not output.is_relative_to(root/'apps/D_Code/build/out'):
        raise PublicationError('Release output must remain in this project task area')
    output.mkdir(parents=True,exist_ok=True)
    index=output/'public.index'
    if index.exists():raise PublicationError('A candidate already exists; use a new output directory')
    selected={p:r for p,r in files.items() if not excluded(p,policy)}
    for name in selected:
        path=(root/name).resolve()
        if not path.is_relative_to(root) or (root/name).is_symlink() or (root/name).is_junction():
            raise PublicationError('Linked or escaping publication path')
    removed=set();changes=[];transformed={};local_hashes={}
    for name in policy['slots']:
        if name not in selected:continue
        data=(root/name).read_bytes()
        clean,report,values=sanitize(name,data,policy)
        changes.extend(report);removed.update(values)
        transformed[name]=clean
    residual=re.compile(b'|'.join(re.escape(v) for v in sorted(removed,key=len,reverse=True))) if removed else None
    findings=[];public_hashes={};bytes_total=0
    for count,(name,record) in enumerate(selected.items(),1):
        data=(root/name).read_bytes()
        local_hashes[name]={'sha256':sha(data),'bytes':len(data),'mode':record['mode']}
        clean=transformed.get(name,data)
        if is_text(name,data):
            clean=redact_repeated_literals(clean,removed)
        elif residual and residual.search(data):
            findings.append({'path':name,'kind':'credential_bytes_in_binary'})
        findings.extend(scan(name,clean,policy))
        if clean!=data:transformed[name]=clean
        public_hashes[name]=sha(clean)
        bytes_total+=len(clean)
        if count%4000==0:print(json.dumps({'state':'auditing_public_content','files':count}),flush=True)
    safe={'redactions':changes,'excluded':[p for p in files if p not in selected],
          'findings':findings,'scanned_files':len(selected),'bytes':bytes_total}
    (output/'sanitization-report.json').write_text(json.dumps(safe,ensure_ascii=False,indent=2),encoding='utf-8')
    if findings:
        print(json.dumps({'state':'review_required','findings':findings[:40],'total':len(findings)},ensure_ascii=False),flush=True)
        raise PublicationError('Unreviewed credential findings; no public candidate was created')
    git(root,'read-tree','--empty',index=index)
    paths=b'\0'.join(p.encode('utf-8') for p in selected)+b'\0'
    git(root,'--literal-pathspecs','add','-f','--pathspec-from-file=-','--pathspec-file-nul',data=paths,index=index)
    updates=[]
    for name,data in transformed.items():
        oid=git(root,'hash-object','-w','--stdin',data=data).decode().strip()
        updates.append(files[name]['mode'].encode()+b' '+oid.encode()+b'\t'+name.encode()+b'\0')
    if updates:git(root,'update-index','-z','--index-info',data=b''.join(updates),index=index)
    executable=[n for n,r in selected.items() if r['mode']=='100755']
    if executable:
        git(root,'update-index','--chmod=+x','-z','--stdin',data=b'\0'.join(n.encode() for n in executable)+b'\0',index=index)
    tree=git(root,'write-tree',index=index).decode().strip()
    # Confirm the actual Git blobs, rather than trusting a preview of replacements.
    verify_tree(root,tree,policy)
    result={'state':'sanitized_tree_verified','candidate_tree':tree,'candidate_index':str(index),
        'files':len(selected),'changed_text_files':len(transformed),'redacted_declarations':len(changes),
        'excluded_files':safe['excluded'],'file_verification':local_hashes,'public_sha256':public_hashes,
        'policy_sha256':sha(POLICY.read_bytes()),'local_source_edited':False}
    (output/'public-verification.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k not in ('file_verification','public_sha256')}),flush=True)
    return result

def verify_tree(root:Path,tree:str,policy:dict):
    rows=git(root,'ls-tree','-r','-z',tree).split(b'\0')
    batch=subprocess.Popen(['git','cat-file','--batch'],cwd=root,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    findings=[]
    try:
        for row in rows:
            if not row:continue
            header,path=row.split(b'\t',1);mode,kind,oid=header.split();name=path.decode('utf-8')
            if excluded(name,policy):raise PublicationError(f'Excluded file entered public tree: {name}')
            if kind!=b'blob' or mode not in (b'100644',b'100755'):raise PublicationError('Unexpected linked Git entry')
            batch.stdin.write(oid+b'\n');batch.stdin.flush()
            info=batch.stdout.readline().split();data=batch.stdout.read(int(info[2]));assert batch.stdout.read(1)==b'\n'
            pointer=re.fullmatch(rb'version https://git-lfs.github.com/spec/v1\noid sha256:([0-9a-f]{64})\nsize ([0-9]+)\n',data)
            if pointer:
                key=pointer[1].decode();obj=root/'.git/lfs/objects'/key[:2]/key[2:4]/key
                data=obj.read_bytes()
                if len(data)!=int(pointer[2]) or sha(data)!=key:raise PublicationError('LFS payload integrity failed')
            if name in policy['slots']:
                clean,report,_=sanitize(name,data,policy)
                if clean!=data:findings.append({'path':name,'kind':'unsanitized_declaration'})
            findings.extend(scan(name,data,policy))
        if findings:
            print(json.dumps({'state':'public_tree_rejected','findings':findings[:30],'total':len(findings)},ensure_ascii=False),flush=True)
            raise PublicationError('Public Git tree contains credential findings')
    finally:
        batch.stdin.close();batch.stdout.close()
        code=batch.wait(timeout=10)
        batch.stderr.close()
        if code!=0:raise PublicationError('Git object inspection failed')

def pre_push(root:Path,remote_name:str,remote_url:str,payload:bytes):
    from public_github_identity import verify
    expected_url=git(root,'config','--local','publicRelease.remote').decode().strip()
    if remote_name!='origin' or remote_url!=expected_url:
        raise PublicationError('Push destination differs from the reviewed public repository')
    login=git(root,'config','--local','publicRelease.githubLogin').decode().strip()
    user_id=int(git(root,'config','--local','publicRelease.githubUserId').decode().strip())
    repository=git(root,'config','--local','publicRelease.repository').decode().strip()
    identity=verify(root,login,user_id,repository)
    checked=set()
    for line in payload.splitlines():
        local_ref,local_sha,remote_ref,remote_sha=line.split()
        if set(local_sha)=={48}:continue
        args=['rev-list',local_sha.decode()]
        if set(remote_sha)!={48}:
            # Unknown remote history is not a reason to skip local history checks.
            known=subprocess.run(['git','cat-file','-e',remote_sha.decode()+'^{commit}'],cwd=root,capture_output=True)
            if known.returncode==0:args+=['^'+remote_sha.decode()]
        for commit in git(root,*args).decode().splitlines():
            tree=git(root,'rev-parse',commit+'^{tree}').decode().strip()
            if tree in checked:continue
            raw=git(root,'show',commit+':apps/D_Code/tools/public_credentials_policy.json')
            policy=json.loads(raw)
            if policy.get('version')!=1:raise PublicationError('Unreviewed public policy in outgoing history')
            verify_tree(root,tree,policy)
            checked.add(tree)
    print(json.dumps({'state':'push_guard_passed','login':identity['login'],'trees':len(checked)}),flush=True)
    result=subprocess.run(['git','lfs','pre-push',remote_name,remote_url],cwd=root,input=payload)
    if result.returncode:raise PublicationError('LFS pre-push did not complete')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest='action',required=True)
    p=sub.add_parser('prepare');p.add_argument('--manifest',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    v=sub.add_parser('verify');v.add_argument('--tree',required=True)
    h=sub.add_parser('pre-push');h.add_argument('remote_name');h.add_argument('remote_url')
    args=parser.parse_args();policy=load_policy(POLICY)
    if args.action=='prepare':
        data=json.loads(args.manifest.read_text(encoding='utf-8'))
        prepare(ROOT,data.get('files',data),args.output,policy)
    elif args.action=='verify':
        verify_tree(ROOT,args.tree,policy)
        print(json.dumps({'state':'public_tree_verified','tree':args.tree}))
    else:
        pre_push(ROOT,args.remote_name,args.remote_url,sys.stdin.buffer.read())

if __name__=='__main__':
    try:main()
    except (PublicationError,OSError) as error:
        print(json.dumps({'state':'blocked','category':type(error).__name__,'detail':str(error)},ensure_ascii=False),flush=True)
        raise SystemExit(1)
