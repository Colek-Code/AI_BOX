"""Verify the GitHub push identity through Git's helper; never expose credential values."""
import json
import os
from pathlib import Path
import re
import subprocess
import urllib.error
import urllib.request

class IdentityError(RuntimeError):
    pass

def verify(root:Path,expected_login:str,expected_id:int,repository:str)->dict:
    if not re.fullmatch(r'[A-Za-z0-9-]+/[A-Za-z0-9_.-]+',repository):
        raise IdentityError('Invalid GitHub repository identity')
    if not re.fullmatch(r'[A-Za-z0-9-]+',expected_login):
        raise IdentityError('Invalid expected GitHub login')
    env={k:v for k,v in os.environ.items() if not k.startswith('GIT_')}
    env['GIT_TERMINAL_PROMPT']='0'
    request=(f'protocol=https\nhost=github.com\npath={repository}.git\nusername={expected_login}\n\n').encode()
    result=subprocess.run(['git','-c','credential.interactive=false','credential','fill'],cwd=root,
        input=request,capture_output=True,env=env)
    if result.returncode:
        raise IdentityError('GitHub sign-in is required before publishing')
    credentials=dict(line.split('=',1) for line in result.stdout.decode().splitlines() if '=' in line)
    token=credentials.get('password')
    if not token:
        raise IdentityError('GitHub credential was not available')
    def api(path):
        req=urllib.request.Request('https://api.github.com'+path,headers={
            'Authorization':'Bearer '+token,'Accept':'application/vnd.github+json',
            'User-Agent':'AI_BOX-publication-identity-check'})
        try:
            with urllib.request.urlopen(req,timeout=30) as response:return json.load(response)
        except urllib.error.HTTPError as error:
            raise IdentityError(f'GitHub identity or permission check failed: HTTP {error.code}') from None
        except urllib.error.URLError:
            raise IdentityError('GitHub identity check could not reach the service') from None
    try:
        user=api('/user')
        if user['login'].casefold()!=expected_login.casefold() or user['id']!=expected_id:
            raise IdentityError('Authenticated GitHub account does not match the reviewed publisher')
        repo=api('/repos/'+repository)
        if repo['full_name'].casefold()!=repository.casefold() or not repo.get('permissions',{}).get('push'):
            raise IdentityError('Authenticated GitHub account cannot push this repository')
        return {'login':user['login'],'user_id':user['id'],'repository':repo['full_name'],'can_push':True}
    finally:
        credentials.clear()
        token=None
