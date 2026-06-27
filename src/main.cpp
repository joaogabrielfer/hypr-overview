#define WLR_USE_UNSTABLE

#include <hyprland/src/includes.hpp>

#include <algorithm>
#include <chrono>
#include <format>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#define private public
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/layout/algorithm/Algorithm.hpp>
#include <hyprland/src/layout/algorithm/tiled/scrolling/ScrollingAlgorithm.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <hyprland/src/layout/target/Target.hpp>
#include <hyprland/src/managers/PointerManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#undef private

inline HANDLE PHANDLE = nullptr;

namespace {

using Desktop::View::ALLOW_FLOATING;
using Desktop::View::FOLLOW_MOUSE_CHECK;
using Desktop::View::INPUT_EXTENTS;
using Desktop::View::WINDOW_ONLY;
using Layout::Tiled::CScrollingAlgorithm;
using Layout::Tiled::SColumnData;

constexpr uint32_t kBtnLeft   = 272;
constexpr uint32_t kBtnRight  = 273;
constexpr uint32_t kBtnMiddle = 274;

struct SSavedColumn {
    WP<SColumnData>           column;
    float                     width = 0.F;
    std::vector<PHLWINDOWREF> windows;
};

struct SOverviewConfig {
    bool  enabled                 = true;
    bool  cancelOnWorkspaceSwitch = true;
    bool  clickSelect             = true;
    bool  clickApply              = true;
    bool  rightClickCancel        = true;
    bool  warpCursorOnExit        = true;
    bool  restoreOriginalOnInvalidSelection = true;
    float fitMarginFactor         = 1.F;
    int   wheelSteps              = 1;
    std::string exitNotification  = "off";
};

struct SRuntimeConfigOverrides {
    std::optional<bool>        enabled;
    std::optional<bool>        cancelOnWorkspaceSwitch;
    std::optional<bool>        clickSelect;
    std::optional<bool>        clickApply;
    std::optional<bool>        rightClickCancel;
    std::optional<bool>        warpCursorOnExit;
    std::optional<bool>        restoreOriginalOnInvalidSelection;
    std::optional<float>       fitMarginFactor;
    std::optional<int>         wheelSteps;
    std::optional<std::string> exitNotification;
};

struct SOverviewState {
    bool                      active = false;
    bool                      refreshingLayout = false;
    PHLWORKSPACEREF           workspace;
    PHLWINDOWREF              originalFocus;
    PHLWINDOWREF              selectedFocus;
    std::vector<SSavedColumn> columns;
};

inline SOverviewState      g_state;
inline SOverviewConfig     g_config;
inline SRuntimeConfigOverrides g_runtimeConfig;
inline CHyprSignalListener g_windowActiveListener;
inline CHyprSignalListener g_workspaceListener;
inline CHyprSignalListener g_windowOpenListener;
inline CHyprSignalListener g_windowDestroyListener;
inline CHyprSignalListener g_windowMoveWorkspaceListener;
inline CHyprSignalListener g_mouseMoveListener;
inline CHyprSignalListener g_mouseButtonListener;
inline CHyprSignalListener g_mouseAxisListener;
inline CHyprSignalListener g_keyboardKeyListener;
inline CHyprSignalListener g_configPreReloadListener;
inline CHyprSignalListener g_configReloadedListener;

inline SP<Config::Values::CBoolValue>   g_cfgEnabled;
inline SP<Config::Values::CBoolValue>   g_cfgCancelOnWorkspaceSwitch;
inline SP<Config::Values::CBoolValue>   g_cfgClickSelect;
inline SP<Config::Values::CBoolValue>   g_cfgClickApply;
inline SP<Config::Values::CBoolValue>   g_cfgRightClickCancel;
inline SP<Config::Values::CBoolValue>   g_cfgWarpCursorOnExit;
inline SP<Config::Values::CBoolValue>   g_cfgRestoreOriginalOnInvalidSelection;
inline SP<Config::Values::CFloatValue>  g_cfgFitMarginFactor;
inline SP<Config::Values::CIntValue>    g_cfgWheelSteps;
inline SP<Config::Values::CStringValue> g_cfgExitNotification;

static SDispatchResult ok() {
    return {};
}

static SDispatchResult fail(const std::string& error) {
    return {.passEvent = false, .success = false, .error = error};
}

static void syncConfig() {
    if (g_cfgEnabled)
        g_config.enabled = g_cfgEnabled->value();
    if (g_cfgCancelOnWorkspaceSwitch)
        g_config.cancelOnWorkspaceSwitch = g_cfgCancelOnWorkspaceSwitch->value();
    if (g_cfgClickSelect)
        g_config.clickSelect = g_cfgClickSelect->value();
    if (g_cfgClickApply)
        g_config.clickApply = g_cfgClickApply->value();
    if (g_cfgRightClickCancel)
        g_config.rightClickCancel = g_cfgRightClickCancel->value();
    if (g_cfgWarpCursorOnExit)
        g_config.warpCursorOnExit = g_cfgWarpCursorOnExit->value();
    if (g_cfgRestoreOriginalOnInvalidSelection)
        g_config.restoreOriginalOnInvalidSelection = g_cfgRestoreOriginalOnInvalidSelection->value();
    if (g_cfgFitMarginFactor)
        g_config.fitMarginFactor = std::clamp(g_cfgFitMarginFactor->value(), 0.2F, 1.0F);
    if (g_cfgWheelSteps)
        g_config.wheelSteps = std::max<int>(1, g_cfgWheelSteps->value());
    if (g_cfgExitNotification)
        g_config.exitNotification = g_cfgExitNotification->value();

    if (g_runtimeConfig.enabled)
        g_config.enabled = *g_runtimeConfig.enabled;
    if (g_runtimeConfig.cancelOnWorkspaceSwitch)
        g_config.cancelOnWorkspaceSwitch = *g_runtimeConfig.cancelOnWorkspaceSwitch;
    if (g_runtimeConfig.clickSelect)
        g_config.clickSelect = *g_runtimeConfig.clickSelect;
    if (g_runtimeConfig.clickApply)
        g_config.clickApply = *g_runtimeConfig.clickApply;
    if (g_runtimeConfig.rightClickCancel)
        g_config.rightClickCancel = *g_runtimeConfig.rightClickCancel;
    if (g_runtimeConfig.warpCursorOnExit)
        g_config.warpCursorOnExit = *g_runtimeConfig.warpCursorOnExit;
    if (g_runtimeConfig.restoreOriginalOnInvalidSelection)
        g_config.restoreOriginalOnInvalidSelection = *g_runtimeConfig.restoreOriginalOnInvalidSelection;
    if (g_runtimeConfig.fitMarginFactor)
        g_config.fitMarginFactor = std::clamp(*g_runtimeConfig.fitMarginFactor, 0.2F, 1.0F);
    if (g_runtimeConfig.wheelSteps)
        g_config.wheelSteps = std::max<int>(1, *g_runtimeConfig.wheelSteps);
    if (g_runtimeConfig.exitNotification)
        g_config.exitNotification = *g_runtimeConfig.exitNotification;
}

static PHLWORKSPACE activeWorkspace() {
    const auto monitor = Desktop::focusState()->monitor();
    if (!monitor)
        return nullptr;

    return monitor->m_activeSpecialWorkspace ? monitor->m_activeSpecialWorkspace : monitor->m_activeWorkspace;
}

static CScrollingAlgorithm* scrollingAlgorithmFor(PHLWORKSPACE workspace) {
    if (!workspace || !workspace->m_space)
        return nullptr;

    const auto algo = workspace->m_space->algorithm();
    if (!algo || !algo->tiledAlgo())
        return nullptr;

    return dynamic_cast<CScrollingAlgorithm*>(algo->tiledAlgo().get());
}

static bool sameWorkspace(const PHLWINDOW& window, const PHLWORKSPACE& workspace) {
    return window && workspace && window->m_workspace == workspace;
}

static bool isScrollingTargetWindow(const PHLWINDOW& window, CScrollingAlgorithm* scrolling = nullptr) {
    if (!window || !window->m_target)
        return false;

    auto* effectiveScrolling = scrolling;
    if (!effectiveScrolling)
        effectiveScrolling = scrollingAlgorithmFor(window->m_workspace);

    return effectiveScrolling && effectiveScrolling->dataFor(window->m_target);
}

static PHLWINDOW firstOverviewWindow(CScrollingAlgorithm* scrolling) {
    if (!scrolling || !scrolling->m_scrollingData)
        return nullptr;

    for (const auto& column : scrolling->m_scrollingData->columns) {
        for (const auto& targetData : column->targetDatas) {
            if (const auto target = targetData->target.lock(); target && target->window())
                return target->window();
        }
    }

    return nullptr;
}

static bool isOverviewWorkspace(const PHLWORKSPACE& workspace) {
    return g_state.active && workspace && workspace == g_state.workspace.lock();
}

static bool isOverviewWindow(const PHLWINDOW& window) {
    return window && isOverviewWorkspace(window->m_workspace) && isScrollingTargetWindow(window);
}

static void clearState() {
    g_state = {};
}

static void saveColumns(CScrollingAlgorithm* scrolling) {
    g_state.columns.clear();

    if (!scrolling || !scrolling->m_scrollingData)
        return;

    for (const auto& column : scrolling->m_scrollingData->columns) {
        SSavedColumn saved;
        saved.column = column;
        saved.width  = column->getColumnWidth();

        for (const auto& targetData : column->targetDatas) {
            if (const auto target = targetData->target.lock(); target && target->window())
                saved.windows.emplace_back(target->window());
        }

        g_state.columns.emplace_back(std::move(saved));
    }
}

static size_t overviewColumnCount(CScrollingAlgorithm* scrolling) {
    if (!scrolling || !scrolling->m_scrollingData)
        return 0;

    return scrolling->m_scrollingData->columns.size();
}

static float overviewColumnWidth(CScrollingAlgorithm* scrolling) {
    const auto count = overviewColumnCount(scrolling);
    if (count == 0)
        return 1.F;

    return std::clamp((1.F / static_cast<float>(count)) * g_config.fitMarginFactor, 0.02F, 1.F);
}

static void fitOverview(CScrollingAlgorithm* scrolling) {
    if (!scrolling || !scrolling->m_scrollingData)
        return;

    const auto width = overviewColumnWidth(scrolling);
    for (const auto& column : scrolling->m_scrollingData->columns)
        column->setColumnWidth(width);

    scrolling->m_scrollingData->controller->setOffset(0);
    scrolling->m_scrollingData->lockedCameraOffset = 0;
    scrolling->m_scrollingData->recalculate();
}

static SP<SColumnData> resolveSavedColumn(CScrollingAlgorithm* scrolling, const SSavedColumn& saved) {
    if (!scrolling || !scrolling->m_scrollingData)
        return nullptr;

    if (const auto direct = saved.column.lock()) {
        const auto it = std::ranges::find(scrolling->m_scrollingData->columns, direct);
        if (it != scrolling->m_scrollingData->columns.end())
            return *it;
    }

    for (const auto& window : saved.windows) {
        if (!window || !window->m_target)
            continue;

        const auto data = scrolling->dataFor(window->m_target);
        if (data)
            return data->column.lock();
    }

    return nullptr;
}

static void restoreColumns(CScrollingAlgorithm* scrolling) {
    if (!scrolling || !scrolling->m_scrollingData)
        return;

    for (const auto& saved : g_state.columns) {
        const auto column = resolveSavedColumn(scrolling, saved);
        if (column)
            column->setColumnWidth(saved.width);
    }

    scrolling->m_scrollingData->lockedCameraOffset.reset();
    scrolling->m_scrollingData->recalculate();
}

static PHLWINDOW selectedWindowOrFallback() {
    if (auto selected = g_state.selectedFocus.lock(); isOverviewWindow(selected))
        return selected;

    if (auto original = g_state.originalFocus.lock(); isOverviewWindow(original))
        return original;

    const auto workspace = g_state.workspace.lock();
    return firstOverviewWindow(scrollingAlgorithmFor(workspace));
}

static bool focusWindow(PHLWINDOW window) {
    if (!window)
        return false;

    [[maybe_unused]] const auto focusResult = Config::Actions::focus(window);
    return Desktop::focusState()->window() == window;
}

static void centerWindow(CScrollingAlgorithm* scrolling, PHLWINDOW window) {
    if (!scrolling || !window || !window->m_target)
        return;

    const auto data = scrolling->dataFor(window->m_target);
    if (!data)
        return;

    const auto column = data->column.lock();
    if (!column)
        return;

    scrolling->focusColumn(column);
    scrolling->focusOnInput(window->m_target, CScrollingAlgorithm::INPUT_MODE_HARD);
}

static void setSelectedWindow(PHLWINDOW window, bool focusNow) {
    if (!isOverviewWindow(window))
        return;

    g_state.selectedFocus = window;
    if (focusNow)
        focusWindow(window);
}

static PHLWINDOW windowAtCursor() {
    if (!g_pPointerManager)
        return nullptr;

    const auto pos = g_pPointerManager->position();
    return g_pCompositor->vectorToWindowUnified(pos, WINDOW_ONLY | INPUT_EXTENTS | ALLOW_FLOATING | FOLLOW_MOUSE_CHECK);
}

static void refreshOverview(CScrollingAlgorithm* scrolling) {
    if (!g_state.active || g_state.refreshingLayout || !scrolling)
        return;

    g_state.refreshingLayout = true;
    fitOverview(scrolling);
    g_state.refreshingLayout = false;
}

static void rebuildSnapshot(CScrollingAlgorithm* scrolling) {
    if (!g_state.active)
        return;

    saveColumns(scrolling);
    refreshOverview(scrolling);
}

static void notifyExit(const std::string& summary) {
    const auto& mode = g_config.exitNotification;
    if (mode.empty() || mode == "off")
        return;

    HyprlandAPI::addNotification(PHANDLE, summary, CHyprColor{0.2F, 0.75F, 0.35F, 1.F}, 1800);
}

static void leaveOverview(bool restoreOriginalSelection) {
    const auto workspace = g_state.workspace.lock();
    auto*      scrolling = scrollingAlgorithmFor(workspace);
    auto       target    = restoreOriginalSelection ? g_state.originalFocus.lock() : g_state.selectedFocus.lock();

    if (!scrolling || !workspace) {
        clearState();
        return;
    }

    g_state.refreshingLayout = true;
    restoreColumns(scrolling);

    if (!target && g_config.restoreOriginalOnInvalidSelection)
        target = g_state.originalFocus.lock();

    if (sameWorkspace(target, workspace)) {
        g_state.selectedFocus = target;
        centerWindow(scrolling, target);
        focusWindow(target);
        if (g_config.warpCursorOnExit)
            g_pCompositor->warpCursorTo(target->middle(), true);
    }

    g_state.refreshingLayout = false;

    notifyExit(restoreOriginalSelection ? "Overview canceled" : "Overview applied");
    clearState();
}

static bool tryMoveSelection(Math::eDirection direction) {
    const auto workspace = g_state.workspace.lock();
    auto*      scrolling = scrollingAlgorithmFor(workspace);
    auto       current   = selectedWindowOrFallback();

    if (!scrolling || !workspace || !current)
        return false;

    const auto next = g_pCompositor->getWindowInDirection(current, direction);

    if (!sameWorkspace(next, workspace))
        return false;

    setSelectedWindow(next, true);
    refreshOverview(scrolling);
    return true;
}

static xkb_keysym_t keysymForEvent(const IKeyboard::SKeyEvent& event) {
    if (!g_pInputManager)
        return XKB_KEY_NoSymbol;

    for (const auto& keyboard : g_pInputManager->m_keyboards) {
        if (!keyboard || !keyboard->m_xkbSymState)
            continue;

        const auto sym = xkb_state_key_get_one_sym(keyboard->m_xkbSymState, event.keycode + 8);
        if (sym != XKB_KEY_NoSymbol)
            return sym;
    }

    return XKB_KEY_NoSymbol;
}

static bool handleOverviewKeys(const IKeyboard::SKeyEvent& event, Event::SCallbackInfo& info) {
    if (!g_state.active || event.state != WL_KEYBOARD_KEY_STATE_PRESSED)
        return false;

    const auto sym = keysymForEvent(event);
    switch (sym) {
        case XKB_KEY_Escape:
            leaveOverview(true);
            info.cancelled = true;
            return true;
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter:
        case XKB_KEY_space:
            leaveOverview(false);
            info.cancelled = true;
            return true;
        case XKB_KEY_Left:
        case XKB_KEY_h:
        case XKB_KEY_H:
            info.cancelled = tryMoveSelection(Math::DIRECTION_LEFT);
            return info.cancelled;
        case XKB_KEY_Right:
        case XKB_KEY_l:
        case XKB_KEY_L:
            info.cancelled = tryMoveSelection(Math::DIRECTION_RIGHT);
            return info.cancelled;
        case XKB_KEY_Up:
        case XKB_KEY_k:
        case XKB_KEY_K:
            info.cancelled = tryMoveSelection(Math::DIRECTION_UP);
            return info.cancelled;
        case XKB_KEY_Down:
        case XKB_KEY_j:
        case XKB_KEY_J:
            info.cancelled = tryMoveSelection(Math::DIRECTION_DOWN);
            return info.cancelled;
        default:
            return false;
    }
}

static void refreshOverviewAfterFocus(PHLWINDOW window) {
    if (!g_state.active || g_state.refreshingLayout)
        return;

    const auto workspace = g_state.workspace.lock();
    auto*      scrolling = scrollingAlgorithmFor(workspace);
    if (!workspace || !scrolling) {
        clearState();
        return;
    }

    if (window && sameWorkspace(window, workspace) && isScrollingTargetWindow(window, scrolling))
        g_state.selectedFocus = window;

    refreshOverview(scrolling);
}

static SDispatchResult enterOverview() {
    syncConfig();

    if (!g_config.enabled)
        return fail("Overview is disabled");

    const auto workspace = activeWorkspace();
    auto*      scrolling = scrollingAlgorithmFor(workspace);

    if (!workspace)
        return fail("No active workspace");

    if (!scrolling)
        return fail("Active workspace is not using the scrolling layout");

    if (workspace->m_hasFullscreenWindow)
        return fail("Overview does not support fullscreen windows yet");

    if (overviewColumnCount(scrolling) == 0)
        return fail("No scrolling columns to overview");

    clearState();
    g_state.active        = true;
    g_state.workspace     = workspace;
    g_state.originalFocus = Desktop::focusState()->window();
    g_state.selectedFocus = isScrollingTargetWindow(g_state.originalFocus.lock(), scrolling) ? g_state.originalFocus : firstOverviewWindow(scrolling);

    saveColumns(scrolling);
    refreshOverview(scrolling);

    return ok();
}

static SDispatchResult dispatchToggle(const std::string&) {
    if (g_state.active) {
        leaveOverview(false);
        return ok();
    }

    return enterOverview();
}

static SDispatchResult dispatchApply(const std::string&) {
    if (!g_state.active)
        return fail("Overview is not active");

    leaveOverview(false);
    return ok();
}

static SDispatchResult dispatchCancel(const std::string&) {
    if (!g_state.active)
        return fail("Overview is not active");

    leaveOverview(true);
    return ok();
}

static SDispatchResult dispatchStatus(const std::string&) {
    return {.passEvent = false, .success = true, .error = g_state.active ? "active" : "inactive"};
}

static bool toggleTriggerMatches(const IKeyboard::SKeyEvent& event) {
    if (g_state.active || !g_config.enabled || event.state != WL_KEYBOARD_KEY_STATE_PRESSED)
        return false;

    if (!g_pInputManager)
        return false;

    const auto sym = keysymForEvent(event);
    if (sym != XKB_KEY_Tab && sym != XKB_KEY_ISO_Left_Tab)
        return false;

    const auto mods         = g_pInputManager->getModsFromAllKBs();
    const auto significant  = HL_MODIFIER_SHIFT | HL_MODIFIER_CTRL | HL_MODIFIER_ALT | HL_MODIFIER_META;
    const auto expectedMods = HL_MODIFIER_META;
    return (mods & significant) == expectedMods;
}

static int luaToggle(lua_State* L) {
    const auto result = dispatchToggle("");
    lua_pushboolean(L, result.success);
    if (!result.success) {
        lua_pushstring(L, result.error.c_str());
        return 2;
    }
    return 1;
}

static int luaApply(lua_State* L) {
    const auto result = dispatchApply("");
    lua_pushboolean(L, result.success);
    if (!result.success) {
        lua_pushstring(L, result.error.c_str());
        return 2;
    }
    return 1;
}

static int luaCancel(lua_State* L) {
    const auto result = dispatchCancel("");
    lua_pushboolean(L, result.success);
    if (!result.success) {
        lua_pushstring(L, result.error.c_str());
        return 2;
    }
    return 1;
}

static int luaActive(lua_State* L) {
    lua_pushboolean(L, g_state.active);
    return 1;
}

static int luaStatus(lua_State* L) {
    lua_newtable(L);
    lua_pushboolean(L, g_state.active);
    lua_setfield(L, -2, "active");
    lua_pushboolean(L, g_config.enabled);
    lua_setfield(L, -2, "enabled");

    const auto workspace = g_state.workspace.lock();
    lua_pushinteger(L, workspace ? workspace->m_id : 0);
    lua_setfield(L, -2, "workspace_id");

    const auto selected = g_state.selectedFocus.lock();
    lua_pushboolean(L, static_cast<bool>(selected));
    lua_setfield(L, -2, "has_selection");

    lua_pushboolean(L, g_config.clickSelect);
    lua_setfield(L, -2, "click_select");
    lua_pushboolean(L, g_config.clickApply);
    lua_setfield(L, -2, "click_apply");
    lua_pushboolean(L, g_config.rightClickCancel);
    lua_setfield(L, -2, "right_click_cancel");
    lua_pushboolean(L, g_config.cancelOnWorkspaceSwitch);
    lua_setfield(L, -2, "cancel_on_workspace_switch");
    lua_pushboolean(L, g_config.warpCursorOnExit);
    lua_setfield(L, -2, "warp_cursor_on_exit");
    lua_pushboolean(L, g_config.restoreOriginalOnInvalidSelection);
    lua_setfield(L, -2, "restore_original_on_invalid_selection");
    lua_pushnumber(L, g_config.fitMarginFactor);
    lua_setfield(L, -2, "fit_margin_factor");
    lua_pushinteger(L, g_config.wheelSteps);
    lua_setfield(L, -2, "wheel_steps");
    lua_pushstring(L, g_config.exitNotification.c_str());
    lua_setfield(L, -2, "exit_notification");
    return 1;
}

template <typename T>
static void assignOptional(std::optional<T>& slot, T value) {
    slot = std::move(value);
}

static bool luaReadBooleanField(lua_State* L, const char* field, std::optional<bool>& target, std::string& error) {
    lua_getfield(L, 1, field);
    const auto type = lua_type(L, -1);
    if (type == LUA_TNIL) {
        lua_pop(L, 1);
        return true;
    }
    if (!lua_isboolean(L, -1)) {
        error = std::format("setup({}): expected boolean", field);
        lua_pop(L, 1);
        return false;
    }
    assignOptional(target, static_cast<bool>(lua_toboolean(L, -1)));
    lua_pop(L, 1);
    return true;
}

static bool luaReadIntegerField(lua_State* L, const char* field, std::optional<int>& target, std::string& error) {
    lua_getfield(L, 1, field);
    const auto type = lua_type(L, -1);
    if (type == LUA_TNIL) {
        lua_pop(L, 1);
        return true;
    }
    if (!lua_isinteger(L, -1)) {
        error = std::format("setup({}): expected integer", field);
        lua_pop(L, 1);
        return false;
    }
    assignOptional(target, static_cast<int>(lua_tointeger(L, -1)));
    lua_pop(L, 1);
    return true;
}

static bool luaReadNumberField(lua_State* L, const char* field, std::optional<float>& target, std::string& error) {
    lua_getfield(L, 1, field);
    const auto type = lua_type(L, -1);
    if (type == LUA_TNIL) {
        lua_pop(L, 1);
        return true;
    }
    if (!lua_isnumber(L, -1)) {
        error = std::format("setup({}): expected number", field);
        lua_pop(L, 1);
        return false;
    }
    assignOptional(target, static_cast<float>(lua_tonumber(L, -1)));
    lua_pop(L, 1);
    return true;
}

static bool luaReadStringField(lua_State* L, const char* field, std::optional<std::string>& target, std::string& error) {
    lua_getfield(L, 1, field);
    const auto type = lua_type(L, -1);
    if (type == LUA_TNIL) {
        lua_pop(L, 1);
        return true;
    }
    if (!lua_isstring(L, -1)) {
        error = std::format("setup({}): expected string", field);
        lua_pop(L, 1);
        return false;
    }
    assignOptional(target, std::string{lua_tostring(L, -1)});
    lua_pop(L, 1);
    return true;
}

static int luaSetup(lua_State* L) {
    if (lua_gettop(L) < 1 || !lua_istable(L, 1)) {
        lua_pushboolean(L, false);
        lua_pushstring(L, "setup(config): expected a table");
        return 2;
    }

    SRuntimeConfigOverrides updated = g_runtimeConfig;
    std::string             error;

    if (!luaReadBooleanField(L, "enabled", updated.enabled, error) ||
        !luaReadBooleanField(L, "cancel_on_workspace_switch", updated.cancelOnWorkspaceSwitch, error) ||
        !luaReadBooleanField(L, "click_select", updated.clickSelect, error) ||
        !luaReadBooleanField(L, "click_apply", updated.clickApply, error) ||
        !luaReadBooleanField(L, "right_click_cancel", updated.rightClickCancel, error) ||
        !luaReadBooleanField(L, "warp_cursor_on_exit", updated.warpCursorOnExit, error) ||
        !luaReadBooleanField(L, "restore_original_on_invalid_selection", updated.restoreOriginalOnInvalidSelection, error) ||
        !luaReadNumberField(L, "fit_margin_factor", updated.fitMarginFactor, error) ||
        !luaReadIntegerField(L, "wheel_steps", updated.wheelSteps, error) ||
        !luaReadStringField(L, "exit_notification", updated.exitNotification, error)) {
        lua_pushboolean(L, false);
        lua_pushstring(L, error.c_str());
        return 2;
    }

    const auto previousConfig = g_runtimeConfig;
    g_runtimeConfig           = std::move(updated);
    syncConfig();

    if (g_config.exitNotification != "off" && g_config.exitNotification != "basic") {
        g_runtimeConfig = previousConfig;
        syncConfig();
        lua_pushboolean(L, false);
        lua_pushstring(L, "setup(exit_notification): expected \"off\" or \"basic\"");
        return 2;
    }

    if (!g_config.enabled && g_state.active)
        leaveOverview(true);
    else if (g_state.active) {
        if (const auto workspace = g_state.workspace.lock(); workspace) {
            if (auto* scrolling = scrollingAlgorithmFor(workspace); scrolling)
                refreshOverview(scrolling);
        }
    }

    lua_pushboolean(L, true);
    return 1;
}

static void registerConfigValues() {
    g_cfgEnabled = makeShared<Config::Values::CBoolValue>(
        "plugin:hyproverview:enabled", "Whether the overview plugin is enabled", true);
    g_cfgCancelOnWorkspaceSwitch = makeShared<Config::Values::CBoolValue>(
        "plugin:hyproverview:cancel_on_workspace_switch", "Cancel overview when the active workspace changes", true);
    g_cfgClickSelect = makeShared<Config::Values::CBoolValue>(
        "plugin:hyproverview:click_select", "Track selection from hover and click in overview", true);
    g_cfgClickApply = makeShared<Config::Values::CBoolValue>(
        "plugin:hyproverview:click_apply", "Apply overview on left click", true);
    g_cfgRightClickCancel = makeShared<Config::Values::CBoolValue>(
        "plugin:hyproverview:right_click_cancel", "Cancel overview on right click", true);
    g_cfgWarpCursorOnExit = makeShared<Config::Values::CBoolValue>(
        "plugin:hyproverview:warp_cursor_on_exit", "Warp the cursor to the selected window when leaving overview", true);
    g_cfgRestoreOriginalOnInvalidSelection = makeShared<Config::Values::CBoolValue>(
        "plugin:hyproverview:restore_original_on_invalid_selection", "Fall back to the original window when the current selection vanished", true);
    g_cfgFitMarginFactor = makeShared<Config::Values::CFloatValue>(
        "plugin:hyproverview:fit_margin_factor", "Scale applied to overview column widths", 1.F,
        Config::Values::SFloatValueOptions{.min = 0.2F, .max = 1.0F});
    g_cfgWheelSteps = makeShared<Config::Values::CIntValue>(
        "plugin:hyproverview:wheel_steps", "Number of columns to move per wheel step in overview", 1,
        Config::Values::SIntValueOptions{.min = 1, .max = 8});
    g_cfgExitNotification = makeShared<Config::Values::CStringValue>(
        "plugin:hyproverview:exit_notification", "Exit notification mode: off or basic", "off");

    HyprlandAPI::addConfigValueV2(PHANDLE, g_cfgEnabled);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_cfgCancelOnWorkspaceSwitch);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_cfgClickSelect);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_cfgClickApply);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_cfgRightClickCancel);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_cfgWarpCursorOnExit);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_cfgRestoreOriginalOnInvalidSelection);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_cfgFitMarginFactor);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_cfgWheelSteps);
    HyprlandAPI::addConfigValueV2(PHANDLE, g_cfgExitNotification);
}

static void registerEventListeners() {
    g_windowActiveListener = Event::bus()->m_events.window.active.listen([](PHLWINDOW window, Desktop::eFocusReason) {
        if (!g_state.active)
            return;

        refreshOverviewAfterFocus(window);
    });

    g_workspaceListener = Event::bus()->m_events.workspace.active.listen([](PHLWORKSPACE workspace) {
        if (!g_state.active)
            return;

        const auto current = g_state.workspace.lock();
        if (workspace != current && g_config.cancelOnWorkspaceSwitch)
            leaveOverview(true);
    });

    g_windowOpenListener = Event::bus()->m_events.window.open.listen([](PHLWINDOW window) {
        if (!g_state.active || !isOverviewWindow(window))
            return;

        if (auto* scrolling = scrollingAlgorithmFor(window->m_workspace); scrolling)
            rebuildSnapshot(scrolling);
    });

    g_windowDestroyListener = Event::bus()->m_events.window.destroy.listen([](PHLWINDOW window) {
        if (!g_state.active || !isOverviewWindow(window))
            return;

        const auto workspace = g_state.workspace.lock();
        auto*      scrolling = scrollingAlgorithmFor(workspace);
        if (!workspace || !scrolling) {
            clearState();
            return;
        }

        if (g_state.selectedFocus.lock() == window || g_state.originalFocus.lock() == window)
            g_state.selectedFocus = isScrollingTargetWindow(Desktop::focusState()->window(), scrolling) ? Desktop::focusState()->window() : firstOverviewWindow(scrolling);

        rebuildSnapshot(scrolling);
    });

    g_windowMoveWorkspaceListener = Event::bus()->m_events.window.moveToWorkspace.listen([](PHLWINDOW window, PHLWORKSPACE workspace) {
        if (!g_state.active)
            return;

        const auto overviewWorkspace = g_state.workspace.lock();
        if (window == g_state.selectedFocus.lock() && workspace != overviewWorkspace)
            g_state.selectedFocus = g_state.originalFocus;

        if (window == g_state.originalFocus.lock() && workspace != overviewWorkspace)
            g_state.originalFocus = g_state.selectedFocus.lock() ? g_state.selectedFocus : firstOverviewWindow(scrollingAlgorithmFor(overviewWorkspace));

        if (auto* scrolling = scrollingAlgorithmFor(overviewWorkspace); scrolling)
            rebuildSnapshot(scrolling);
    });

    g_configPreReloadListener = Event::bus()->m_events.config.preReload.listen([]() {
        if (g_state.active)
            leaveOverview(true);
    });

    g_configReloadedListener = Event::bus()->m_events.config.reloaded.listen([]() {
        syncConfig();
    });

    g_mouseMoveListener = Event::bus()->m_events.input.mouse.move.listen([](Vector2D, Event::SCallbackInfo&) {
        if (!g_state.active || !g_config.clickSelect)
            return;

        if (const auto hovered = windowAtCursor(); isOverviewWindow(hovered))
            setSelectedWindow(hovered, false);
    });

    g_mouseButtonListener = Event::bus()->m_events.input.mouse.button.listen([](IPointer::SButtonEvent event, Event::SCallbackInfo& info) {
        if (!g_state.active || event.state != WL_POINTER_BUTTON_STATE_PRESSED)
            return;

        const auto hovered = windowAtCursor();
        const auto hoveredOverviewWindow = isOverviewWindow(hovered);

        if (event.button == kBtnLeft && g_config.clickSelect && hoveredOverviewWindow)
            setSelectedWindow(hovered, true);

        if (event.button == kBtnLeft && g_config.clickApply && hoveredOverviewWindow) {
            info.cancelled = true;
            leaveOverview(false);
            return;
        }

        if ((event.button == kBtnRight || event.button == kBtnMiddle) && g_config.rightClickCancel) {
            info.cancelled = true;
            leaveOverview(true);
        }
    });

    g_mouseAxisListener = Event::bus()->m_events.input.mouse.axis.listen([](IPointer::SAxisEvent event, Event::SCallbackInfo& info) {
        if (!g_state.active || event.delta == 0.0)
            return;

        auto direction = Math::DIRECTION_RIGHT;
        if (event.axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
            direction = event.delta > 0.0 ? Math::DIRECTION_LEFT : Math::DIRECTION_RIGHT;
        else
            direction = event.delta > 0.0 ? Math::DIRECTION_LEFT : Math::DIRECTION_RIGHT;

        bool moved = false;
        for (int i = 0; i < g_config.wheelSteps; ++i)
            moved = tryMoveSelection(direction) || moved;

        info.cancelled = moved;
    });

    g_keyboardKeyListener = Event::bus()->m_events.input.keyboard.key.listen([](IKeyboard::SKeyEvent event, Event::SCallbackInfo& info) {
        if (g_state.active) {
            handleOverviewKeys(event, info);
            return;
        }

        if (toggleTriggerMatches(event)) {
            info.cancelled = true;
            dispatchToggle("");
        }
    });
}

} // namespace

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string compositorHash = __hyprland_api_get_hash();
    const std::string clientHash     = __hyprland_api_get_client_hash();

    if (compositorHash != clientHash) {
        HyprlandAPI::addNotification(PHANDLE, "[hypr-overview] Header mismatch, refusing to load.", CHyprColor{1.F, 0.2F, 0.2F, 1.F}, 5000);
        throw std::runtime_error("[hypr-overview] Version mismatch");
    }

    registerConfigValues();
    syncConfig();
    registerEventListeners();

    HyprlandAPI::addDispatcherV2(PHANDLE, "hyproverview_toggle", dispatchToggle);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyproverview_apply", dispatchApply);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyproverview_cancel", dispatchCancel);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyproverview_status", dispatchStatus);

    if (Config::mgr() && Config::mgr()->type() == Config::CONFIG_LUA) {
        if (!HyprlandAPI::addLuaFunction(PHANDLE, "hyproverview", "setup", luaSetup) ||
            !HyprlandAPI::addLuaFunction(PHANDLE, "hyproverview", "toggle", luaToggle) ||
            !HyprlandAPI::addLuaFunction(PHANDLE, "hyproverview", "apply", luaApply) ||
            !HyprlandAPI::addLuaFunction(PHANDLE, "hyproverview", "cancel", luaCancel) ||
            !HyprlandAPI::addLuaFunction(PHANDLE, "hyproverview", "active", luaActive) ||
            !HyprlandAPI::addLuaFunction(PHANDLE, "hyproverview", "status", luaStatus)) {
            throw std::runtime_error("[hypr-overview] Failed to register Lua functions");
        }
    }

    return {"hypr-overview", "Overview mode for the Hyprland scrolling layout", "Joao Gabriel", "1.0.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    if (g_state.active)
        leaveOverview(true);

    g_windowActiveListener.reset();
    g_workspaceListener.reset();
    g_windowOpenListener.reset();
    g_windowDestroyListener.reset();
    g_windowMoveWorkspaceListener.reset();
    g_mouseMoveListener.reset();
    g_mouseButtonListener.reset();
    g_mouseAxisListener.reset();
    g_keyboardKeyListener.reset();
    g_configPreReloadListener.reset();
    g_configReloadedListener.reset();

    HyprlandAPI::removeDispatcher(PHANDLE, "hyproverview_toggle");
    HyprlandAPI::removeDispatcher(PHANDLE, "hyproverview_apply");
    HyprlandAPI::removeDispatcher(PHANDLE, "hyproverview_cancel");
    HyprlandAPI::removeDispatcher(PHANDLE, "hyproverview_status");
    HyprlandAPI::removeLuaFunction(PHANDLE, "hyproverview", "setup");
    HyprlandAPI::removeLuaFunction(PHANDLE, "hyproverview", "toggle");
    HyprlandAPI::removeLuaFunction(PHANDLE, "hyproverview", "apply");
    HyprlandAPI::removeLuaFunction(PHANDLE, "hyproverview", "cancel");
    HyprlandAPI::removeLuaFunction(PHANDLE, "hyproverview", "active");
    HyprlandAPI::removeLuaFunction(PHANDLE, "hyproverview", "status");
}
