"""验证生产 BLE JSON 编解码及容量边界。"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
sys.dont_write_bytecode = True
from host_support import ROOT, prepare_work_directory
from production_c_function import function


def response_contract(out, cc):
    """从既有ATT模型生成有限域，执行生产输出函数和负控。

    Args:
        out: 本任务拥有的独立输出目录。
        cc: 只构建主机测试的C编译器路径。
    Raises:
        AssertionError: 规范反例或指定负控未得到预期结果。
        RuntimeError: 主机测试编译失败；原日志保留。
    """
    source = ROOT/'apps/D_Code/modules/services/ble_provisioning/d_ble_net_cfg.c'
    model = ROOT/'apps/D_Code/tests/contracts/ble_response.json'
    test = ROOT/'apps/D_Code/tests/host/network/ble_response_tests.c'
    contract = json.loads(model.read_text(encoding='utf-8'))['response_output_contract']
    production = source.read_text(encoding='utf-8')
    default = function(production, 'd_ble_encrypt').replace('d_ble_encrypt(', 'default_encrypt(', 1)
    send = function(production, 'd_ble_send_json_response')
    notify = function(production, 'ble_connected_net_notify')
    base_response = b'{"status":0,"mac":"000102030405","msg":"connected"}'
    declarations = []
    for name, macro in [('direct_lengths','DIRECT_LENGTH_COUNT'),('direct_capacities','DIRECT_CAPACITY_COUNT'),('send_lengths','SEND_LENGTH_COUNT')]:
        bounds = contract[name]
        values = list(range(bounds['min'],bounds['max']+1)) + bounds.get('extra',[])
        declarations += [f'static const unsigned {name}[] = {{' + ','.join(map(str, values)) + '};',
                         f'#define {macro} {len(values)}U']
    declarations += [f'#define IDENTITY_MIN {contract["identity_lengths"]["min"]}',
                     f'#define IDENTITY_MAX {contract["identity_lengths"]["max"]}',
                     f'#define BASE_RESPONSE_LENGTH {len(base_response)}U',
                     '#define UUID_FIELD_OVERHEAD 10U', '#define BATCH_FIELD_OVERHEAD 15U']
    assert set(contract['encryption_modes']) == {'plain','failure','invalid_length','padded'}
    declarations += ['enum { '+','.join('MODE_'+name.upper()+'='+str(i) for i,name in enumerate(contract['encryption_modes']))+' };',
                     f'#define ENCRYPTION_MODE_COUNT {len(contract["encryption_modes"])}U',
                     'static const unsigned format_fail_calls[] = {'+','.join(map(str,contract['formatter_fail_calls']))+'};',
                     f'#define FORMAT_FAIL_COUNT {len(contract["formatter_fail_calls"])}U']
    variants = [('production',default,send,notify,False)]
    for control in contract['negative_controls']:
        fragments = dict(default=default,send=send,notify=notify)
        target = fragments[control['function']]
        count = control.get('count',1)
        fragments[control['function']] = target.replace(control['before'],control['after']) if target.count(control['before'])==count else None
        variants.append((control['id'],fragments['default'],fragments['send'],fragments['notify'],True))
    results = []
    for name, encryption, sending, notification, negative in variants:
        if encryption is None or sending is None or notification is None: raise AssertionError('BLE response negative anchor missing: '+name)
        stage=out/name;stage.mkdir(parents=True,exist_ok=True)
        (stage/'ble_default_encrypt.inc').write_text(encryption,encoding='utf-8')
        (stage/'ble_response_functions.inc').write_text(sending+'\n'+notification,encoding='utf-8')
        (stage/'ble_response_cases.inc').write_text('\n'.join(declarations),encoding='utf-8')
        exe=stage/'test.exe';env=os.environ|dict(TEMP=str(stage),TMP=str(stage),TMPDIR=str(stage))
        compiled=subprocess.run([cc,'-std=c11','-Wall','-Wextra','-Werror','-I'+str(stage),str(test),'-o',str(exe)],env=env,capture_output=True,timeout=60)
        (stage/'compile.log').write_bytes(compiled.stdout+compiled.stderr)
        if compiled.returncode: raise RuntimeError('BLE response compile: '+str(stage/'compile.log'))
        run=subprocess.run([str(exe)],cwd=stage,env=env,capture_output=True,timeout=30)
        (stage/'run.log').write_bytes(run.stdout+run.stderr)
        if bool(run.returncode)!=negative: raise AssertionError('BLE response '+name+' exit='+str(run.returncode))
        results.append(dict(name=name,exit_code=run.returncode,negative_rejected=negative))
    report=dict(result='PASS',variants=results,sources={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (source,model,test,Path(__file__))},boundary=contract['boundary'])
    (out/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print('PASS BLE response output: finite capacity/identity domains and three negative controls')


def main():
    """编译生产 BLE 输出函数并执行有限域与三个负控。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work-dir', required=True, type=Path)
    parser.add_argument('--cc', default='D:/mingw64/bin/gcc.exe')
    args = parser.parse_args()
    response_contract(prepare_work_directory(ROOT,args.work_dir)/'response',args.cc)
if __name__ == '__main__': main()
