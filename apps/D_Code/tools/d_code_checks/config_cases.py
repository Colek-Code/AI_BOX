"""Valid, invalid and source-membership configuration fixtures."""

from __future__ import annotations

from .support import disabled_services, expect_invalid, expect_valid


def run_config_cases() -> None:
    device_cloud = (
        "modules/services/provisioning/device_credentials.c",
        "modules/services/provisioning/provision_service.c",
        "modules/services/websocket/ws_service.c",
    )
    expect_valid(
        "Qwen full-managed isolates the device platform",
        {"D_QWEN_AUTH_MODE": "D_QWEN_AUTH_MODE_FULL_MANAGED", "D_FEATURE_WEBSOCKET": "1"},
        {"D_BUILD_QWEN_AUTH_MODE": "1", "D_BUILD_FEATURE_WEBSOCKET": "0"},
        present_sources=("framework/d_crypto.c", "modules/integrations/qwen/qwen_net.c"),
        absent_sources=device_cloud,
    )
    expect_valid(
        "Qwen semi-managed retains platform credentials and bootstrap",
        {"D_QWEN_AUTH_MODE": "D_QWEN_AUTH_MODE_SEMI_MANAGED", "D_FEATURE_WEBSOCKET": "1"},
        {"D_BUILD_QWEN_AUTH_MODE": "2", "D_BUILD_FEATURE_WEBSOCKET": "1"},
        present_sources=device_cloud + ("modules/integrations/qwen/qwen_net.c",),
    )
    expect_invalid("Qwen semi-managed requires the platform",
        {"D_QWEN_AUTH_MODE": "D_QWEN_AUTH_MODE_SEMI_MANAGED", "D_FEATURE_WEBSOCKET": "0"},
        "半托管需要自有平台首配")
    expect_invalid("Unknown Qwen owner", {"D_QWEN_AUTH_MODE": "3"}, "D_QWEN_AUTH_MODE 无效")
    expect_valid(
        "GC Product UI",
        {},
        {"D_BUILD_BOARD_GC9307": "1", "D_BUILD_UI_PRODUCT": "1"},
        present_sources=(
            "modules/applications/product_ui/product_ui.c",
            "platform/screen/profiles/gc9307_hyn/d_display_gc9307.c",
            "app/d_ui_startup.c",
            "framework/d_ui_font.c",
        ),
        absent_sources=(
            "modules/diagnostics/display/tablet_shell/tablet_shell.c",
            "modules/diagnostics/display/screen_test/screen_test_page.c",
            "platform/screen/profiles/emi_ili948x_gt911_ft6236/d_display_emi.c",
            "ui/ui.c",
        ),
    )

    screen_only = disabled_services()
    expect_valid(
        "GC TabletShell",
        {**screen_only, "D_UI_TARGET": "D_UI_TABLET_SHELL"},
        {
            "D_BUILD_BOARD_GC9307": "1",
            "D_BUILD_UI_TABLET_SHELL": "1",
            "D_BUILD_AUDIO": "0",
        },
        present_sources=("modules/diagnostics/display/tablet_shell/tablet_shell.c",),
        absent_sources=(
            "modules/applications/product_ui/product_ui.c",
            "modules/diagnostics/display/screen_test/screen_test_page.c",
        ),
    )
    expect_valid(
        "GC ScreenTest",
        {**screen_only, "D_UI_TARGET": "D_UI_SCREEN_TEST"},
        {
            "D_BUILD_BOARD_GC9307": "1",
            "D_BUILD_UI_SCREEN_TEST": "1",
            "D_BUILD_AUDIO": "0",
        },
        present_sources=("modules/diagnostics/display/screen_test/screen_test_page.c",),
        absent_sources=(
            "modules/applications/product_ui/product_ui.c",
            "modules/diagnostics/display/tablet_shell/tablet_shell.c",
        ),
    )
    expect_valid(
        "EMI legacy",
        {
            "D_FEATURE_OTA": "0",
            "D_BOARD_TARGET": "D_BOARD_COPY_EMI_ILI948X",
            "D_UI_TARGET": "D_UI_CIPHER_PANEL",
            "D_FEATURE_CAMERA": "0",
        },
        {"D_BUILD_BOARD_EMI_ILI948X": "1", "D_BUILD_UI_CIPHER_PANEL": "1"},
        present_sources=(
            "modules/applications/legacy_ui/cipher_panel/cipher_panel_demo.c",
            "platform/screen/profiles/emi_ili948x_gt911_ft6236/d_display_emi.c",
        ),
        absent_sources=(
            "modules/applications/product_ui/product_ui.c",
            "platform/screen/profiles/gc9307_hyn/d_display_gc9307.c",
        ),
    )
    expect_valid(
        "EMI LauncherDemo",
        {
            "D_FEATURE_OTA": "0",
            "D_BOARD_TARGET": "D_BOARD_COPY_EMI_ILI948X",
            "D_UI_TARGET": "D_UI_LAUNCHER_DEMO",
            "D_FEATURE_CAMERA": "0",
        },
        {"D_BUILD_BOARD_EMI_ILI948X": "1", "D_BUILD_UI_LAUNCHER_DEMO": "1"},
        present_sources=("modules/diagnostics/display/launcher_demo/launcher_demo.c",),
        absent_sources=(
            "modules/applications/legacy_ui/cipher_panel/cipher_panel_demo.c",
            "modules/diagnostics/display/light_spot_demo/light_spot_demo.c",
        ),
    )
    expect_valid(
        "EMI LightSpotDemo",
        {
            "D_FEATURE_OTA": "0",
            "D_BOARD_TARGET": "D_BOARD_COPY_EMI_ILI948X",
            "D_UI_TARGET": "D_UI_LIGHT_SPOT_DEMO",
            "D_FEATURE_CAMERA": "0",
        },
        {"D_BUILD_BOARD_EMI_ILI948X": "1", "D_BUILD_UI_LIGHT_SPOT_DEMO": "1"},
        present_sources=("modules/diagnostics/display/light_spot_demo/light_spot_demo.c",),
        absent_sources=(
            "modules/applications/legacy_ui/cipher_panel/cipher_panel_demo.c",
            "modules/diagnostics/display/launcher_demo/launcher_demo.c",
        ),
    )
    expect_valid(
        "Network and Qwen off",
        screen_only,
        {
            "D_BUILD_FEATURE_WIFI": "0",
            "D_BUILD_FEATURE_QWEN": "0",
            "D_BUILD_AUDIO": "1",
        },
        present_sources=("modules/services/audio/d_local_prompt.c",
                         "modules/services/audio/d_local_prompt_decoder.c"),
        absent_sources=(
            "modules/services/wifi/wifi_service.c",
            "modules/services/websocket/ws_service.c",
            "modules/integrations/qwen/qwen_service.c",
            "modules/integrations/xiaole_wakeup/xiaole_wakeup.c",
        ),
    )
    expect_valid(
        "Qwen without WebSocket",
        {"D_FEATURE_WEBSOCKET": "0"},
        {"D_BUILD_FEATURE_QWEN": "1", "D_BUILD_FEATURE_WEBSOCKET": "0"},
        present_sources=("framework/d_crypto.c", "modules/integrations/qwen/qwen_service.c"),
        absent_sources=(
            "modules/services/provisioning/device_credentials.c",
            "modules/services/provisioning/provision_service.c",
            "modules/services/websocket/ws_service.c",
        ),
    )
    expect_valid(
        "AI Chat without WebSocket",
        {
            "D_BOARD_TARGET": "D_BOARD_COPY_EMI_ILI948X",
            "D_UI_TARGET": "D_UI_CIPHER_PANEL",
            "D_FEATURE_WEBSOCKET": "0",
            "D_FEATURE_AI_CHAT": "1",
            "D_FEATURE_OTA": "0",
            "D_FEATURE_QWEN": "0",
            "D_FEATURE_XIAOLE_WAKEUP": "0",
        },
        {"D_BUILD_FEATURE_AI_CHAT": "1", "D_BUILD_FEATURE_WEBSOCKET": "0"},
        present_sources=("framework/d_crypto.c", "modules/applications/ai_chat/ai_service.c"),
        absent_sources=(
            "modules/services/provisioning/device_credentials.c",
            "modules/services/provisioning/provision_service.c",
            "modules/services/websocket/ws_service.c",
        ),
    )
    expect_valid(
        "AI Chat with WebSocket",
        {
            "D_BOARD_TARGET": "D_BOARD_COPY_EMI_ILI948X",
            "D_UI_TARGET": "D_UI_CIPHER_PANEL",
            "D_FEATURE_WEBSOCKET": "1",
            "D_FEATURE_AI_CHAT": "1",
            "D_FEATURE_OTA": "0",
            "D_FEATURE_QWEN": "0",
            "D_FEATURE_XIAOLE_WAKEUP": "0",
        },
        {"D_BUILD_FEATURE_AI_CHAT": "1", "D_BUILD_FEATURE_WEBSOCKET": "1"},
        present_sources=(
            "framework/d_crypto.c",
            "modules/services/provisioning/device_credentials.c",
            "modules/services/provisioning/provision_service.c",
            "modules/services/websocket/ws_service.c",
            "modules/applications/ai_chat/ai_service.c",
        ),
    )

    expect_invalid("Invalid boolean", {"D_FEATURE_WIFI": "2"}, "开关只能填写 0 或 1")
    expect_invalid(
        "Board/UI mismatch",
        {"D_BOARD_TARGET": "D_BOARD_COPY_EMI_ILI948X"},
        "只能搭配 GC9307/HYN 板型",
    )
    expect_invalid("Missing WiFi dependency", {"D_FEATURE_WIFI": "0"}, "需要同时启用 WiFi")
    expect_invalid("Voice-agent conflict", {"D_FEATURE_BRTC": "1"}, "只能启用一个")
    expect_valid("OTA enabled on Product dual bank", {"D_FEATURE_OTA": "1"},
        {"D_BUILD_FEATURE_OTA": "1", "D_BUILD_DOUBLE_BANK": "1", "D_BUILD_OTA_KEEP_VM": "1"},
        present_sources=("modules/services/ota/ota_service.c", "modules/services/ota/ota_https.c"))
    expect_valid("OTA disabled preserves layout", {"D_FEATURE_OTA": "0"},
        {"D_BUILD_FEATURE_OTA": "0", "D_BUILD_DOUBLE_BANK": "1"},
        absent_sources=("modules/services/ota/ota_service.c",))
    expect_valid("OTA erase is a target package policy", {"D_OTA_KEEP_VM": "0"},
        {"D_BUILD_OTA_KEEP_VM": "0", "D_BUILD_DOUBLE_BANK": "1"})
    expect_invalid("OTA invalid VM policy", {"D_OTA_KEEP_VM": "2"}, "OTA layout and VM policy")
    expect_valid("OTA maximum SDK sequence", {"D_OTA_BOOT_VERSION": "65535"},
        {"D_BUILD_OTA_BOOT_VERSION": "65535"})
    expect_invalid("OTA SDK sequence overflow", {"D_OTA_BOOT_VERSION": "65536"}, "D_OTA_BOOT_VERSION")
    expect_invalid("OTA SDK sequence negative", {"D_OTA_BOOT_VERSION": "-1"}, "D_OTA_BOOT_VERSION")
    expect_invalid("OTA requires Product", {"D_UI_TARGET": "D_UI_TABLET_SHELL", "D_FEATURE_OTA": "1"},
        "OTA requires")
    expect_invalid(
        "Power control without Key Service",
        {**screen_only, "D_FEATURE_POWER_CONTROL": "1"},
        "启用 Power Control 时必须同时启用 Key Service",
    )
    expect_invalid(
        "Qwen without Key Service",
        {**screen_only, "D_FEATURE_WIFI": "1", "D_FEATURE_QWEN": "1"},
        "启用 Qwen 时必须同时启用 Key Service",
    )
