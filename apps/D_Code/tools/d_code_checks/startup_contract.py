"""Static and LVGL-free models for startup publication contracts."""

from __future__ import annotations

from dataclasses import dataclass
import re

from .support import (
    AI_PRESENTER,
    AI_SERVICE_HEADER,
    APP_MAIN,
    EVENT_BUS_HEADER,
    EVENT_BUS_SOURCE,
    LVGL_HEADER,
    LVGL_SOURCE,
    UI_MODULE_HEADER,
    UI_MODULE_SOURCE,
    SERVICE_STARTUP,
    POMODORO_PAGE_SOURCE,
    DEVICE_VOLUME_HEADER,
    DEVICE_VOLUME_SOURCE,
    QWEN_SERVICE_SOURCE,
    UI_STARTUP_HEADER,
    UI_STARTUP_SOURCE,
    error_code,
)


@dataclass
class ProducerModel:
    state: str = "NOT_STARTED"
    result: int = -5
    published: bool = False
    started: bool = False
    callback_count: int = 0

    def init(self, valid: bool) -> int | None:
        if not valid:
            return error_code("D_ERR_INVALID_ARG")
        if self.published:
            return self.result
        if self.started:
            return error_code("D_ERR_BUSY")
        self.started = True
        self.state = "STARTING"
        return None

    def publish(self, result: int) -> int:
        if self.published:
            return self.result
        self.result = result
        self.published = True
        self.state = "READY" if result == error_code("D_OK") else "FAILED"
        self.callback_count += 1
        return result

    def timeout(self) -> int:
        if self.published:
            return self.result
        self.state = "LATE_PENDING"
        return error_code("D_ERR_TIMEOUT")


@dataclass
class ConsumerModel:
    result: int = -5
    published: bool = False
    consumed: bool = False
    pending: bool = False
    summary_reported: bool = False
    startup_error: bool = False
    finalize_count: int = 0

    def publish(self, result: int) -> None:
        if not self.published and not self.consumed:
            self.result = result
            self.published = True
        if self.summary_reported:
            self.finalize()

    def mark_pending_if_unsettled(self) -> bool:
        if self.published or self.consumed:
            return False
        self.pending = True
        return True

    def finalize(self) -> bool:
        if not self.published or self.consumed:
            return False
        self.consumed = True
        self.pending = False
        self.startup_error = self.result != error_code("D_OK")
        self.finalize_count += 1
        return True


@dataclass
class VolumeBackendModel:
    active_backend: str | None = None
    update_in_progress: bool = False

    def register(self, backend: str) -> int:
        if self.update_in_progress:
            return error_code("D_ERR_BUSY")
        if self.active_backend not in (None, backend):
            return error_code("D_ERR_BUSY")
        self.active_backend = backend
        return error_code("D_OK")

    def unregister(self, backend: str) -> int:
        if self.active_backend not in (None, backend):
            return error_code("D_ERR_NOT_FOUND")
        if self.update_in_progress:
            return error_code("D_ERR_BUSY")
        self.active_backend = None
        return error_code("D_OK")


def check_exact_once_models() -> None:
    ok = error_code("D_OK")
    io_error = error_code("D_ERR_IO")
    not_ready = error_code("D_ERR_NOT_READY")
    busy = error_code("D_ERR_BUSY")

    invalid = ProducerModel()
    before = vars(invalid).copy()
    if invalid.init(False) != error_code("D_ERR_INVALID_ARG") or vars(invalid) != before:
        raise AssertionError("invalid LVGL spec must not change initializer state")

    starting = ProducerModel()
    if starting.init(True) is not None or starting.init(True) != busy:
        raise AssertionError("repeated init during STARTING must return D_ERR_BUSY")

    sync_failure = ProducerModel()
    sync_failure.init(True)
    sync_failure.publish(io_error)
    if sync_failure.state != "FAILED" or sync_failure.callback_count != 1:
        raise AssertionError("task creation failure must publish one FAILED terminal result")

    terminal_repeat = ProducerModel()
    terminal_repeat.init(True)
    terminal_repeat.publish(ok)
    if terminal_repeat.init(True) != ok or terminal_repeat.callback_count != 1:
        raise AssertionError("terminal repeat init must return cache without a second callback")

    for result in (ok, not_ready):
        late = ProducerModel()
        late.init(True)
        if late.timeout() != error_code("D_ERR_TIMEOUT"):
            raise AssertionError("timeout must detach an unpublished initializer")
        late.publish(result)
        if not late.published or late.result != result or late.callback_count != 1:
            raise AssertionError("late terminal result must remain exact-once")

    callback_first = ConsumerModel(pending=True)
    callback_first.publish(ok)
    callback_first.publish(ok)
    callback_first.finalize()
    if callback_first.finalize_count != 1:
        raise AssertionError("callback-before-init-return must be consumed once")

    snapshot_first = ConsumerModel(pending=True)
    snapshot_first.publish(ok)
    snapshot_first.finalize()
    snapshot_first.publish(ok)
    if snapshot_first.finalize_count != 1:
        raise AssertionError("snapshot-before-callback must be consumed once")

    snapshot_pending_race = ConsumerModel()
    snapshot_pending_race.publish(ok)
    if snapshot_pending_race.mark_pending_if_unsettled():
        raise AssertionError("a callback result must win the snapshot-to-pending race")
    snapshot_pending_race.finalize()
    if snapshot_pending_race.pending or snapshot_pending_race.finalize_count != 1:
        raise AssertionError("snapshot-to-pending race must settle without stale pending state")

    before_summary = ConsumerModel(pending=True)
    before_summary.publish(ok)
    before_summary.summary_reported = True
    before_summary.finalize()
    if before_summary.finalize_count != 1:
        raise AssertionError("callback before summary must settle once after summary")

    after_summary = ConsumerModel(pending=True, summary_reported=True)
    after_summary.publish(not_ready)
    if after_summary.finalize_count != 1 or not after_summary.startup_error:
        raise AssertionError("callback after summary must settle valid errors once")


def check_volume_backend_model() -> None:
    model = VolumeBackendModel()
    if model.register("qwen") != error_code("D_OK"):
        raise AssertionError("Qwen volume backend registration must succeed")
    if model.register("qwen") != error_code("D_OK"):
        raise AssertionError("same volume backend registration must be idempotent")
    if model.unregister("other") != error_code("D_ERR_NOT_FOUND"):
        raise AssertionError("a different backend must not release the active owner")
    if model.active_backend != "qwen":
        raise AssertionError("mismatched backend release corrupted the active owner")

    model.update_in_progress = True
    if model.unregister("qwen") != error_code("D_ERR_BUSY"):
        raise AssertionError("backend release must wait for an active callback")
    if model.active_backend != "qwen":
        raise AssertionError("busy backend release detached a live callback")

    model.update_in_progress = False
    if model.unregister("qwen") != error_code("D_OK"):
        raise AssertionError("matching idle backend release must succeed")
    if model.unregister("qwen") != error_code("D_OK"):
        raise AssertionError("empty backend release must be idempotent")
    if model.register("replacement") != error_code("D_OK"):
        raise AssertionError("a replacement backend must register after release")


def check_startup_contract() -> None:
    app_main = APP_MAIN.read_text(encoding="utf-8")
    service_startup = SERVICE_STARTUP.read_text(encoding="utf-8")
    pomodoro_page = POMODORO_PAGE_SOURCE.read_text(encoding="utf-8")
    event_bus_header = EVENT_BUS_HEADER.read_text(encoding="utf-8")
    event_bus_source = EVENT_BUS_SOURCE.read_text(encoding="utf-8")
    module_header = UI_MODULE_HEADER.read_text(encoding="utf-8")
    module_source = UI_MODULE_SOURCE.read_text(encoding="utf-8")
    lvgl_header = LVGL_HEADER.read_text(encoding="utf-8")
    lvgl = LVGL_SOURCE.read_text(encoding="utf-8")
    ui_startup = UI_STARTUP_SOURCE.read_text(encoding="utf-8")
    ai_presenter = AI_PRESENTER.read_text(encoding="utf-8")
    ai_service_header = AI_SERVICE_HEADER.read_text(encoding="utf-8")
    ui_startup_header = UI_STARTUP_HEADER.read_text(encoding="utf-8")
    volume_header = DEVICE_VOLUME_HEADER.read_text(encoding="utf-8")
    volume_source = DEVICE_VOLUME_SOURCE.read_text(encoding="utf-8")
    qwen_service = QWEN_SERVICE_SOURCE.read_text(encoding="utf-8")

    service_call = app_main.find("d_service_start_all()")
    ui_call = app_main.find("d_lvgl_init(&ui_spec)")
    if not (0 <= service_call < ui_call):
        raise AssertionError("app_main must start services before the UI initializer")

    for token in (
        "D_SERVICE_POLICY_REQUIRED",
        "D_SERVICE_POLICY_OPTIONAL",
        "required_failure_count",
        "optional_failure_count",
    ):
        if token not in service_startup:
            raise AssertionError(f"service result aggregation missing {token}")
    websocket_start = service_startup.find("d_service_start_websocket(void)")
    websocket_init = service_startup.find("ws_service_init()", websocket_start)
    bridge_init = service_startup.find("ai_service_ws_bridge_init()", websocket_start)
    service_list = service_startup.find("g_services[]", websocket_start)
    if not (0 <= websocket_start < websocket_init < bridge_init < service_list):
        raise AssertionError("AI bridge must remain inside WebSocket success ownership")
    if "d_service_start_ai_ws_bridge" in service_startup:
        raise AssertionError("AI bridge must not become an independent service entry")

    if "d_err_t d_event_bus_init(void)" not in event_bus_header:
        raise AssertionError("event bus init must publish a result")
    for token in (
        "os_mutex_create(&s_mutex) != OS_NO_ERR",
        "os_sem_create(&s_ring_sem, 0) != OS_NO_ERR",
        'thread_fork("d_evt_disp"',
        "return D_ERR_IO;",
    ):
        if token not in event_bus_source:
            raise AssertionError(f"event bus failure closure missing {token}")

    for token in (
        "d_lvgl_init_spec_t",
        "d_lvgl_init_snapshot_t",
        "d_lvgl_get_init_snapshot",
        "is_result_published",
    ):
        if token not in lvgl_header:
            raise AssertionError(f"LVGL public snapshot contract missing {token}")
    for token in (
        "s_lvgl_init_spec = *spec;",
        "d_lvgl_publish_terminal",
        "s_is_result_published = 1U;",
        "D_LVGL_INIT_LATE_PENDING",
        "os_sem_post(&s_lvgl_init_sem)",
        "callback(result, callback_context)",
    ):
        if token not in lvgl:
            raise AssertionError(f"LVGL exact-once implementation missing {token}")
    for forbidden in (
        "d_lvgl_get_init_state",
        "d_lvgl_get_init_result",
        "d_lvgl_transition_begin",
        "os_sem_del(&s_lvgl_init_sem",
        "s_lvgl_init_result != D_ERR_NOT_READY",
    ):
        if forbidden in lvgl + lvgl_header:
            raise AssertionError(f"obsolete LVGL contract still selected: {forbidden}")

    for token in (
        "is_ui_result_published",
        "is_ui_result_consumed",
        "d_app_publish_ui_result",
        "d_app_mark_ui_init_pending",
        "d_app_finalize_ui_result",
        "ui_snapshot.is_result_published",
    ):
        if token not in app_main:
            raise AssertionError(f"app exact-once consumer missing {token}")
    if "s_app_startup.is_ui_result_published = 0U;" not in app_main:
        raise AssertionError("app tracker must initialize publication state explicitly")
    if app_main.count("s_app_startup.is_ui_result_published = 0U;") != 1:
        raise AssertionError("app consumer must not clear the historical published bit")
    if app_main.count("s_app_startup.is_ui_init_pending = 1U;") != 1:
        raise AssertionError("UI pending state must have one guarded writer")
    if "if (d_app_mark_ui_init_pending() != 0)" not in app_main:
        raise AssertionError("snapshot-to-pending transition must use the guarded writer")
    if re.search(r"LOG[EWIDV]\([^,]+,\s*[^,;]+\?", app_main):
        raise AssertionError("logging format must not be a ternary expression")

    for token in (
        "d_err_t (*init)(void);",
        "d_err_t d_ui_module_init_all(void);",
    ):
        if token not in module_header:
            raise AssertionError(f"module initialization result contract missing {token}")
    for token in (
        "d_err_t d_ui_module_init_all(void)",
        "init_result = module->init();",
        "return init_result;",
    ):
        if token not in module_source:
            raise AssertionError(f"module initialization failure propagation missing {token}")

    bootstrap_order = [
        ui_startup.find("d_ui_module_init();"),
        ui_startup.find("page_manager_init();"),
        ui_startup.find("d_ui_startup_register_root()"),
        ui_startup.find("d_ui_startup_register_modules()"),
        ui_startup.find("d_ui_module_init_all();"),
        ui_startup.find("d_ui_startup_prepare()"),
        ui_startup.find("page_manager_switch_to(PAGE_TYPE_MAIN"),
    ]
    if bootstrap_order != sorted(bootstrap_order) or min(bootstrap_order) < 0:
        raise AssertionError("UI bootstrap phase order is incomplete or reordered")
    if "init_result = d_ui_module_init_all();" not in ui_startup:
        raise AssertionError("UI startup must consume module initialization results")

    for token in ("product_ui_pomodoro_init()", "PRODUCT_UI_SERVICE_ERROR",
                  "product_ui_service_publish(PRODUCT_UI_SERVICE_POMODORO", "return D_OK;"):
        if token not in pomodoro_page:
            raise AssertionError(f"Pomodoro optional-capability startup contract missing {token}")

    wifi_subscribe = ai_presenter.find("d_event_subscribe(EVT_WIFI_CONNECTED")
    wifi_snapshot = ai_presenter.find("wifi_service_get_status(&status)")
    if not (0 <= wifi_subscribe < wifi_snapshot):
        raise AssertionError("AI Presenter must subscribe before WiFi reconciliation")
    if ai_presenter.count("if (d_event_subscribe(") != 5:
        raise AssertionError("AI Presenter must check all five event subscriptions")
    for token in (
        "d_event_unsubscribe(EVT_AI_RESPONSE",
        "d_event_unsubscribe(EVT_AI_ERROR",
        "d_event_unsubscribe(EVT_AI_BUSY",
        "d_event_unsubscribe(EVT_WIFI_CONNECTED",
        "return D_ERR_IO;",
    ):
        if token not in ai_presenter:
            raise AssertionError(f"AI Presenter subscription rollback missing {token}")
    app_include = ai_service_header.find('#include "config/app_config.h"')
    websocket_guard = ai_service_header.find("#if D_DEVICE_CLOUD_ENABLE")
    if not (0 <= app_include < websocket_guard):
        raise AssertionError("ai_service.h must own its feature macro entry")

    signature = "const char *d_ui_startup_name(void)"
    if signature not in ui_startup_header or signature not in ui_startup:
        raise AssertionError("UI startup name declaration and definition must match")

    for token in (
        "d_device_volume_unregister_backend",
        "D_ERR_NOT_FOUND",
        "s_volume.state.is_in_progress",
        "s_volume.is_detaching = 1U;",
        "s_volume.state.is_pending = 0U;",
        "memset(&s_volume.backend, 0, sizeof(s_volume.backend));",
    ):
        if token not in volume_header + volume_source:
            raise AssertionError(f"volume backend release contract missing {token}")
    unregister = volume_source.split("d_err_t d_device_volume_unregister_backend(", 1)[1]
    unregister = unregister.split("void d_device_volume_quiesce_begin", 1)[0]
    busy = unregister.find("if (s_volume.state.is_in_progress)")
    release = unregister.find("memset(&s_volume.backend, 0, sizeof(s_volume.backend));")
    if not (0 <= busy < release) or "return D_ERR_BUSY;" not in unregister[busy:release]:
        raise AssertionError("volume backend must retain its callback until the active operation returns")
    for token in (
        "s_qwen_volume_backend",
        "qwen_service_unregister_volume_backend",
        "qwen_service_deinit_audio",
        "d_device_volume_unregister_backend(&s_qwen_volume_backend)",
        "QWEN_VOLUME_BACKEND_RELEASE_RETRIES",
        "return D_ERR_NOT_READY;",
    ):
        if token not in qwen_service:
            raise AssertionError(f"Qwen volume lifecycle closure missing {token}")
    if qwen_service.count("qwen_audio_deinit();") != 3:
        raise AssertionError(
            "Qwen audio teardown must remain owned by registration rollback, "
            "init rollback, and the backend-detached service teardown"
        )
    deinit_start = qwen_service.find("void qwen_service_deinit(void)")
    detach = qwen_service.find(
        "qwen_service_unregister_volume_backend()", deinit_start
    )
    unsubscribe = qwen_service.find("d_event_unsubscribe(", deinit_start)
    audio_deinit = qwen_service.find("qwen_audio_deinit();", deinit_start)
    if not (0 <= deinit_start < detach < unsubscribe < audio_deinit):
        raise AssertionError(
            "Qwen service teardown must detach volume before stopping owners and "
            "deinitializing audio"
        )

    check_exact_once_models()
    check_volume_backend_model()
