"""验证闹钟读取、BLE 输出及公开接入库绑定；不调用烧录器。"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import sys

from host_support import ROOT, prepare_work_directory
from production_c_function import self_test


def check_binding(binding, archive, header):
    """核对公开库的目标、接口版本及两个实际文件摘要。"""
    if binding.get('abi') != 1 or binding.get('target') != 'AC79NN-1.2.14/pi32v2-r3':
        raise ValueError('Unsupported public access-library ABI or target')
    for key, path in (('sha256', archive), ('public_header_sha256', header)):
        if binding.get(key) != hashlib.sha256(path.read_bytes()).hexdigest():
            raise ValueError('Access-library binding mismatch: ' + key)


def main():
    """先检查库绑定及负控，再运行两组生产函数／线程回归。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work-dir', required=True, type=Path)
    parser.add_argument('--cc', default='D:/mingw64/bin/gcc.exe')
    args = parser.parse_args()
    out = prepare_work_directory(ROOT, args.work_dir)
    qwen = ROOT/'apps/D_Code/modules/integrations/qwen'
    archive = qwen/'lib/libqwen_access.a'
    header = qwen/'qwen_access.h'
    binding = json.loads((qwen/'lib/libqwen_access.binding.json').read_text(encoding='utf-8'))
    check_binding(binding, archive, header)
    for key in ('abi', 'target', 'sha256', 'public_header_sha256'):
        corrupted = binding | {key: None}
        try:
            check_binding(corrupted, archive, header)
        except ValueError:
            continue
        raise AssertionError('Binding negative control escaped: ' + key)
    self_test()
    env = os.environ | {'PYTHONDONTWRITEBYTECODE': '1'}
    for name, launcher, extra in (
        ('ble', 'run_ble_response_tests.py', []),
        ('alarm', 'run_alarm_load_tests.py', ['--negative-controls']),
    ):
        stage = out/name
        stage.mkdir(exist_ok=True)
        child_env = env | dict(TEMP=str(stage), TMP=str(stage), TMPDIR=str(stage))
        result = subprocess.run([
            sys.executable, '-B', '-X', 'utf8', str(Path(__file__).with_name(launcher)),
            '--cc', args.cc, '--work-dir', str(stage), *extra,
        ], cwd=ROOT, env=child_env, capture_output=True, timeout=600)
        (out/(name+'.log')).write_bytes(result.stdout+result.stderr)
        if result.returncode:
            raise RuntimeError(name+' failed; inspect '+str(out/(name+'.log')))
        print('PASS', name, flush=True)
    check_binding(binding, archive, header)
    report = {'result': 'PASS', 'binding_negative_controls': 4,
              'archive_sha256': binding['sha256'],
              'boundary': 'Host contracts and archive binding only; native build and device acceptance are separate.'}
    (out/'result.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print('PASS public runtime regression and library binding')


if __name__ == '__main__':
    main()
