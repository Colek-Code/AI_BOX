"""Shared compiler, Make and temporary-menu helpers."""

from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


D_CODE = Path(__file__).resolve().parents[2]
APP_CONFIG = D_CODE / "config" / "app_config.h"
BOARD_CONFIG = D_CODE / "config" / "board_config.h"
QWEN_CONFIG = D_CODE / "config" / "qwen_config.h"
OTA_CONFIG = D_CODE / "config" / "ota_config.h"
BUILD_TEMPLATE = D_CODE / "build" / "d_build_config.in"
BOARD_INCLUDE = D_CODE / "board" / "wl82"
APP_MAIN = D_CODE / "app" / "app_main.c"
APP_ROOT = D_CODE / "app"
SERVICE_STARTUP = APP_ROOT / "d_service_startup.c"
UI_STARTUP_HEADER = APP_ROOT / "d_ui_startup.h"
UI_STARTUP_SOURCE = APP_ROOT / "d_ui_startup.c"
EVENT_BUS_HEADER = D_CODE / "framework" / "d_event_bus.h"
EVENT_BUS_SOURCE = D_CODE / "framework" / "d_event_bus.c"
ERROR_HEADER = D_CODE / "framework" / "d_error.h"
UI_MODULE_HEADER = D_CODE / "framework" / "d_ui_module.h"
UI_MODULE_SOURCE = D_CODE / "framework" / "d_ui_module.c"
LVGL_HEADER = D_CODE / "framework" / "d_lvgl.h"
LVGL_SOURCE = D_CODE / "framework" / "d_lvgl.c"
AI_PRESENTER = D_CODE / "modules" / "applications" / "ai_chat" / "ai_presenter.c"
AI_SERVICE_HEADER = D_CODE / "modules" / "applications" / "ai_chat" / "ai_service.h"
MAKEFILE = BOARD_INCLUDE / "Makefile"
FIRMWARE_SOURCES = D_CODE / "build" / "d_firmware_sources.def"
PRODUCT_UI_ROOT = D_CODE / "modules" / "applications" / "product_ui"
POMODORO_PAGE_SOURCE = (
    PRODUCT_UI_ROOT / "features" / "time" / "product_ui_pomodoro_page.c"
)
DEVICE_VOLUME_HEADER = (
    D_CODE / "modules" / "services" / "audio" / "d_device_volume.h"
)
DEVICE_VOLUME_SOURCE = (
    D_CODE / "modules" / "services" / "audio" / "d_device_volume.c"
)
QWEN_SERVICE_SOURCE = (
    D_CODE / "modules" / "integrations" / "qwen" / "qwen_service.c"
)


def compiler_path() -> str:
    configured = os.environ.get("D_CODE_CLANG")
    candidates = [configured, "C:/JL/pi32/bin/clang.exe", shutil.which("clang")]
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return str(candidate)
    raise RuntimeError("未找到 clang；可通过 D_CODE_CLANG 指定预处理器")


def make_path() -> str:
    configured = os.environ.get("D_CODE_MAKE")
    candidates = [configured, shutil.which("mingw32-make"), shutil.which("make")]
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return str(candidate)
    raise RuntimeError("未找到 GNU Make；可通过 D_CODE_MAKE 指定")


def replace_define(text: str, name: str, value: str) -> str:
    pattern = re.compile(
        rf"^(#define\s+{re.escape(name)}\s+)[^\s/]+", re.MULTILINE
    )
    updated, count = pattern.subn(rf"\g<1>{value}", text, count=1)
    if count != 1:
        raise AssertionError(f"配置菜单缺少 {name}")
    return updated


def selector_config_path(name: str) -> str:
    """One owner map shared by fixtures, target builds and receipt audits."""
    return {"D_BOARD_TARGET": "config/board_config.h",
            "D_OTA_KEEP_VM": "config/ota_config.h",
            "D_OTA_BOOT_VERSION": "config/ota_config.h",
            "D_OTA_RELEASE_URL": "config/ota_config.h",
            "D_QWEN_AUTH_MODE": "config/qwen_config.h"}.get(name, "config/app_config.h")


def configured_source_bytes(content: bytes, relative: str, overrides: dict[str, str]) -> bytes:
    for name, value in overrides.items():
        if selector_config_path(name) == relative:
            text = content.decode("utf-8").replace("\r\n", "\n")
            content = replace_define(text, name, value).encode("utf-8")
    return content


def materialize_menu(directory: Path, overrides: dict[str, str]) -> None:
    # Production files use both SDK short names and owner-qualified config includes.
    # Both must resolve to the same test menu, otherwise syntax silently tests the live profile.
    qualified = directory / "config"
    qualified.mkdir(exist_ok=True)
    for source in (APP_CONFIG, BOARD_CONFIG, QWEN_CONFIG, OTA_CONFIG):
        relative = source.relative_to(D_CODE).as_posix()
        content = configured_source_bytes(source.read_bytes(), relative, overrides)
        (directory / source.name).write_bytes(content)
        (qualified / source.name).write_bytes(content)


def preprocess(overrides: dict[str, str]) -> subprocess.CompletedProcess[str]:
    with tempfile.TemporaryDirectory(prefix="d_code_config_") as temporary:
        include_dir = Path(temporary)
        materialize_menu(include_dir, overrides)
        command = [
            compiler_path(),
            "-E",
            "-P",
            "-x",
            "c",
            f"-I{include_dir}",
            f"-I{D_CODE}",
            f"-I{D_CODE / 'config'}",
            f"-I{BOARD_INCLUDE}",
            str(BUILD_TEMPLATE),
        ]
        return subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )


def resolved_values(output: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in output.splitlines():
        match = re.fullmatch(r"(D_BUILD_[A-Z0-9_]+)\s*:=\s*(\S+)", line.strip())
        if match:
            values[match.group(1)] = match.group(2)
    return values


def resolved_sources(output: str) -> list[str]:
    sources: list[str] = []
    pattern = re.compile(r'^D_BUILD_D_CODE_SOURCES\s*\+=\s*"([^"]+)"$')
    for line in output.splitlines():
        match = pattern.fullmatch(line.strip())
        if match:
            sources.append(match.group(1))
    return sources


def decode_clang_unicode_escapes(text: str) -> str:
    return re.sub(
        r"<U\+([0-9A-Fa-f]{4,6})>",
        lambda match: chr(int(match.group(1), 16)),
        text,
    )


def expect_valid(
    name: str,
    overrides: dict[str, str],
    expected: dict[str, str],
    present_sources: tuple[str, ...] = (),
    absent_sources: tuple[str, ...] = (),
) -> None:
    result = preprocess(overrides)
    if result.returncode != 0:
        raise AssertionError(f"{name} 应通过，但预处理失败：\n{result.stderr}")
    actual = resolved_values(result.stdout)
    for key, value in expected.items():
        if actual.get(key) != value:
            raise AssertionError(
                f"{name}: {key} 期望 {value}，实际 {actual.get(key)!r}"
            )
    sources = resolved_sources(result.stdout)
    if len(sources) != len(set(sources)):
        duplicates = sorted({source for source in sources if sources.count(source) > 1})
        raise AssertionError(f"{name}: 源文件清单存在重复项 {duplicates}")
    missing_files = [source for source in sources if not (D_CODE / source).is_file()]
    if missing_files:
        raise AssertionError(f"{name}: 源文件清单引用不存在的文件 {missing_files}")
    for source in present_sources:
        if source not in sources:
            raise AssertionError(f"{name}: 应包含源文件 {source}")
    for source in absent_sources:
        if source in sources:
            raise AssertionError(f"{name}: 不应包含源文件 {source}")


def expect_invalid(name: str, overrides: dict[str, str], message: str) -> None:
    result = preprocess(overrides)
    evidence = decode_clang_unicode_escapes(result.stdout + result.stderr)
    if result.returncode == 0:
        raise AssertionError(f"{name} 应被配置校验拒绝")
    if message not in evidence:
        raise AssertionError(f"{name} 缺少预期中文错误：{message}\n{evidence}")


def disabled_services() -> dict[str, str]:
    return {
        "D_FEATURE_KEY_SERVICE": "0",
        "D_FEATURE_POWER_CONTROL": "0",
        "D_FEATURE_WIFI": "0",
        "D_FEATURE_BLE_PROVISION": "0",
        "D_FEATURE_WEBSOCKET": "0",
        "D_FEATURE_CAMERA": "0",
        "D_FEATURE_QWEN": "0",
        "D_FEATURE_XIAOLE_WAKEUP": "0",
        "D_FEATURE_OTA": "0",
    }


def error_code(name: str) -> int:
    text = ERROR_HEADER.read_text(encoding="utf-8")
    match = re.search(rf"\b{re.escape(name)}\s*=\s*(-?\d+)", text)
    if match is None:
        raise AssertionError(f"d_error.h 缺少可建模的错误码 {name}")
    return int(match.group(1))
