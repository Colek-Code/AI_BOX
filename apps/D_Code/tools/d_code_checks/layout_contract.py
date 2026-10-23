"""D_Code owner layout, Make dependency and public-header contracts."""

from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

from .support import (
    APP_CONFIG,
    BOARD_CONFIG,
    D_CODE,
    MAKEFILE,
    PRODUCT_UI_ROOT,
    FIRMWARE_SOURCES,
    compiler_path,
    make_path,
)


PUBLIC_HEADERS = (
    "app/d_service_startup.h",
    "app/d_ui_startup.h",
    "framework/d_lvgl.h",
    "framework/d_ui_font.h",
    "framework/d_ui_module.h",
    "framework/d_page_types.h",
    "framework/d_page_manager.h",
    "platform/screen/transition/d_page_transition.h",
    "framework/d_event_payloads.h",
    "framework/compat/d_stdio_compat.h",
    "framework/d_lvgl_port.h",
    "platform/screen/d_display.h",
    "platform/screen/lcd/d_lcd_compat.h",
    "platform/pwm/d_pwm.h",
    "platform/screen/profiles/gc9307_hyn/d_screen_gc9307_hyn.h",
    "platform/storage/d_storage.h",
    "platform/screen/power/d_screen_power.h",
    "modules/services/audio/d_device_volume.h",
    "modules/services/audio/d_audio_shared_stop.h",
    "modules/services/ota/ota_service.h",
    "modules/services/ble_provisioning/d_ble_net_cfg.h",
)

MODULE_OWNER_ROOTS = {"applications", "services", "integrations", "diagnostics"}

def find_git_index_case_mismatches(
    actual_paths: set[str], indexed_paths: set[str]
) -> list[tuple[str, tuple[str, ...]]]:
    """Return working-tree paths whose Git index spelling differs by case."""
    indexed_by_case: dict[str, list[str]] = {}
    for path in indexed_paths:
        indexed_by_case.setdefault(path.casefold(), []).append(path)

    mismatches: list[tuple[str, tuple[str, ...]]] = []
    for actual_path in sorted(actual_paths):
        indexed_candidates = indexed_by_case.get(actual_path.casefold(), [])
        if indexed_candidates and actual_path not in indexed_candidates:
            mismatches.append((actual_path, tuple(sorted(indexed_candidates))))
    return mismatches


def find_git_index_untracked_paths(
    actual_paths: set[str], indexed_paths: set[str]
) -> list[str]:
    """Return paths that have no exact or case-insensitive Git index entry."""
    indexed_casefolds = {path.casefold() for path in indexed_paths}
    return sorted(
        path for path in actual_paths if path.casefold() not in indexed_casefolds
    )


def load_git_index_paths() -> set[str]:
    """Read the exact path spelling stored by Git for D_Code."""
    repo_root = D_CODE.parents[1]
    result = subprocess.run(
        ["git", "ls-files", "-z", "--", "apps/D_Code"],
        cwd=repo_root,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if result.returncode != 0:
        raise AssertionError(f"git ls-files failed: {result.stderr}")
    return {path for path in result.stdout.split("\0") if path}


def to_repo_d_code_paths(actual_paths: set[str]) -> set[str]:
    """Convert D_Code-relative paths to repository-relative POSIX paths."""
    repo_root = D_CODE.parents[1]
    return {
        (D_CODE / path).relative_to(repo_root).as_posix() for path in actual_paths
    }


def check_git_index_path_case(actual_paths: set[str]) -> None:
    """Require existing D_Code path spelling to match the Git index."""
    indexed_paths = load_git_index_paths()
    repo_actual_paths = to_repo_d_code_paths(actual_paths)
    mismatches = find_git_index_case_mismatches(repo_actual_paths, indexed_paths)
    if mismatches:
        details = [
            f"{actual} <- index {', '.join(indexed)}"
            for actual, indexed in mismatches
        ]
        raise AssertionError(f"Git index path case mismatch: {details}")


def check_git_index_contract_paths(actual_paths: set[str], label: str) -> None:
    """Require recoverable Git entries for a bounded set of contract paths."""
    indexed_paths = load_git_index_paths()
    repo_actual_paths = to_repo_d_code_paths(actual_paths)
    untracked = find_git_index_untracked_paths(repo_actual_paths, indexed_paths)
    mismatches = find_git_index_case_mismatches(repo_actual_paths, indexed_paths)
    findings: list[str] = []
    if untracked:
        findings.append(f"not indexed: {untracked}")
    if mismatches:
        mismatch_details = [
            f"{actual} <- index {', '.join(indexed)}"
            for actual, indexed in mismatches
        ]
        findings.append(f"case mismatch: {mismatch_details}")
    if findings:
        raise AssertionError(f"Git index contract failed for {label}: {findings}")


def check_git_index_helper_contract() -> None:
    """Keep index membership and case-spelling failures distinct."""
    indexed = {
        "apps/D_Code/exact.c",
        "apps/D_Code/CaseSensitive.c",
    }
    if find_git_index_untracked_paths({"apps/D_Code/exact.c"}, indexed):
        raise AssertionError("exact Git index path was reported as untracked")
    if find_git_index_untracked_paths({"apps/D_Code/casesensitive.c"}, indexed):
        raise AssertionError("case mismatch was incorrectly reported as untracked")
    if find_git_index_untracked_paths({"apps/D_Code/new_owner.c"}, indexed) != [
        "apps/D_Code/new_owner.c"
    ]:
        raise AssertionError("untracked Git index path was not detected")
    expected_mismatch = [
        ("apps/D_Code/casesensitive.c", ("apps/D_Code/CaseSensitive.c",))
    ]
    if (
        find_git_index_case_mismatches(
            {"apps/D_Code/casesensitive.c"}, indexed
        )
        != expected_mismatch
    ):
        raise AssertionError("Git index case mismatch was not detected")


def check_public_surface() -> None:
    app_text = APP_CONFIG.read_text(encoding="utf-8")
    board_text = BOARD_CONFIG.read_text(encoding="utf-8")
    public_app = app_text.split("内部配置解析区", maxsplit=1)[0]
    for token in ("D_SCREEN_PROFILE_", "D_SCREEN_BRINGUP_", "TCFG_"):
        if token in public_app or token in board_text:
            raise AssertionError(f"公开配置菜单不应出现内部标识 {token}")
    if re.search(r"\bCONFIG_[A-Z0-9_]", public_app + board_text):
        raise AssertionError("公开配置菜单不应出现 SDK CONFIG_* 宏")
    for match in re.finditer(
        r"^#define\s+(D_FEATURE_[A-Z0-9_]+)\s+(\S+)",
        public_app,
        re.MULTILINE,
    ):
        if match.group(2) not in {"0", "1"}:
            raise AssertionError(f"{match.group(1)} 必须直接填写 0 或 1")


def make_targets_are_current(directory: Path) -> bool:
    result = subprocess.run(
        [make_path(), "-f", "Makefile", "-q", "config_a.o", "config_b.o"],
        cwd=directory,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if result.returncode not in (0, 1):
        raise AssertionError(f"GNU Make 配置依赖自测失败：\n{result.stdout}{result.stderr}")
    return result.returncode == 0


def rebuild_make_targets(directory: Path) -> None:
    result = subprocess.run(
        [make_path(), "-f", "Makefile", "config_a.o", "config_b.o"],
        cwd=directory,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if result.returncode != 0:
        raise AssertionError(f"GNU Make 配置依赖重建失败：\n{result.stdout}{result.stderr}")


def check_incremental_config_dependency(makefile: str) -> None:
    dependency = re.search(
        r"^\$\(OBJS\)\s*:\s*\$\(RESOLVED_CONFIG_MK\)\s*$",
        makefile,
        re.MULTILINE,
    )
    if dependency is None:
        raise AssertionError("全部对象必须依赖解析后的配置文件")

    with tempfile.TemporaryDirectory(prefix="d_code_make_dependency_") as temporary:
        directory = Path(temporary)
        resolved = directory / "resolved.mk"
        objects = [directory / "config_a.o", directory / "config_b.o"]
        fixture = (
            f"PYTHON := {Path(sys.executable).as_posix()}\n"
            "RESOLVED_CONFIG_MK := resolved.mk\n"
            "OBJS := config_a.o config_b.o\n"
            f"{dependency.group(0)}\n"
            '\t@"$(PYTHON)" -c "import shutil,sys; '
            'shutil.copyfile(sys.argv[1], sys.argv[2])" $< $@\n'
        )
        (directory / "Makefile").write_text(fixture, encoding="utf-8")
        base_time = time.time() - 100.0
        resolved.write_text("PROFILE=A\n", encoding="utf-8")
        os.utime(resolved, (base_time, base_time))
        for obj in objects:
            obj.write_text("A\n", encoding="utf-8")
            os.utime(obj, (base_time + 10.0, base_time + 10.0))
        if not make_targets_are_current(directory):
            raise AssertionError("A 配置初始对象应处于最新状态")
        for profile, stamp in (("B", 20.0), ("A", 40.0)):
            resolved.write_text(f"PROFILE={profile}\n", encoding="utf-8")
            os.utime(resolved, (base_time + stamp, base_time + stamp))
            if make_targets_are_current(directory):
                raise AssertionError(f"切换到 {profile} 后旧对象未过期")
            rebuild_make_targets(directory)
            expected = f"PROFILE={profile}\n"
            if any(obj.read_text(encoding="utf-8") != expected for obj in objects):
                raise AssertionError(f"对象没有使用 {profile} 配置重建")
            for obj in objects:
                os.utime(obj, (base_time + stamp + 10.0, base_time + stamp + 10.0))


def check_cloud_config_dependency(makefile: str) -> None:
    rule = re.search(r"^\$\(RESOLVED_CONFIG_MK\):.*?(?=^\t[^\t])", makefile, re.M | re.S)
    if (rule is None or "$(CLOUD_RESOLVE_FILE)" not in rule.group(0) or
            "CLOUD_RESOLVE_FILE := $(MAKEFILE_DIR)../../config/internal/d_cloud_resolve.h" not in makefile):
        raise AssertionError("托管模式解析头必须使 Make 配置缓存过期")
    with tempfile.TemporaryDirectory(prefix="d_code_cloud_dependency_") as folder:
        directory = Path(folder)
        header, resolved = directory / "cloud.h", directory / "resolved.mk"
        objects = [directory / "config_a.o", directory / "config_b.o"]
        copy = '\t@"$(PYTHON)" -c "import shutil,sys; shutil.copyfile(sys.argv[1],sys.argv[2])" '
        fixture = (f"PYTHON := {Path(sys.executable).as_posix()}\n"
                   "RESOLVED_CONFIG_MK := resolved.mk\nCLOUD_RESOLVE_FILE := cloud.h\n"
                   "OBJS := config_a.o config_b.o\n" + rule.group(0) +
                   copy + "$(CLOUD_RESOLVE_FILE) $@\n"
                   "$(OBJS): $(RESOLVED_CONFIG_MK)\n" + copy + "$< $@\n")
        (directory / "Makefile").write_text(fixture, encoding="utf-8")
        memory = directory / "d_board_memory_config.h"; memory.write_text("", encoding="utf-8")
        stamp = time.time() - 100.0
        for path, offset in ((memory, 0), (header, 0), (resolved, 10), *( (p, 20) for p in objects )):
            path.write_text("FULL\n", encoding="utf-8"); os.utime(path, (stamp + offset, stamp + offset))
        if not make_targets_are_current(directory): raise AssertionError("初始托管配置不应过期")
        for label, offset in (("SEMI", 30), ("FULL", 60)):
            header.write_text(label + "\n", encoding="utf-8")
            os.utime(header, (stamp + offset, stamp + offset))
            if make_targets_are_current(directory): raise AssertionError("解析头变更未使配置与对象过期")
            rebuild_make_targets(directory)
            if any(path.read_text(encoding="utf-8") != label + "\n" for path in (resolved, *objects)):
                raise AssertionError("托管模式依赖未传播到全部对象")
            for path in (resolved, *objects): os.utime(path, (stamp + offset + 10, stamp + offset + 10))


def check_make_contract(makefile: str) -> None:
    default_goal = re.search(r"^\.DEFAULT_GOAL\s*:?=\s*([^\s#]+)", makefile, re.MULTILINE)
    if default_goal is None or default_goal.group(1) != "all":
        raise AssertionError("Makefile 默认目标必须保持 SDK 的 build-and-download all 入口")
    if not re.search(r"^help:\s*$", makefile, re.MULTILINE):
        raise AssertionError("Makefile 必须保留显式 help 目标")
    if not re.search(r"^firmware_elf:\s+pre_build\s+\$\(OUT_ELF\)\s*$", makefile, re.MULTILINE):
        raise AssertionError("Makefile 缺少纯 ELF firmware_elf 目标")
    all_target = re.search(
        r"^all:\s+firmware_elf\s*$\n(?P<recipe>(?:\t[^\n]*\n)+)",
        makefile,
        re.MULTILINE,
    )
    if all_target is None or "$(RUN_POST_SCRIPT) sdk" not in all_target.group("recipe"):
        raise AssertionError("Makefile all 必须在纯 ELF 完成后执行 SDK post-build/download")
    if makefile.count("$(RUN_POST_SCRIPT) sdk") != 1:
        raise AssertionError("SDK post-build/download 必须只由 all 目标执行一次")
    check_incremental_config_dependency(makefile)
    check_cloud_config_dependency(makefile)
    for token in (
        "-MMD -MP",
        ".d.v2",
        "config/internal/d_board_resolve.h",
        "d_board_memory_config.h",
    ):
        if token not in makefile:
            raise AssertionError(f"Make dependency contract missing {token}")
    if not re.search(
        r"^BUILD_DIR\s*\?=\s*\.\./\.\./build/out/wl82/obj\s*$",
        makefile,
        re.MULTILINE,
    ):
        raise AssertionError("D_Code build output must live under build/out/wl82")
    if "LEGACY_BUILD_DIRS ?= obj objs" not in makefile:
        raise AssertionError("Make clean must retain explicit legacy obj/objs cleanup")
    for forbidden in ("apps/D_Code/include", "-I../../../../apps/D_Code/app \\"):
        if forbidden in makefile:
            raise AssertionError(f"obsolete broad include path remains: {forbidden}")
    d_code_owner_includes = [
        line.strip()
        for line in makefile.splitlines()
        if "-I../../../../apps/D_Code/" in line
        and "/sdk" not in line
        and "-I../../../../apps/D_Code/config " not in line
    ]
    if d_code_owner_includes:
        raise AssertionError(
            "D_Code owner-specific include roots remain: "
            f"{d_code_owner_includes}"
        )
    app_config_include = "-I../../../../apps/D_Code/config \\\n"
    if makefile.count(app_config_include) != 1:
        raise AssertionError(
            "Makefile 必须保留唯一的 D_Code app_config.h include root"
        )
    app_config_pos = makefile.find(app_config_include)
    demo_include_positions = [
        makefile.find("-I../../../../apps/demo/demo_audio/include"),
        makefile.find("-I../../../../apps/demo/demo_DevKitBoard/include"),
    ]
    if any(pos < 0 or app_config_pos > pos for pos in demo_include_positions):
        raise AssertionError(
            "D_Code app_config.h include root 必须位于所有 demo include root 之前"
        )
    if not re.search(
        r"ifeq \(\$\(OS\),Windows_NT\)\s+(?:#[^\n]*\n)*"
        r"LINK_AT \?= 1\s+else ifeq \(\$\(VENDOR_ROOT\),\$\(ROOT_PREFIX\)\)\s+"
        r"LINK_AT \?= 0\s+else\s+(?:#[^\n]*\n)*"
        r"LINK_AT \?= 1\s+endif",
        makefile,
        re.MULTILINE,
    ):
        raise AssertionError(
            "原位与隔离 VENDOR_ROOT 构建均须默认使用响应文件，避免 Windows 命令行溢出"
        )
    audio_group = makefile.find("D_AUDIO_LIBS :=")
    audio_gate = makefile.find("ifeq ($(D_BUILD_AUDIO),1)", audio_group)
    audio_use = makefile.find("$(D_AUDIO_LIBS)", audio_gate)
    if min(audio_group, audio_gate, audio_use) < 0:
        raise AssertionError("Makefile 缺少 D_BUILD_AUDIO 音频库边界")
    for library in ("audio_server.a", "lib_mp3_dec.a", "media_app.a", "ai_server.a"):
        if makefile.count(library) != 1:
            raise AssertionError(f"{library} 必须只归属于 D_AUDIO_LIBS")


def check_paths_and_dead_surfaces(*, check_index: bool = True) -> None:
    required = (
        "build/d_build_config.in",
        "build/d_firmware_sources.def",
        "config/app_config.h",
        "config/board_config.h",
        "config/deployment_config.h",
        "config/qwen_config.h",
        "config/runtime_config.h",
        "config/internal/d_board_resolve.h",
        "config/internal/d_feature_resolve.h",
        "app/d_service_startup.c",
        "app/d_ui_startup.c",
        "board/wl82/d_board.c",
        "board/wl82/d_board_memory_config.h",
        "framework/d_lvgl.c",
        "framework/d_ui_font.c",
        "framework/d_ui_module.c",
        "framework/d_page_types.h",
        "platform/screen/transition/d_page_transition.c",
        "platform/screen/lcd/d_lcd_compat.c",
        "platform/screen/d_display.h",
        "platform/pwm/d_pwm.c",
        "platform/storage/d_storage.c",
        "platform/screen/power/d_screen_power.c",
        "modules/applications/legacy_ui/cipher_panel/cipher_panel_demo.c",
        "modules/applications/product_ui/features/time/product_ui_pomodoro_page.c",
        "modules/applications/product_ui/shell/app_registry/product_ui_app_registry.c",
        "modules/applications/product_ui/runtime/service_registry/product_ui_service_registry.c",
        "modules/diagnostics/network/tcp_client/d_tcp_client.c",
        "modules/integrations/xiaole_wakeup/sdk/liblx_aec.a",
    )
    missing = [path for path in required if not (D_CODE / path).is_file()]
    if missing:
        raise AssertionError(f"new owner paths missing: {missing}")
    if check_index:
        check_git_index_contract_paths(set(required), "required owner paths")
    obsolete = (
        "app/d_source_manifest.h",
        "app/startup/d_service_manifest.c",
        "app/startup/d_service_manifest.h",
        "app/startup/d_ui_manifest.c",
        "app/startup/d_ui_manifest.h",
        "app/startup/d_ui_bootstrap.c",
        "app/startup/d_ui_bootstrap.h",
        "build/d_source_manifest.h",
        "config/d_app_config.h",
        "config/d_advanced_config.h",
        "config/d_module_config.h",
        "framework/D_lvgl.c",
        "framework/D_module.c",
        "framework/D_page_manager.c",
        "framework/d_module.c",
        "framework/d_module.h",
        "board/wl82/D_board.c",
        "board/wl82/board_D_Code_cfg.h",
        "lcd/lcd_drive.c",
        "lcd/lcd_drive.h",
        "lcd/lcd_config.h",
        "platform/screen/lcd/d_lcd_config.h",
        "lcd/drivers/gc9307/lcd_gc9307.c",
        "lcd/drivers/gc9307/lcd_gc9307.h",
        "lcd/drivers/ili948x/lcd_ili9481.c",
        "lcd/drivers/ili948x/lcd_ili9488.c",
        "test/D_tcp_client.c",
        "test/D_pwm.c",
        "tests/firmware/pwm_breath_demo.c",
        "ui/ui.c",
        "ui/ui.h",
        "modules/main_page/core/D_main_page.c",
        "modules/main_page/cipher_panel/cipher_panel_demo.c",
        "modules/gallery/D_gallery_page.c",
        "modules/applications/product_ui/apps/calculator/product_ui_calculator_page.c",
        "modules/applications/product_ui/core/product_ui_internal.h",
        "modules/applications/product_ui/services/product_ui_services.c",
        "modules/applications/product_ui/pages/product_ui_scene.c",
        "platform/bsp/lcd/bsp_lcd.h",
        "platform/bsp/pwm/bsp_pwm.c",
        "platform/services/storage/d_storage.c",
        "platform/services/screen_lock/d_screen_power.c",
    )
    actual_paths = {
        path.relative_to(D_CODE).as_posix()
        for path in D_CODE.rglob("*")
        if path.is_file()
    }
    if check_index:
        check_git_index_path_case(actual_paths)
    present = [path for path in obsolete if path in actual_paths]
    if present:
        raise AssertionError(f"obsolete owner paths remain: {present}")
    legacy_dirs_present = [
        name
        for name in ("include", "ui", "textboard", "lcd", "test")
        if (D_CODE / name).exists()
    ]
    if legacy_dirs_present:
        raise AssertionError(
            f"legacy top-level directories remain: {legacy_dirs_present}"
        )

    modules_root = D_CODE / "modules"
    module_roots = {
        path.name for path in modules_root.iterdir() if path.is_dir()
    }
    layout_findings: list[str] = []
    if module_roots != MODULE_OWNER_ROOTS:
        layout_findings.append(
            "modules/ must expose only applications/services/integrations/"
            f"diagnostics, got {sorted(module_roots)}"
        )
    module_root_files = [path.name for path in modules_root.iterdir() if path.is_file()]
    if module_root_files:
        layout_findings.append(
            f"modules/ root contains ownerless files: {module_root_files}"
        )

    navigation_noise = (
        "app/startup",
        "config/parameters",
        "platform/bsp",
        "platform/display",
        "platform/screen_lock",
        "platform/screen_profiles",
        "platform/services",
        "modules/applications/product_ui/applications",
        "modules/applications/product_ui/components",
        "modules/applications/product_ui/core",
        "modules/applications/product_ui/design",
        "modules/applications/product_ui/pages",
        "modules/applications/product_ui/services",
        "modules/applications/product_ui/verification",
        "board/wl82/obj",
        "board/wl82/objs",
    )
    present_noise = [path for path in navigation_noise if (D_CODE / path).exists()]
    if present_noise:
        layout_findings.append(
            f"obsolete navigation directories remain: {present_noise}"
        )
    pycache_dirs = sorted(
        path.relative_to(D_CODE).as_posix()
        for path in D_CODE.rglob("__pycache__")
        if path.is_dir()
    )
    if pycache_dirs:
        layout_findings.append(f"Python cache directories remain: {pycache_dirs}")
    if layout_findings:
        raise AssertionError("; ".join(layout_findings))

    check_cross_owner_includes()
    check_product_ui_registry_contract()

    checker_files = sorted((D_CODE / "tools" / "d_code_checks").glob("*.py"))
    if len(checker_files) < 6:
        raise AssertionError("split D_Code checker modules are incomplete")
    repo_root = D_CODE.parents[1]
    checker_paths = {
        path.relative_to(D_CODE).as_posix() for path in checker_files
    }
    if check_index:
        check_git_index_contract_paths(checker_paths, "D_Code checker modules")
    ignored = subprocess.run(
        [
            "git",
            "check-ignore",
            "--",
            *(str(path.relative_to(repo_root)) for path in checker_files),
        ],
        cwd=repo_root,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if ignored.returncode == 0:
        raise AssertionError(
            "split D_Code checker modules are ignored by Git: "
            f"{ignored.stdout.strip()}"
        )
    if ignored.returncode != 1:
        raise AssertionError(f"git check-ignore failed: {ignored.stderr}")

    code_files = [path for pattern in ("*.c", "*.h") for path in D_CODE.rglob(pattern)
                  if path.relative_to(D_CODE).parts[:2] != ("build", "out")]
    code = "\n".join(path.read_text(encoding="utf-8", errors="replace") for path in code_files)
    for forbidden in (
        '"ui/ui.h"',
        '#include "include/',
        "PAGE_FLAG_OVERLAY_MODE",
        "page_manager_get_container",
        "page_manager_back_to_main_with_transition",
        "page_manager_touch_down",
        "page_manager_touch_move",
        "page_manager_touch_up",
        "page_manager_set_overlay_mode",
        "page_manager_get_overlay_mode",
        "d_module_find_by_page",
        "d_module_find_by_id",
        "d_module_deinit_all",
        "D_MODULE_UI_DEFINE",
        "D_MODULE_UI_DEFINE_EX",
        "D_MODULE_FUNC_DEFINE",
        "D_PWM_TEST_DEMO",
        "g_font_cn",
    ):
        if forbidden in code:
            raise AssertionError(f"dead or ambiguous public surface remains: {forbidden}")
    if re.search(r"#define\s+LCD_(?:W|H)\b", code):
        raise AssertionError("generic LCD dimension macro remains outside a profile owner")
    module_header = (D_CODE / "framework" / "d_ui_module.h").read_text(encoding="utf-8")
    if not re.search(r"#define\s+D_MODULE_MAX_COUNT\s+24\b", module_header):
        raise AssertionError("D_MODULE_MAX_COUNT must remain 24")


def check_manifest_contract(*, check_index: bool = True) -> None:
    manifest = FIRMWARE_SOURCES.read_text(encoding="utf-8")
    sources = re.findall(r'D_SOURCE\("([^"]+\.c)"\)', manifest)
    if len(sources) != len(set(sources)):
        raise AssertionError("source manifest contains duplicate C entries")
    non_production_sources = [
        source for source in sources
        if source.startswith(("tests/", "tools/"))
    ]
    if non_production_sources:
        raise AssertionError(
            "source manifest must not select host tools or test-owned sources: "
            f"{non_production_sources}"
        )
    if check_index:
        check_git_index_contract_paths(set(sources), "firmware source manifest")
    missing = [source for source in sources if not (D_CODE / source).is_file()]
    if missing:
        raise AssertionError(f"source manifest paths missing: {missing}")
    product_sources = set(PRODUCT_UI_ROOT.rglob("*.c"))
    listed = {D_CODE / source for source in sources}
    unlisted = sorted(product_sources - listed)
    if unlisted:
        names = [path.relative_to(D_CODE).as_posix() for path in unlisted]
        raise AssertionError(f"Product UI C files missing from manifest: {names}")
    tcp_source = 'D_SOURCE("modules/diagnostics/network/tcp_client/d_tcp_client.c")'
    tcp_gate = re.search(
        r"#ifdef\s+D_TCP_CLIENT_TEST\s+" + re.escape(tcp_source) + r"\s+#endif",
        manifest,
    )
    if tcp_gate is None:
        raise AssertionError("TCP client diagnostic must be selected by D_TCP_CLIENT_TEST")


def check_cross_owner_includes() -> None:
    """Preserve the full-suite entry while resolving the actual declared targets."""
    from .include_contract import check
    result = check(D_CODE, MAKEFILE)
    if result['exit_code']:
        findings = [f"{row['source']}:{row['line']} -> {row['include']}: {row['reason']}"
                    for row in result['findings'] if row['status'] in ('violation','unresolved')]
        raise AssertionError('Declared architecture dependencies are not fully verified: ' + '; '.join(findings[:12]))


def check_product_ui_registry_contract() -> None:
    """Keep Product UI page registration and Home visibility in one manifest."""
    registry = (
        PRODUCT_UI_ROOT
        / "shell"
        / "app_registry"
        / "product_ui_app_registry.c"
    ).read_text(encoding="utf-8")
    ui_startup = (D_CODE / "app" / "d_ui_startup.c").read_text(encoding="utf-8")
    home = (
        PRODUCT_UI_ROOT / "shell" / "home" / "product_ui_home.c"
    ).read_text(encoding="utf-8")

    for token in (
        "PRODUCT_UI_HOME_ENTRY(",
        "PRODUCT_UI_DETAIL_ENTRY(",
        "product_ui_app_registry_home_count",
        "product_ui_app_registry_home_get",
    ):
        if token not in registry:
            raise AssertionError(f"Product UI app registry contract missing {token}")
    if "product_ui_app_registry_count()" not in ui_startup:
        raise AssertionError("UI startup does not register Product UI manifest entries")
    for token in (
        "product_ui_app_registry_home_count()",
        "product_ui_app_registry_home_get(",
    ):
        if token not in home:
            raise AssertionError(f"Home does not consume Product UI app registry: {token}")
    if "s_product_apps" in home:
        raise AssertionError("Home still owns a second Product UI app catalog")


def check_public_headers(*, check_index: bool = True) -> None:
    if check_index:
        check_git_index_contract_paths(set(PUBLIC_HEADERS), "public headers")
    lvgl_stub = (
        "#ifndef LVGL_H\n#define LVGL_H\n#include <stdint.h>\n"
        "typedef struct lv_obj_t lv_obj_t;\n"
        "typedef struct lv_font_t { const struct lv_font_t *fallback; } lv_font_t;\n"
        "#define LV_FONT_DECLARE(name) extern const lv_font_t name\n"
        "#endif\n"
    )
    for language in ("c", "c++"):
        for enabled in (False, True):
            with tempfile.TemporaryDirectory(prefix="d_code_header_") as temporary:
                root = Path(temporary)
                (root / "config").mkdir()
                (root / "config" / "app_config.h").write_text(
                    "#define D_LVGL_ENABLE\n" if enabled else "",
                    encoding="utf-8",
                )
                (root / "lvgl.h").write_text(lvgl_stub, encoding="utf-8")
                for index, header in enumerate(PUBLIC_HEADERS):
                    suffix = "cpp" if language == "c++" else "c"
                    source = root / f"probe_{index}.{suffix}"
                    profile_defines = ""
                    if header.endswith("d_screen_gc9307_hyn.h"):
                        profile_defines = (
                            "#define D_LCD_ENABLE 1\n"
                            "#define D_BOARD_IS_GC9307 1\n"
                        )
                    source.write_text(
                        f'{profile_defines}#include "{header}"\n',
                        encoding="utf-8",
                    )
                    result = subprocess.run(
                        [
                            compiler_path(), "-fsyntax-only", "-x", language,
                            f"-I{root}", f"-I{D_CODE}",
                            f"-I{D_CODE / 'framework'}", str(source),
                        ],
                        check=False, capture_output=True, text=True,
                        encoding="utf-8", errors="replace",
                    )
                    if result.returncode != 0:
                        mode = "enabled" if enabled else "disabled"
                        raise AssertionError(
                            f"header {header} fails {language}/{mode}:\n"
                            f"{result.stderr}"
                        )


def check_stdio_compat_contract() -> None:
    compat = (D_CODE / "framework" / "compat" / "d_stdio_compat.h").read_text(
        encoding="utf-8"
    )
    for token in (
        '__has_include("system/generic/printf.h")',
        "unsigned long size",
        "__builtin_va_list ap",
    ):
        if token not in compat:
            raise AssertionError(f"SDK stdio-compatible declaration missing {token}")
    for forbidden in ("size_t size", "#include <stdio.h>"):
        if forbidden in compat:
            raise AssertionError(f"incompatible stdio declaration remains: {forbidden}")
    if re.search(r"(?<!__builtin_)\bva_list\s+ap\b", compat):
        raise AssertionError("incompatible stdio declaration remains: va_list ap")


def check_lcd_static_clear_contract() -> None:
    lcd_header = (D_CODE / "platform/screen/lcd/d_lcd_compat.h").read_text(
        encoding="utf-8"
    )
    lcd_source = (D_CODE / "platform/screen/lcd/d_lcd_compat.c").read_text(
        encoding="utf-8"
    )
    emi_drivers = (
        D_CODE
        / "platform/screen/profiles/emi_ili948x_gt911_ft6236/lcd_ili9481.c",
        D_CODE
        / "platform/screen/profiles/emi_ili948x_gt911_ft6236/lcd_ili9488.c",
    )

    if "d_lcd_fill_rect" in lcd_header + lcd_source:
        raise AssertionError("unused dynamic d_lcd_fill_rect API remains")
    for token in (
        "D_LCD_SOLID_CHUNK_PIXELS",
        "s_lcd_solid_chunk",
        "d_lcd_write_solid_pixels",
        "lcd_interface_non_block_wait();",
    ):
        if token not in lcd_header + lcd_source:
            raise AssertionError(f"bounded LCD solid-color writer missing {token}")
    for driver_path in emi_drivers:
        driver = driver_path.read_text(encoding="utf-8")
        if re.search(r"\b(?:malloc|free)\s*\(", driver):
            raise AssertionError(
                f"EMI clear path still uses heap: {driver_path.relative_to(D_CODE)}"
            )
        if "d_lcd_write_solid_pixels" not in driver:
            raise AssertionError(
                f"EMI clear path does not use bounded writer: "
                f"{driver_path.relative_to(D_CODE)}"
            )


def check_layout_contract() -> None:
    check_git_index_helper_contract()
    check_public_surface()
    makefile = MAKEFILE.read_text(encoding="utf-8")
    check_make_contract(makefile)
    check_paths_and_dead_surfaces()
    check_manifest_contract()
    check_public_headers()
    check_stdio_compat_contract()
    check_lcd_static_clear_contract()
    from .public_distribution import check_public_distribution
    check_public_distribution(D_CODE.parents[1])
