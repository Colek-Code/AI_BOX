"""JieLi compiler syntax probes for every supported board/root-UI pair."""

from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor, as_completed
from contextlib import ExitStack
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

from .support import (
    BOARD_INCLUDE,
    D_CODE,
    disabled_services,
    make_path,
    materialize_menu,
    preprocess,
    resolved_sources,
)


COMMON_SOURCES = (
    "app/app_main.c",
    "app/d_service_startup.c",
    "app/d_ui_startup.c",
    "framework/d_lvgl.c",
    "framework/d_ui_font.c",
    "framework/d_ui_module.c",
    "framework/d_page_manager.c",
    "platform/screen/transition/d_page_transition.c",
)

SYNTAX_CASES = (
    ("GC Product UI", {}, "modules/applications/product_ui/product_ui.c"),
    (
        "GC TabletShell",
        {**disabled_services(), "D_UI_TARGET": "D_UI_TABLET_SHELL"},
        "modules/diagnostics/display/tablet_shell/tablet_shell.c",
    ),
    (
        "GC ScreenTest",
        {**disabled_services(), "D_UI_TARGET": "D_UI_SCREEN_TEST"},
        "modules/diagnostics/display/screen_test/screen_test_page.c",
    ),
    (
        "EMI CipherPanel",
        {
            "D_FEATURE_OTA": "0",
            "D_BOARD_TARGET": "D_BOARD_COPY_EMI_ILI948X",
            "D_UI_TARGET": "D_UI_CIPHER_PANEL",
            "D_FEATURE_CAMERA": "0",
        },
        "modules/applications/legacy_ui/cipher_panel/cipher_panel_demo.c",
    ),
    (
        "EMI LauncherDemo",
        {
            "D_FEATURE_OTA": "0",
            "D_BOARD_TARGET": "D_BOARD_COPY_EMI_ILI948X",
            "D_UI_TARGET": "D_UI_LAUNCHER_DEMO",
            "D_FEATURE_CAMERA": "0",
        },
        "modules/diagnostics/display/launcher_demo/launcher_demo.c",
    ),
    (
        "EMI LightSpotDemo",
        {
            "D_FEATURE_OTA": "0",
            "D_BOARD_TARGET": "D_BOARD_COPY_EMI_ILI948X",
            "D_UI_TARGET": "D_UI_LIGHT_SPOT_DEMO",
            "D_FEATURE_CAMERA": "0",
        },
        "modules/diagnostics/display/light_spot_demo/light_spot_demo.c",
    ),
)

AI_WEBSOCKET_OVERRIDES = {
    "D_FEATURE_OTA": "0",
    "D_BOARD_TARGET": "D_BOARD_COPY_EMI_ILI948X",
    "D_UI_TARGET": "D_UI_CIPHER_PANEL",
    "D_FEATURE_WEBSOCKET": "1",
    "D_FEATURE_AI_CHAT": "1",
    "D_FEATURE_QWEN": "0",
    "D_FEATURE_XIAOLE_WAKEUP": "0",
}

LEGACY_CAMERA_GALLERY_OVERRIDES = {
    "D_FEATURE_OTA": "0",
    "D_BOARD_TARGET": "D_BOARD_COPY_EMI_ILI948X",
    "D_UI_TARGET": "D_UI_CIPHER_PANEL",
    "D_FEATURE_CAMERA": "1",
    "D_FEATURE_GALLERY": "1",
    "D_FEATURE_QWEN": "0",
    "D_FEATURE_XIAOLE_WAKEUP": "0",
}

SYNTAX_BATCH_SIZE = 4
SYNTAX_MAX_WORKERS = max(1, min(4, os.cpu_count() or 1))
DRYRUN_TIMEOUT_SECONDS = 60
SYNTAX_BATCH_TIMEOUT_SECONDS = 180
APP_MAIN_OBJECT_TARGET = (
    "../../build/out/wl82/obj/apps/D_Code/app/app_main.c.o"
)


def dryrun_compile_tokens(menu: Path) -> list[str]:
    resolved_config = menu / "d_resolved_config.mk"
    config_includes = " ".join(
        f"-I{path.as_posix()}"
        for path in (menu, D_CODE / "config", D_CODE / "build", D_CODE, BOARD_INCLUDE)
    )
    try:
        result = subprocess.run(
            [
                make_path(),
                "-n",
                "-B",
                "VERBOSE=1",
                f"RESOLVED_CONFIG_MK={resolved_config.as_posix()}",
                f"APP_CONFIG_FILE={(menu / 'app_config.h').as_posix()}",
                f"BOARD_CONFIG_FILE={(menu / 'board_config.h').as_posix()}",
                f"CONFIG_CPP_INCLUDES={config_includes}",
                APP_MAIN_OBJECT_TARGET,
            ],
            cwd=BOARD_INCLUDE,
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=DRYRUN_TIMEOUT_SECONDS,
        )
    except subprocess.TimeoutExpired as error:
        raise AssertionError(
            f"Make dry-run timed out after {DRYRUN_TIMEOUT_SECONDS}s"
        ) from error
    if result.returncode != 0:
        raise AssertionError(f"Make dry-run failed:\n{result.stderr}")
    for line in result.stdout.splitlines():
        if " -c " in line and "apps/D_Code/app/app_main.c" in line:
            tokens = shlex.split(line, posix=True)
            if "-MP" not in tokens or not any(token.endswith(".d.v2") for token in tokens):
                raise AssertionError("dry-run compile command lacks -MP/.d.v2")
            if any(
                old in token
                for token in tokens
                for old in ("apps/D_Code/include", "apps/D_Code/app", "apps/D_Code/ui")
                if token.startswith("-I")
            ):
                raise AssertionError("dry-run compile command retains an obsolete include root")
            return tokens
    raise AssertionError("Make dry-run did not expose the app_main compile command")


def syntax_command(base: list[str], menu: Path, sources: list[str]) -> list[str]:
    command = [base[0], "-fsyntax-only", f"-I{menu}"]
    index = 1
    while index < len(base):
        token = base[index]
        if token in ("-MMD", "-MP"):
            index += 1
            continue
        if token in ("-MF", "-c", "-o"):
            index += 2
            continue
        command.append(token)
        index += 1
    command.extend(str(D_CODE / source) for source in sources)
    return command


def run_source_batch(base: list[str], menu: Path, case_name: str,
                     sources: list[str]) -> None:
    try:
        result = subprocess.run(
            syntax_command(base, menu, sources),
            cwd=BOARD_INCLUDE,
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=SYNTAX_BATCH_TIMEOUT_SECONDS,
        )
    except subprocess.TimeoutExpired as error:
        raise AssertionError(
            f"{case_name}: JieLi syntax probe timed out after "
            f"{SYNTAX_BATCH_TIMEOUT_SECONDS}s for {sources}"
        ) from error
    if result.returncode != 0:
        raise AssertionError(
            f"{case_name}: JieLi syntax probe failed for {sources}:\n"
            f"{result.stderr[-12000:]}"
        )


def run_syntax_cases(*, verbose: bool = True) -> None:
    jobs: list[tuple[list[str], Path, str, list[str]]] = []
    with ExitStack() as stack:
        for case_name, overrides, root_source in SYNTAX_CASES:
            temporary = stack.enter_context(
                tempfile.TemporaryDirectory(prefix="d_code_syntax_")
            )
            menu = Path(temporary)
            materialize_menu(menu, overrides)
            config_result = preprocess(overrides)
            if config_result.returncode != 0:
                raise AssertionError(
                    f"{case_name}: cannot resolve syntax source set:\n"
                    f"{config_result.stderr}"
                )
            sources = resolved_sources(config_result.stdout)
            if root_source not in sources:
                raise AssertionError(f"{case_name}: root source is not selected")
            for source in COMMON_SOURCES:
                if source not in sources:
                    raise AssertionError(f"{case_name}: common source missing {source}")
            base = dryrun_compile_tokens(menu)
            for offset in range(0, len(sources), SYNTAX_BATCH_SIZE):
                jobs.append(
                    (
                        base,
                        menu,
                        case_name,
                        sources[offset:offset + SYNTAX_BATCH_SIZE],
                    )
                )

        temporary = stack.enter_context(
            tempfile.TemporaryDirectory(prefix="d_code_syntax_ai_ws_")
        )
        menu = Path(temporary)
        materialize_menu(menu, AI_WEBSOCKET_OVERRIDES)
        base = dryrun_compile_tokens(menu)
        jobs.append(
            (
                base,
                menu,
                "AI Chat + WebSocket",
                [
                    "app/d_service_startup.c",
                    "modules/applications/ai_chat/ai_presenter.c",
                    "modules/applications/ai_chat/ai_service.c",
                ],
            )
        )

        temporary = stack.enter_context(
            tempfile.TemporaryDirectory(prefix="d_code_syntax_legacy_media_")
        )
        menu = Path(temporary)
        materialize_menu(menu, LEGACY_CAMERA_GALLERY_OVERRIDES)
        config_result = preprocess(LEGACY_CAMERA_GALLERY_OVERRIDES)
        if config_result.returncode != 0:
            raise AssertionError(
                "Legacy Camera + Gallery: cannot resolve syntax source set:\n"
                f"{config_result.stderr}"
            )
        sources = resolved_sources(config_result.stdout)
        legacy_media_sources = [
            "app/d_ui_startup.c",
            "modules/applications/camera/d_camera_page.c",
            "modules/applications/legacy_ui/gallery/d_gallery_page.c",
        ]
        missing = [source for source in legacy_media_sources if source not in sources]
        if missing:
            raise AssertionError(
                f"Legacy Camera + Gallery: selected source set misses {missing}"
            )
        base = dryrun_compile_tokens(menu)
        jobs.append((base, menu, "Legacy Camera + Gallery", legacy_media_sources))

        worker_count = min(SYNTAX_MAX_WORKERS, len(jobs))
        with ThreadPoolExecutor(max_workers=worker_count) as executor:
            pending = {
                executor.submit(run_source_batch, *job): (job[2], job[3])
                for job in jobs
            }
            total = len(pending)
            failures: list[str] = []
            for completed, future in enumerate(as_completed(pending), start=1):
                case_name, sources = pending[future]
                try:
                    future.result()
                except Exception as error:
                    failures.append(str(error))
                    outcome = "FAIL"
                else:
                    outcome = "PASS"
                if verbose:
                    print(
                        f"[syntax {completed}/{total}] {outcome} {case_name}: "
                        f"{', '.join(sources)}",
                        file=sys.stderr,
                        flush=True,
                    )
            if failures:
                raise AssertionError(
                    "JieLi syntax matrix failures:\n\n" + "\n\n".join(failures)
                )
