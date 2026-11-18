"""验证生产闹钟时钟、模型和工作线程。"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
ALARM_LOAD_CONTRACT_PATH = Path(__file__).with_name('contracts')/'alarm_load.json'
ALARM_LOAD_CONTRACT = json.loads(ALARM_LOAD_CONTRACT_PATH.read_text(encoding='utf-8'))['load_gate_contract']
PRESET_CASES = tuple('slots-'+a+b for a in ALARM_LOAD_CONTRACT['slot_kinds'] for b in ALARM_LOAD_CONTRACT['slot_kinds']) + ('defaults','restore-defaults','first-fail','edit','delete','clear','restore-clear')
sys.dont_write_bytecode = True
from host_support import ROOT, prepare_work_directory


def main():
    """验证生产闹钟时钟、模型和工作线程。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--cc", default="D:/mingw64/bin/gcc.exe")
    parser.add_argument("--negative-controls", action="store_true")
    args = parser.parse_args()
    output = prepare_work_directory(ROOT, args.work_dir) / "host"
    output.mkdir(parents=True, exist_ok=True)
    temporary = output / "tmp"
    temporary.mkdir(exist_ok=True)
    environment = os.environ | {"TEMP": str(temporary), "TMP": str(temporary), "TMPDIR": str(temporary)}
    dcode = ROOT / "apps/D_Code"
    feature = dcode / "modules/applications/product_ui/features/time"
    tests = ROOT / "apps/D_Code/tests/host"
    host = tests / "lvgl_host"
    calendar = feature / "product_ui_time_domain_calendar.c"
    models = [feature / ("product_ui_alarm_" + name + ".c") for name in ("rules", "model", "codec")]
    clock = feature / "product_ui_clock_model.c"
    fixtures = tests / "alarm/fixtures"
    fixture_manifest = json.loads((fixtures / "legacy_manifest.json").read_text(encoding="utf-8"))
    declarations = []
    for name, digest in fixture_manifest["fixtures"].items():
        payload = (fixtures / name).read_bytes()
        if hashlib.sha256(payload).hexdigest() != digest:
            raise RuntimeError("Frozen legacy fixture changed: " + name)
        declarations.append("static const unsigned char " + Path(name).stem + "[] = {" +
                            ",".join(str(value) for value in payload) + "};")
    (temporary / "alarm_legacy_fixtures.h").write_text("\n".join(declarations) + "\n", encoding="utf-8")
    gates = ['typedef struct { char left, right; unsigned incomplete, protected_record; } alarm_read_gate_t;',
             'static const alarm_read_gate_t alarm_read_gates[] = {']
    for left in ALARM_LOAD_CONTRACT['slot_kinds']:
        for right in ALARM_LOAD_CONTRACT['slot_kinds']:
            incomplete = any(k in ALARM_LOAD_CONTRACT['incomplete_kinds'] for k in (left,right))
            protected = any(k in ALARM_LOAD_CONTRACT['version_kinds'] for k in (left,right))
            gates.append(f"{{'{left}','{right}',{int(incomplete)}U,{int(protected)}U}},")
    gates.append('};')
    (temporary/'alarm_read_gates.h').write_text('\n'.join(gates),encoding='utf-8')
    suites = {"presets": [tests / 'alarm/product_ui_alarm_preset_tests.c', *models, clock, calendar,
        feature/'product_ui_clock.c', feature/'product_ui_alarm_service.c', feature/'product_ui_time_domain.c',
        dcode/'modules/applications/product_ui/shared/text/product_ui_text.c',
        host/'product_ui_host_alarm.c',host/'product_ui_host_os.c',
        dcode/'modules/applications/product_ui/adapters/gc9307/product_ui_alarm_gc9307.c']}
    report = {}
    for name, sources in suites.items():
        executable = output / (name + "_tests.exe")
        flags = [args.cc, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror", "-pthread",
                 "-I" + str(host), "-I" + str(dcode), "-I" + str(temporary),
                 "-I" + str(ROOT / "apps/common/example/third_party/lvgl_v9"),
                 "-DLV_CONF_PATH=" + (host / "lv_conf.h").as_posix()]
        if name in ("sdk-vm", "service", "commands", "presets"):
            flags += ["-DPRODUCT_UI_ALARM_HOST_SDK_VM=1", "-DD_BOARD_IS_GC9307=1", "-I" + str(ROOT / "include_lib/utils")]
        inputs = [*sources, *feature.glob("*.h"), *host.rglob("*.h"), *fixtures.glob("*"), dcode / "framework/d_error.h",
                  dcode / "modules/applications/product_ui/adapters/gc9307/product_ui_vm_gc9307.h",
                  dcode / "platform/storage/d_vm_status.h",
                  Path(__file__), Path(__file__).with_name("host_support.py"), ALARM_LOAD_CONTRACT_PATH]
        frozen = {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
        build = subprocess.run([*flags, *map(str, sources), "-o", str(executable)], cwd=ROOT,
                               env=environment, capture_output=True, text=True, timeout=120)
        (output / (name + "_compile.log")).write_text(build.stdout + build.stderr, encoding="utf-8")
        if build.returncode:
            raise RuntimeError(build.stdout + build.stderr)
        scenarios = PRESET_CASES
        results = []
        for scenario in scenarios:
            command = [str(executable)]
            if name in ("service", "commands", "presets"):
                storage_case = "fresh" if scenario == "restore" else scenario.removeprefix("restore-")
                directory = output / (name + "-vm-" + storage_case)
                directory.mkdir(exist_ok=True)
                prefix = directory / "alarms"
                if not scenario.startswith("restore"):
                    for slot in ("a", "b"):
                        (directory / ("alarms-" + slot + ".bin")).write_bytes(b"")
                command.extend([str(prefix), scenario])
            run = subprocess.run(command, cwd=output, env=environment, capture_output=True, text=True, timeout=35)
            (output / (name + "-" + scenario + ".log")).write_text(run.stdout + run.stderr, encoding="utf-8")
            if run.returncode:
                raise RuntimeError(name + "/" + scenario + "\n" + run.stdout + run.stderr)
            print("PASS alarm", scenario, flush=True)
            results.append({"scenario": scenario, "exit_code": run.returncode})
        if any(hashlib.sha256((ROOT / relative).read_bytes()).hexdigest() != digest for relative, digest in frozen.items()):
            raise RuntimeError("Inputs changed during " + name + "; repeat against a stable working tree")
        report[name] = {"result": "PASS", "scenarios": results, "sources": frozen, "compile_flags": flags}
        if name == 'presets' and args.negative_controls:
            owner = feature/'product_ui_alarm_service.c'
            original = owner.read_text(encoding='utf-8')
            controls = []
            for control in ALARM_LOAD_CONTRACT['negative_controls']:
                assert original.count(control['before']) == 1, control['id']
                stage = output/control['id'];stage.mkdir(exist_ok=True)
                mutated = stage/'alarm_service.c'
                mutated.write_text(original.replace(control['before'],control['after'],1),encoding='utf-8')
                negative_sources = [mutated if p==owner else p for p in sources]
                exe = stage/'test.exe'
                build = subprocess.run([*flags,'-I'+str(feature),*map(str,negative_sources),'-o',str(exe)],cwd=ROOT,env=environment,capture_output=True,timeout=120)
                (stage/'compile.log').write_bytes(build.stdout+build.stderr)
                if build.returncode:raise RuntimeError('Alarm negative compilation failed: '+str(stage))
                # 有效A槽与读取失败B槽的实际生产调用链，必须拒绝普通变更。
                run = subprocess.run([str(exe),str(stage/'alarms'),'slots-ei'],cwd=stage,env=environment,capture_output=True,timeout=35)
                (stage/'run.log').write_bytes(run.stdout+run.stderr)
                assert run.returncode != 0 and b'Assertion failed' in run.stderr, control['id']
                controls.append(dict(id=control['id'],negative_rejected=True,exit_code=run.returncode))
            report[name]['negative_controls'] = controls
    (output / "presets_result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("Scoped clock/model/service validation only; full sound, LVGL, ELF and device gates are separate.")


if __name__ == "__main__":
    main()
