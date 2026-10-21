"""Build and verify public credential-free blobs without editing local configuration."""
from __future__ import annotations
import fnmatch
import hashlib
import json
from pathlib import Path
import re

STRING = rb'"(?:\\.|[^"\\])*"'
STRINGS = STRING + rb'(?:(?:\s|\\\r?\n|/\*.*?\*/)*' + STRING + rb')*'
DEFINE = re.compile(rb'(?m)^[ \t]*(?://[ \t]*)?#[ \t]*define[ \t]+(?P<symbol>[A-Za-z_][A-Za-z_0-9]*)[ \t]+(?:\\\r?\n\s*)?(?P<value>' + STRINGS + rb')')
ASSIGN = re.compile(rb'(?P<symbol>[A-Za-z_][A-Za-z_0-9]*)\s*(?:\[[^\]\r\n]*\])?\s*=\s*(?P<value>' + STRINGS + rb')')
JSON_FIELD = re.compile(rb'"(?P<symbol>[A-Za-z_][A-Za-z_0-9]*)"\s*:\s*(?P<value>' + STRING + rb')')
WIFI_NAME = rb'(?P<symbol>wifi_(?:enter_sta_mode|set_sta_mode|sta_connect|connect))'
WIFI_SSID = re.compile(WIFI_NAME + rb'\s*\(\s*(?P<value>' + STRINGS + rb')\s*,')
WIFI_PASSWORD = re.compile(WIFI_NAME + rb'\s*\(\s*(?:' + STRINGS + rb'|[A-Za-z_][A-Za-z_0-9.>\[\]-]*)\s*,\s*(?P<value>' + STRINGS + rb')')
PATTERNS = {'define': DEFINE, 'assignment': ASSIGN, 'json': JSON_FIELD,
            'wifi_ssid_call': WIFI_SSID, 'wifi_password_call': WIFI_PASSWORD}
SENSITIVE = re.compile(r'(?i)(?:password|passwd|pwd|api_?key|api_?secret|app_?key|app_?secret|access_?key|auth_?key|private_?key|device_?key|product_?secret|device_?secret|client_?secret|factory_?secret|license_?key|activation_?code|auth_?code|access_?token|^token$|^secret$|^ssid$|(?:STA|INIT|ROUTER|WIFI).*SSID$)')
TOKEN_PATTERNS = (
    re.compile(rb'(?<![A-Za-z0-9_])gh[pousr]_[A-Za-z0-9]{36,255}'),
    re.compile(rb'github_pat_[A-Za-z0-9_]{40,255}'),
    re.compile(rb'(?<![A-Za-z0-9])sk-(?:proj-)?[A-Za-z0-9_-]{32,255}'),
    re.compile(rb'(?<![A-Za-z0-9])AKIA[0-9A-Z]{16}(?![A-Za-z0-9])'),
    re.compile(rb'(?i)\bBearer[ \t]+[A-Za-z0-9._~-]{20,}'),
    re.compile(rb'https?://[^\s/:"<>]+:[^\s/@"<>]+@'),
    re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH |ENCRYPTED )?PRIVATE KEY-----[A-Za-z0-9+/= \r\n]{80,}-----END (?:RSA |EC |OPENSSH |ENCRYPTED )?PRIVATE KEY-----'),
)

class PublicationError(ValueError):
    """Only locations and categories belong in this exception, never source values."""

def load_policy(path: Path) -> dict:
    policy = json.loads(path.read_text(encoding='utf-8'))
    if policy.get('version') != 1:
        raise PublicationError('Unsupported public credential policy')
    return policy

def excluded(path: str, policy: dict) -> bool:
    return any(fnmatch.fnmatchcase(path.casefold(), pattern.casefold()) for pattern in policy['exclude'])

def placeholder(value: bytes) -> bool:
    content = value.strip().strip(b'"').strip()
    return not content or bool(re.fullmatch(rb'(?:YOUR_[A-Z0-9_]+|CHANGE_ME|<[^>]+>|[xX.*]+|0{8,}|\$\{[A-Z0-9_]+\})', content))

def matches(data: bytes, kind: str):
    in_code = kind == 'assignment' or kind.startswith('wifi_')
    quoted = [m.span() for m in re.finditer(STRING, data)] if in_code else []
    cursor = 0
    for match in PATTERNS[kind].finditer(data):
        if in_code:
            while cursor < len(quoted) and quoted[cursor][1] <= match.start():
                cursor += 1
            if cursor < len(quoted) and quoted[cursor][0] <= match.start() < quoted[cursor][1]:
                continue
        yield match

def sanitize(path: str, data: bytes, policy: dict) -> tuple[bytes, list[dict], set[bytes]]:
    edits = {}
    report = []
    removed = set()
    for rule in policy['slots'].get(path, []):
        found = [m for m in matches(data, rule['kind']) if m['symbol'].decode() == rule['symbol']]
        if len(found) != rule['count']:
            raise PublicationError(f"Credential declaration changed: {path}: {rule['symbol']}")
        for match in found:
            start, end = match.span('value')
            replacement = json.dumps(rule.get('public_value', '')).encode('ascii')
            if match['value'] != replacement:
                edits[start, end] = replacement
                report.append({'path': path, 'line': data.count(b'\n', 0, start) + 1,
                               'symbol': rule['symbol'], 'action': 'public_placeholder'})
                # Keep exact high-entropy literal bytes only in memory for residual checks.
                for token in re.finditer(STRING, match['value']):
                    value = token[0][1:-1]
                    if len(value) >= 16 and b'\\' not in value and not placeholder(token[0]):
                        removed.add(value)
    previous = len(data) + 1
    for (start, end), replacement in sorted(edits.items(), reverse=True):
        if end > previous:
            raise PublicationError(f'Overlapping credential declarations: {path}')
        data = data[:start] + replacement + data[end:]
        previous = start
    if path in policy.get('strip_private_pem', []):
        blocks = list(TOKEN_PATTERNS[-1].finditer(data))
        for block in blocks:
            report.append({'path':path,'line':data.count(b'\n',0,block.start())+1,
                           'symbol':'PRIVATE_KEY','action':'remove_private_pem_block'})
        data = TOKEN_PATTERNS[-1].sub(b'',data)
    return data, report, removed

def scan(path: str, data: bytes, policy: dict) -> list[dict]:
    findings = []
    lower = data.lower()
    signals = (any(p in data for p in (b'ghp_',b'gho_',b'ghu_',b'ghs_',b'ghr_')),
               b'github_pat_' in data,b'sk-' in data,b'AKIA' in data,b'bearer' in lower,
               b'http' in data,b'PRIVATE KEY-----' in data)
    for pattern, present in zip(TOKEN_PATTERNS,signals):
        if not present:
            continue
        for match in pattern.finditer(data):
            allowed = policy.get('noncredential_token_shapes', {}).get(path, [])
            if hashlib.sha256(match[0]).hexdigest() in allowed:
                continue
            if not re.fullmatch(rb'(?:gh[pousr]_|sk-)[xX0_-]+', match[0]):
                findings.append({'path': path, 'line': data.count(b'\n', 0, match.start()) + 1,
                                 'kind': 'credential_token_shape'})
    extension = Path(path).suffix.lower()
    if extension not in {'.c', '.h', '.cpp', '.hpp', '.json', '.ini', '.conf', '.yaml', '.yml'}:
        return findings
    if not any(signal in lower for signal in (b'pass',b'pwd',b'key',b'secret',b'token',b'ssid',b'auth',b'wifi_',b'activat')):
        return findings
    if extension in {'.ini','.conf','.yaml','.yml'}:
        for match in re.finditer(rb'(?m)^[ \t]*([A-Za-z_][A-Za-z_0-9.-]*)[ \t]*[:=][ \t]*([^\r\n#;]+)',data):
            if SENSITIVE.search(match[1].decode()) and not placeholder(match[2]):
                findings.append({'path':path,'line':data.count(b'\n',0,match.start())+1,
                                 'symbol':match[1].decode(),'kind':'unreviewed_config_credential'})
        return findings
    accepted = policy.get('noncredential_literals', {}).get(path, {})
    kinds = ('json',) if Path(path).suffix.lower() == '.json' else ('define', 'assignment', 'wifi_ssid_call', 'wifi_password_call')
    for kind in kinds:
        for match in matches(data, kind):
            symbol = match['symbol'].decode()
            sensitive = kind.startswith('wifi_') or bool(SENSITIVE.search(symbol))
            if not sensitive or placeholder(match['value']):
                continue
            witness = hashlib.sha256(match['value']).hexdigest()
            if witness in accepted.get(symbol, []):
                continue
            findings.append({'path': path, 'line': data.count(b'\n', 0, match.start()) + 1,
                             'symbol': symbol, 'kind': 'unreviewed_credential_literal'})
    return findings

def redact_repeated_literals(data: bytes, values: set[bytes]) -> bytes:
    # Called for text files only. Zero filling preserves literal widths and C syntax.
    for value in sorted(values, key=len, reverse=True):
        data = data.replace(value, b'0' * len(value))
    return data
