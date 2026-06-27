#define WLR_USE_UNSTABLE

#include <hyprland/src/includes.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <expected>
#include <format>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#define private public
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/layout/algorithm/Algorithm.hpp>
#include <hyprland/src/layout/algorithm/tiled/scrolling/ScrollingAlgorithm.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <hyprland/src/managers/PointerManager.hpp>
#include <hyprland/src/managers/SeatManager.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>
#include <hyprland/src/render/types.hpp>
#undef private

inline HANDLE PHANDLE = nullptr;

namespace {

using Layout::Tiled::CScrollingAlgorithm;
using Layout::Tiled::SColumnData;

constexpr uint32_t kBtnLeft = 272;
constexpr uint32_t kBtnRight = 273;
constexpr uint32_t kBtnMiddle = 274;

struct SOverviewConfig {
  bool enabled = true;
  bool clickSelect = true;
  bool clickApply = true;
  bool rightClickCancel = true;
  int maxVisibleWorkspaces = 3;
  int padding = 30;
  int rowGap = 18;
  int columnGap = 10;
  int windowGap = 8;
  int rounding = 10;
  int animationDurationMs = 220;
  float backgroundOpacity = 0.94F;
  CHyprColor backgroundColor = CHyprColor{0xFF101018};
  CHyprColor rowColor = CHyprColor{0xFF242432};
  CHyprColor activeRowColor = CHyprColor{0xFF303044};
  CHyprColor selectionColor = CHyprColor{0xFF7AA2F7};
};

enum class EOverviewPhase {
  ENTERING,
  ACTIVE,
  EXITING,
};

struct SWindowThumbnail {
  PHLWINDOWREF window;
  PHLWORKSPACEREF workspace;
  CBox inputBox;
};

struct SWorkspaceThumbnail {
  PHLWORKSPACEREF workspace;
  CBox inputBox;
};

struct SOverviewState {
  bool active = false;
  EOverviewPhase phase = EOverviewPhase::ENTERING;
  Time::steady_tp phaseStartedAt = {};
  PHLMONITORREF monitor;
  PHLWORKSPACEREF originalWorkspace;
  PHLWINDOWREF originalFocus;
  PHLWINDOWREF selectedFocus;
  std::vector<SWindowThumbnail> windows;
  std::vector<SWorkspaceThumbnail> workspaces;
  std::string pendingExitReason = "inactive";
};

inline SOverviewConfig g_config;
inline SOverviewState g_state;
inline std::string g_lastExitReason = "not started";

inline SP<Config::Values::CBoolValue> g_cfgEnabled;
inline SP<Config::Values::CBoolValue> g_cfgClickSelect;
inline SP<Config::Values::CBoolValue> g_cfgClickApply;
inline SP<Config::Values::CBoolValue> g_cfgRightClickCancel;
inline SP<Config::Values::CIntValue> g_cfgMaxVisibleWorkspaces;
inline SP<Config::Values::CIntValue> g_cfgPadding;
inline SP<Config::Values::CIntValue> g_cfgRowGap;
inline SP<Config::Values::CIntValue> g_cfgColumnGap;
inline SP<Config::Values::CIntValue> g_cfgWindowGap;
inline SP<Config::Values::CIntValue> g_cfgRounding;
inline SP<Config::Values::CIntValue> g_cfgAnimationDurationMs;
inline SP<Config::Values::CFloatValue> g_cfgBackgroundOpacity;
inline SP<Config::Values::CColorValue> g_cfgBackgroundColor;
inline SP<Config::Values::CColorValue> g_cfgRowColor;
inline SP<Config::Values::CColorValue> g_cfgActiveRowColor;
inline SP<Config::Values::CColorValue> g_cfgSelectionColor;

inline CHyprSignalListener g_renderListener;
inline CHyprSignalListener g_workspaceListener;
inline CHyprSignalListener g_windowActiveListener;
inline CHyprSignalListener g_windowOpenListener;
inline CHyprSignalListener g_windowDestroyListener;
inline CHyprSignalListener g_windowMoveListener;
inline CHyprSignalListener g_monitorRemovedListener;
inline CHyprSignalListener g_mouseMoveListener;
inline CHyprSignalListener g_mouseButtonListener;
inline CHyprSignalListener g_mouseAxisListener;
inline CHyprSignalListener g_keyboardKeyListener;
inline CHyprSignalListener g_configReloadedListener;

static SDispatchResult ok() { return {}; }

static SDispatchResult fail(const std::string &error) {
  return {.passEvent = false, .success = false, .error = error};
}

static void syncConfig() {
  if (g_cfgEnabled)
    g_config.enabled = g_cfgEnabled->value();
  if (g_cfgClickSelect)
    g_config.clickSelect = g_cfgClickSelect->value();
  if (g_cfgClickApply)
    g_config.clickApply = g_cfgClickApply->value();
  if (g_cfgRightClickCancel)
    g_config.rightClickCancel = g_cfgRightClickCancel->value();
  if (g_cfgMaxVisibleWorkspaces)
    g_config.maxVisibleWorkspaces =
        std::clamp<int>(g_cfgMaxVisibleWorkspaces->value(), 1, 32);
  if (g_cfgPadding)
    g_config.padding = std::max<int>(0, g_cfgPadding->value());
  if (g_cfgRowGap)
    g_config.rowGap = std::max<int>(0, g_cfgRowGap->value());
  if (g_cfgColumnGap)
    g_config.columnGap = std::max<int>(0, g_cfgColumnGap->value());
  if (g_cfgWindowGap)
    g_config.windowGap = std::max<int>(0, g_cfgWindowGap->value());
  if (g_cfgRounding)
    g_config.rounding = std::max<int>(0, g_cfgRounding->value());
  if (g_cfgAnimationDurationMs)
    g_config.animationDurationMs =
        std::clamp<int>(g_cfgAnimationDurationMs->value(), 0, 2000);
  if (g_cfgBackgroundOpacity)
    g_config.backgroundOpacity =
        std::clamp(g_cfgBackgroundOpacity->value(), 0.F, 1.F);
  if (g_cfgBackgroundColor)
    g_config.backgroundColor =
        CHyprColor{static_cast<uint64_t>(g_cfgBackgroundColor->value())};
  if (g_cfgRowColor)
    g_config.rowColor =
        CHyprColor{static_cast<uint64_t>(g_cfgRowColor->value())};
  if (g_cfgActiveRowColor)
    g_config.activeRowColor =
        CHyprColor{static_cast<uint64_t>(g_cfgActiveRowColor->value())};
  if (g_cfgSelectionColor)
    g_config.selectionColor =
        CHyprColor{static_cast<uint64_t>(g_cfgSelectionColor->value())};
}

static PHLMONITOR overviewMonitor() { return g_state.monitor.lock(); }

static void damageOverview() {
  if (const auto monitor = overviewMonitor(); monitor) {
    g_pHyprRenderer->damageMonitor(monitor);
    g_pCompositor->scheduleFrameForMonitor(monitor);
  }
}

static void clearState(const std::string_view reason) {
  g_lastExitReason = reason;
  g_state = {};
}

static double animationDurationSeconds() {
  return static_cast<double>(g_config.animationDurationMs) / 1000.0;
}

static float easedProgress(float t) {
  const auto clamped = std::clamp(t, 0.F, 1.F);
  return 1.F - std::pow(1.F - clamped, 3.F);
}

static float phaseProgress(const Time::steady_tp &now) {
  if (!g_state.active)
    return 0.F;

  if (g_state.phase == EOverviewPhase::ACTIVE || g_config.animationDurationMs <= 0)
    return 1.F;

  const auto elapsed = std::chrono::duration<float>(now - g_state.phaseStartedAt)
                           .count();
  const auto duration = static_cast<float>(animationDurationSeconds());
  if (duration <= 0.F)
    return 1.F;

  return easedProgress(elapsed / duration);
}

static bool phaseFinished(const Time::steady_tp &now) {
  if (!g_state.active || g_state.phase == EOverviewPhase::ACTIVE ||
      g_config.animationDurationMs <= 0)
    return true;

  return std::chrono::duration<float>(now - g_state.phaseStartedAt).count() >=
         animationDurationSeconds();
}

static CBox lerpBox(const CBox &from, const CBox &to, float t) {
  return {{std::lerp(from.x, to.x, t), std::lerp(from.y, to.y, t)},
          {std::lerp(from.w, to.w, t), std::lerp(from.h, to.h, t)}};
}

static CHyprColor modulated(const CHyprColor &color, float alpha) {
  return color.modifyA(std::clamp(alpha, 0.F, 1.F) * color.a);
}

static CScrollingAlgorithm *
scrollingAlgorithmFor(const PHLWORKSPACE &workspace) {
  if (!workspace || !workspace->m_space)
    return nullptr;

  const auto algorithm = workspace->m_space->algorithm();
  if (!algorithm || !algorithm->tiledAlgo())
    return nullptr;

  return dynamic_cast<CScrollingAlgorithm *>(algorithm->tiledAlgo().get());
}

static std::vector<PHLWORKSPACE>
workspacesForMonitor(const PHLMONITOR &monitor) {
  std::vector<PHLWORKSPACE> result;
  if (!monitor)
    return result;

  for (const auto &workspaceRef : g_pCompositor->getWorkspaces()) {
    const auto workspace = workspaceRef.lock();
    if (!workspace || workspace->m_isSpecialWorkspace || workspace->m_id < 1)
      continue;
    if (workspace->m_monitor.lock() == monitor)
      result.emplace_back(workspace);
  }

  std::ranges::sort(result, {}, [](const PHLWORKSPACE &workspace) {
    return workspace->m_id;
  });
  return result;
}

static size_t workspaceIndex(const std::vector<PHLWORKSPACE> &workspaces,
                             const PHLWORKSPACE &workspace) {
  const auto it = std::ranges::find(workspaces, workspace);
  if (it == workspaces.end())
    return 0;
  return static_cast<size_t>(std::distance(workspaces.begin(), it));
}

static std::pair<size_t, size_t>
visibleWorkspaceRange(const std::vector<PHLWORKSPACE> &workspaces,
                      const PHLWORKSPACE &anchor) {
  if (workspaces.empty())
    return {0, 0};

  const auto visibleCount = std::min<size_t>(
      workspaces.size(), static_cast<size_t>(g_config.maxVisibleWorkspaces));
  const auto anchorIdx = workspaceIndex(workspaces, anchor);
  const auto preferredStart = anchorIdx >= visibleCount / 2
                                  ? anchorIdx - visibleCount / 2
                                  : 0;
  const auto maxStart = workspaces.size() - visibleCount;
  const auto start = std::min(preferredStart, maxStart);
  return {start, start + visibleCount};
}

static double workspaceWidthFactor(const PHLWORKSPACE &workspace) {
  if (auto *scrolling = scrollingAlgorithmFor(workspace);
      scrolling && scrolling->m_scrollingData) {
    const auto viewportWidth = scrolling->primaryViewportSize();
    const auto maxWidth = scrolling->m_scrollingData->maxWidth();
    if (viewportWidth > 0.0 && maxWidth > 0.0)
      return std::max(1.0, maxWidth / viewportWidth);
  }

  size_t tiledWindows = 0;
  for (const auto &window : g_pCompositor->m_windows) {
    if (window && window->m_workspace == workspace && window->m_isMapped &&
        !window->m_isFloating)
      ++tiledWindows;
  }

  return std::max<double>(1.0, static_cast<double>(tiledWindows));
}

static CBox workspaceOverviewBox(const PHLMONITOR &monitor, double padding,
                                 double y, double rowHeight,
                                 double widthFactor) {
  const auto availableWidth =
      std::max(1.0, monitor->m_transformedSize.x - padding * 2.0);
  const auto aspect =
      monitor->m_transformedSize.y > 0
          ? monitor->m_transformedSize.x / monitor->m_transformedSize.y
          : 1.0;
  const auto width =
      std::max(1.0, std::min(availableWidth, rowHeight * aspect * widthFactor));
  return {{(monitor->m_transformedSize.x - width) / 2.0, y}, {width, rowHeight}};
}

static void renderRect(const CBox &box, const CHyprColor &color,
                       const CBox &clip, int rounding = 0,
                       float alpha = 1.F) {
  CRectPassElement::SRectData data;
  data.box = box;
  data.color = modulated(color, alpha);
  data.round = rounding;
  data.roundingPower = 2.F;
  data.clipBox = clip;
  g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(data));
}

static CBox insetBox(CBox box, double inset) {
  const auto safeInset =
      std::max(0.0, std::min({inset, box.w / 2.0, box.h / 2.0}));
  box.x += safeInset;
  box.y += safeInset;
  box.w = std::max(1.0, box.w - safeInset * 2.0);
  box.h = std::max(1.0, box.h - safeInset * 2.0);
  return box;
}

static CBox fitBox(const CBox &bounds, double sourceWidth,
                   double sourceHeight) {
  if (sourceWidth <= 0.0 || sourceHeight <= 0.0 || bounds.w <= 0.0 ||
      bounds.h <= 0.0)
    return {};

  const auto scale = std::min(bounds.w / sourceWidth, bounds.h / sourceHeight);
  const auto width = sourceWidth * scale;
  const auto height = sourceHeight * scale;
  return {{bounds.x + (bounds.w - width) / 2.0,
           bounds.y + (bounds.h - height) / 2.0},
          {width, height}};
}

static void renderWindowPreview(const PHLWINDOW &window,
                                const PHLMONITOR &monitor, const CBox &bounds,
                                const CBox &clip, const Time::steady_tp &,
                                float alpha) {
  if (!window || !monitor || !window->m_isMapped || !window->wlSurface() ||
      !window->wlSurface()->resource())
    return;

  const auto rootSurface = window->wlSurface()->resource();
  const auto rootSize = rootSurface->m_current.size;
  if (rootSize.x <= 0 || rootSize.y <= 0)
    return;

  const auto target = fitBox(bounds, rootSize.x, rootSize.y);
  const auto scale = target.w / rootSize.x;
  rootSurface->breadthfirst(
      [&target, &clip, &window, &rootSurface, rootSize, alpha,
       scale](SP<CWLSurfaceResource> surface, const Vector2D &offset, void *) {
        if (!surface || !surface->m_current.texture ||
            surface->m_current.size.x <= 0 || surface->m_current.size.y <= 0)
          return;

        // Full-size synchronization subsurfaces are used differently by
        // clients: in Firefox they can hold the actual web view, while in
        // Chromium they can be an opaque staging surface over valid root
        // content. Blend these surfaces so either representation remains
        // visible in the preview.
        const auto fullCoverSubsurface =
            surface != rootSurface && std::abs(offset.x) < 1.0 &&
            std::abs(offset.y) < 1.0 &&
            surface->m_current.size.x >= rootSize.x * 0.95 &&
            surface->m_current.size.y >= rootSize.y * 0.95;

        CBox textureBox{
            target.pos() + offset * scale,
            surface->m_current.size * scale,
        };
        CTexPassElement::SRenderData data;
        data.tex = surface->m_current.texture;
        data.box = textureBox;
        data.a = (fullCoverSubsurface ? 0.55F : 1.F) * alpha;
        data.overallA = alpha;
        data.damage = CRegion{textureBox};
        data.round =
            surface == window->wlSurface()->resource()
                ? std::max(0, std::min(g_config.rounding - 2,
                                       static_cast<int>(
                                           std::min(target.w, target.h) / 4.0)))
                : 0;
        data.roundingPower = window->roundingPower();
        data.clipBox = clip;
        data.allowCustomUV = true;
        data.surface = surface;
        g_pHyprRenderer->m_renderPass.add(
            makeUnique<CTexPassElement>(std::move(data)));
      },
      nullptr);
}

static CBox toInputBox(CBox renderBox, const PHLMONITOR &monitor) {
  renderBox.scale(1.0 / monitor->m_scale);
  renderBox.x += monitor->m_position.x;
  renderBox.y += monitor->m_position.y;
  return renderBox;
}

static void addWindowThumbnail(const PHLWINDOW &window,
                               const PHLWORKSPACE &workspace,
                               const PHLMONITOR &monitor, const CBox &cell,
                               const CBox &row, const CBox &fullClip,
                               const Time::steady_tp &now, float alpha) {
  if (!window)
    return;

  const auto selected = window == g_state.selectedFocus.lock();
  if (selected)
    renderRect(cell, g_config.selectionColor, row, g_config.rounding, alpha);

  const auto inner =
      insetBox(cell, selected ? std::max(3.0, 3.0 * monitor->m_scale)
                              : std::max(1.0, 1.0 * monitor->m_scale));
  renderRect(inner, CHyprColor{0.055F, 0.055F, 0.075F, 1.F}, row,
             std::max(0, g_config.rounding - 2), alpha);
  renderWindowPreview(window, monitor, inner, row, now, alpha);

  g_state.windows.emplace_back(SWindowThumbnail{
      .window = window,
      .workspace = workspace,
      .inputBox = toInputBox(cell, monitor),
  });

  g_pHyprRenderer->m_renderData.clipBox = fullClip;
}

static void renderScrollingWorkspace(const PHLWORKSPACE &workspace,
                                     CScrollingAlgorithm *scrolling,
                                     const PHLMONITOR &monitor, const CBox &row,
                                     const CBox &fullClip,
                                     const Time::steady_tp &now, float alpha) {
  if (!scrolling || !scrolling->m_scrollingData ||
      scrolling->m_scrollingData->columns.empty())
    return;

  const auto &columns = scrolling->m_scrollingData->columns;
  double totalWidth = 0.0;
  for (const auto &column : columns)
    totalWidth += std::max(0.05F, column->getColumnWidth());
  if (totalWidth <= 0.0)
    return;

  const auto columnGap = g_config.columnGap * monitor->m_scale;
  const auto windowGap = g_config.windowGap * monitor->m_scale;
  const auto usableWidth = std::max(
      1.0, row.w - columnGap * static_cast<double>(columns.size() > 0
                                                       ? columns.size() - 1
                                                       : 0));

  double x = row.x;
  for (const auto &column : columns) {
    const auto columnWidth =
        usableWidth * std::max(0.05F, column->getColumnWidth()) / totalWidth;
    double totalHeight = 0.0;
    for (size_t i = 0; i < column->targetDatas.size(); ++i)
      totalHeight += std::max(0.01F, column->getTargetSize(i));

    if (totalHeight <= 0.0 || column->targetDatas.empty()) {
      x += columnWidth + columnGap;
      continue;
    }

    const auto usableHeight =
        std::max(1.0, row.h - windowGap * static_cast<double>(
                                              column->targetDatas.size() - 1));
    double y = row.y;
    for (size_t i = 0; i < column->targetDatas.size(); ++i) {
      const auto &targetData = column->targetDatas[i];
      const auto target = targetData->target.lock();
      const auto window = target ? target->window() : nullptr;
      const auto height = usableHeight *
                          std::max(0.01F, column->getTargetSize(i)) /
                          totalHeight;
      const CBox cell{{x, y}, {columnWidth, height}};
      addWindowThumbnail(window, workspace, monitor, cell, row, fullClip, now,
                         alpha);
      y += height + windowGap;
    }

    x += columnWidth + columnGap;
  }
}

static void renderFallbackWorkspace(const PHLWORKSPACE &workspace,
                                    const PHLMONITOR &monitor, const CBox &row,
                                    const CBox &fullClip,
                                    const Time::steady_tp &now, float alpha) {
  std::vector<PHLWINDOW> windows;
  for (const auto &window : g_pCompositor->m_windows) {
    if (window && window->m_workspace == workspace && window->m_isMapped &&
        !window->m_isFloating)
      windows.emplace_back(window);
  }

  if (windows.empty())
    return;

  const auto gap = g_config.columnGap * monitor->m_scale;
  const auto width =
      std::max(1.0, (row.w - gap * static_cast<double>(windows.size() - 1)) /
                        static_cast<double>(windows.size()));
  double x = row.x;
  for (const auto &window : windows) {
    const CBox cell{{x, row.y}, {width, row.h}};
    addWindowThumbnail(window, workspace, monitor, cell, row, fullClip, now,
                       alpha);
    x += width + gap;
  }
}

static void renderOverview() {
  if (!g_state.active)
    return;

  const auto monitor = overviewMonitor();
  if (!monitor || g_pHyprRenderer->m_renderData.pMonitor != monitor) {
    if (!monitor)
      clearState("monitor reference expired while rendering");
    return;
  }

  const auto workspaces = workspacesForMonitor(monitor);
  if (workspaces.empty())
    return;

  const CBox fullClip{{0, 0}, monitor->m_transformedSize};
  const auto now = Time::steadyNow();
  const auto progress = phaseProgress(now);
  const auto visibleProgress =
      g_state.phase == EOverviewPhase::EXITING ? 1.F - progress : progress;
  const auto background =
      g_config.backgroundColor.modifyA(g_config.backgroundOpacity);
  renderRect(fullClip, background, fullClip, 0, visibleProgress);

  g_state.windows.clear();
  g_state.workspaces.clear();

  const auto padding = g_config.padding * monitor->m_scale;
  const auto rowGap = g_config.rowGap * monitor->m_scale;
  const auto activeWorkspace = monitor->m_activeSpecialWorkspace
                                   ? monitor->m_activeSpecialWorkspace
                                   : monitor->m_activeWorkspace;
  const auto visibleRange = visibleWorkspaceRange(workspaces, activeWorkspace);
  const auto visibleCount =
      std::max<size_t>(1, visibleRange.second - visibleRange.first);
  const auto availableHeight =
      std::max(1.0, fullClip.h - padding * 2.0 -
                        rowGap * static_cast<double>(visibleCount - 1));
  const auto rowHeight =
      std::max(1.0, availableHeight / static_cast<double>(visibleCount));
  const auto activeWidthFactor = workspaceWidthFactor(activeWorkspace);
  const auto sourceBox =
      workspaceOverviewBox(monitor, padding, padding, rowHeight, activeWidthFactor);
  const auto hiddenBox =
      insetBox(sourceBox, std::min(sourceBox.w, sourceBox.h) * 0.2);

  double y = padding;
  for (size_t workspacePos = visibleRange.first; workspacePos < visibleRange.second;
       ++workspacePos) {
    const auto &workspace = workspaces[workspacePos];
    const auto targetRow = workspaceOverviewBox(
        monitor, padding, y, rowHeight, workspaceWidthFactor(workspace));
    const auto startRow = workspace == activeWorkspace ? fullClip : hiddenBox;
    const auto outerRow = g_state.phase == EOverviewPhase::EXITING
                              ? targetRow
                              : lerpBox(startRow, targetRow, progress);
    const auto active = workspace == activeWorkspace;
    renderRect(outerRow, active ? g_config.selectionColor : g_config.rowColor,
               fullClip, g_config.rounding, visibleProgress);

    const auto border = active ? std::max(3.0, 3.0 * monitor->m_scale)
                               : std::max(1.0, 1.0 * monitor->m_scale);
    const auto row = insetBox(outerRow, border);
    renderRect(row, active ? g_config.activeRowColor : g_config.rowColor,
               outerRow, std::max(0, g_config.rounding - 2), visibleProgress);

    g_state.workspaces.emplace_back(SWorkspaceThumbnail{
        .workspace = workspace,
        .inputBox = toInputBox(outerRow, monitor),
    });

    if (auto *scrolling = scrollingAlgorithmFor(workspace); scrolling)
      renderScrollingWorkspace(
          workspace, scrolling, monitor,
          insetBox(row, std::max(4.0, 4.0 * monitor->m_scale)), fullClip, now,
          visibleProgress);
    else
      renderFallbackWorkspace(
          workspace, monitor,
          insetBox(row, std::max(4.0, 4.0 * monitor->m_scale)), fullClip, now,
          visibleProgress);

    y += rowHeight + rowGap;
  }

  g_pHyprRenderer->m_renderData.clipBox = fullClip;

  if (g_state.phase == EOverviewPhase::ENTERING && phaseFinished(now))
    g_state.phase = EOverviewPhase::ACTIVE;
  else if (g_state.phase == EOverviewPhase::EXITING && phaseFinished(now)) {
    const auto exitReason = g_state.pendingExitReason;
    clearState(exitReason);
    return;
  }

  if (g_state.active && g_state.phase != EOverviewPhase::ACTIVE)
    damageOverview();
}

static PHLWINDOW windowAtOverviewPoint(const Vector2D &point) {
  for (auto it = g_state.windows.rbegin(); it != g_state.windows.rend(); ++it) {
    if (it->inputBox.containsPoint(point))
      return it->window.lock();
  }
  return nullptr;
}

static PHLWORKSPACE workspaceAtOverviewPoint(const Vector2D &point) {
  for (const auto &item : g_state.workspaces) {
    if (item.inputBox.containsPoint(point))
      return item.workspace.lock();
  }
  return nullptr;
}

static void focusOverviewTarget(const PHLWORKSPACE &workspace,
                                const PHLWINDOW &window) {
  const auto monitor = overviewMonitor();
  if (!monitor || !workspace)
    return;

  if (monitor->m_activeWorkspace != workspace)
    monitor->changeWorkspace(workspace, false, true, false);
  if (window) [[maybe_unused]]
    const auto focusResult = Config::Actions::focus(window);
}

static void leaveOverview(bool restoreOriginal) {
  if (!g_state.active)
    return;

  const auto monitor = overviewMonitor();
  const auto workspace = restoreOriginal
                             ? g_state.originalWorkspace.lock()
                             : (g_state.selectedFocus.lock()
                                    ? g_state.selectedFocus.lock()->m_workspace
                                : monitor ? monitor->m_activeWorkspace
                                          : nullptr);
  const auto window = restoreOriginal ? g_state.originalFocus.lock()
                                      : g_state.selectedFocus.lock();

  if (monitor && workspace)
    focusOverviewTarget(workspace, window);

  g_state.phase = EOverviewPhase::EXITING;
  g_state.phaseStartedAt = Time::steadyNow();
  g_state.pendingExitReason = restoreOriginal ? "canceled" : "applied";
  damageOverview();
}

static SDispatchResult enterOverview() {
  syncConfig();
  if (!g_config.enabled)
    return fail("Overview is disabled");

  const auto monitor = Desktop::focusState()->monitor();
  if (!monitor)
    return fail("No focused monitor");

  const auto workspaces = workspacesForMonitor(monitor);
  if (workspaces.empty())
    return fail("No workspaces to show");

  clearState("entering");
  g_state.active = true;
  g_state.phase = EOverviewPhase::ENTERING;
  g_state.phaseStartedAt = Time::steadyNow();
  g_state.monitor = monitor;
  g_state.originalWorkspace = monitor->m_activeWorkspace;
  g_state.originalFocus = Desktop::focusState()->window();
  g_state.selectedFocus = Desktop::focusState()->window();
  g_state.pendingExitReason = "applied";
  damageOverview();
  return ok();
}

static SDispatchResult dispatchToggle(const std::string &) {
  if (g_state.active) {
    leaveOverview(false);
    return ok();
  }

  const auto result = enterOverview();
  if (!result.success)
    HyprlandAPI::addNotification(PHANDLE, "[hypr-overview] " + result.error,
                                 CHyprColor{1.F, 0.2F, 0.2F, 1.F}, 3500);
  return result;
}

static SDispatchResult dispatchApply(const std::string &) {
  if (!g_state.active)
    return fail("Overview is not active");
  leaveOverview(false);
  return ok();
}

static SDispatchResult dispatchCancel(const std::string &) {
  if (!g_state.active)
    return fail("Overview is not active");
  leaveOverview(true);
  return ok();
}

static bool directionCandidate(const Vector2D &from, const Vector2D &to,
                               Math::eDirection direction, double &primary,
                               double &secondary) {
  const auto dx = to.x - from.x;
  const auto dy = to.y - from.y;

  switch (direction) {
  case Math::DIRECTION_LEFT:
    primary = -dx;
    secondary = std::abs(dy);
    return dx < -1.0;
  case Math::DIRECTION_RIGHT:
    primary = dx;
    secondary = std::abs(dy);
    return dx > 1.0;
  case Math::DIRECTION_UP:
    primary = -dy;
    secondary = std::abs(dx);
    return dy < -1.0;
  case Math::DIRECTION_DOWN:
    primary = dy;
    secondary = std::abs(dx);
    return dy > 1.0;
  default:
    return false;
  }
}

static SDispatchResult dispatchMove(const std::string &direction) {
  if (!g_state.active)
    return fail("Overview is not active");
  if (g_state.windows.empty())
    return fail("Overview has not rendered yet");

  Math::eDirection parsed;
  if (direction == "left" || direction == "l")
    parsed = Math::DIRECTION_LEFT;
  else if (direction == "right" || direction == "r")
    parsed = Math::DIRECTION_RIGHT;
  else if (direction == "up" || direction == "u")
    parsed = Math::DIRECTION_UP;
  else if (direction == "down" || direction == "d")
    parsed = Math::DIRECTION_DOWN;
  else
    return fail("Expected move direction: left, right, up, or down");

  const auto current = g_state.selectedFocus.lock();
  Vector2D origin;
  bool foundOrigin = false;
  for (const auto &item : g_state.windows) {
    if (item.window.lock() == current) {
      origin = item.inputBox.middle();
      foundOrigin = true;
      break;
    }
  }
  if (!foundOrigin)
    origin = g_state.windows.front().inputBox.middle();

  PHLWINDOW best;
  double bestScore = std::numeric_limits<double>::infinity();
  for (const auto &item : g_state.windows) {
    const auto window = item.window.lock();
    if (!window || window == current)
      continue;

    double primary = 0.0;
    double secondary = 0.0;
    if (!directionCandidate(origin, item.inputBox.middle(), parsed, primary,
                            secondary))
      continue;

    const auto score = primary + secondary * 2.5;
    if (score < bestScore) {
      bestScore = score;
      best = window;
    }
  }

  if (!best)
    return fail("No overview window in that direction");

  g_state.selectedFocus = best;
  damageOverview();
  return ok();
}

static SDispatchResult dispatchStatus(const std::string &) {
  return {.passEvent = false,
          .success = true,
          .error = g_state.active ? "active" : "inactive"};
}

static int pushActionResult(lua_State *state, const SDispatchResult &result) {
  lua_pushboolean(state, result.success);
  if (!result.success) {
    lua_pushstring(state, result.error.c_str());
    return 2;
  }
  return 1;
}

static int luaToggle(lua_State *state) {
  return pushActionResult(state, dispatchToggle(""));
}

static int luaApply(lua_State *state) {
  return pushActionResult(state, dispatchApply(""));
}

static int luaCancel(lua_State *state) {
  return pushActionResult(state, dispatchCancel(""));
}

static int luaMove(lua_State *state) {
  return pushActionResult(state, dispatchMove(luaL_checkstring(state, 1)));
}

static int luaActive(lua_State *state) {
  lua_pushboolean(state, g_state.active);
  return 1;
}

static int luaStatus(lua_State *state) {
  lua_newtable(state);
  lua_pushboolean(state, g_state.active);
  lua_setfield(state, -2, "active");
  lua_pushinteger(state, static_cast<lua_Integer>(g_state.workspaces.size()));
  lua_setfield(state, -2, "workspace_count");
  lua_pushinteger(state, static_cast<lua_Integer>(g_state.windows.size()));
  lua_setfield(state, -2, "window_count");
  lua_pushstring(state, g_lastExitReason.c_str());
  lua_setfield(state, -2, "last_exit_reason");
  const auto selected = g_state.selectedFocus.lock();
  lua_pushstring(
      state, selected ? std::format("0x{:x}",
                                    reinterpret_cast<uintptr_t>(selected.get()))
                            .c_str()
                      : "");
  lua_setfield(state, -2, "selected_window");
  return 1;
}

static void registerConfigValues() {
  g_cfgEnabled = makeShared<Config::Values::CBoolValue>(
      "plugin:hyproverview:enabled", "Enable hypr-overview", true);
  g_cfgClickSelect = makeShared<Config::Values::CBoolValue>(
      "plugin:hyproverview:click_select", "Select window thumbnails on hover",
      true);
  g_cfgClickApply = makeShared<Config::Values::CBoolValue>(
      "plugin:hyproverview:click_apply",
      "Apply a window thumbnail on left click", true);
  g_cfgRightClickCancel = makeShared<Config::Values::CBoolValue>(
      "plugin:hyproverview:right_click_cancel",
      "Cancel overview on right click", true);
  g_cfgMaxVisibleWorkspaces = makeShared<Config::Values::CIntValue>(
      "plugin:hyproverview:max_visible_workspaces",
      "Maximum workspace rows visible before vertical scrolling", 3,
      Config::Values::SIntValueOptions{.min = 1, .max = 32});
  g_cfgPadding = makeShared<Config::Values::CIntValue>(
      "plugin:hyproverview:padding", "Outer overview padding", 30,
      Config::Values::SIntValueOptions{.min = 0, .max = 300});
  g_cfgRowGap = makeShared<Config::Values::CIntValue>(
      "plugin:hyproverview:row_gap", "Gap between workspace rows", 18,
      Config::Values::SIntValueOptions{.min = 0, .max = 200});
  g_cfgColumnGap = makeShared<Config::Values::CIntValue>(
      "plugin:hyproverview:column_gap", "Gap between scrolling columns", 10,
      Config::Values::SIntValueOptions{.min = 0, .max = 100});
  g_cfgWindowGap = makeShared<Config::Values::CIntValue>(
      "plugin:hyproverview:window_gap", "Gap between windows in a column", 8,
      Config::Values::SIntValueOptions{.min = 0, .max = 100});
  g_cfgRounding = makeShared<Config::Values::CIntValue>(
      "plugin:hyproverview:rounding", "Overview card rounding", 10,
      Config::Values::SIntValueOptions{.min = 0, .max = 100});
  g_cfgAnimationDurationMs = makeShared<Config::Values::CIntValue>(
      "plugin:hyproverview:animation_duration_ms",
      "Overview zoom animation duration in milliseconds", 220,
      Config::Values::SIntValueOptions{.min = 0, .max = 2000});
  g_cfgBackgroundOpacity = makeShared<Config::Values::CFloatValue>(
      "plugin:hyproverview:background_opacity", "Overview backdrop opacity",
      0.94F, Config::Values::SFloatValueOptions{.min = 0.F, .max = 1.F});
  g_cfgBackgroundColor = makeShared<Config::Values::CColorValue>(
      "plugin:hyproverview:background_color", "Overview backdrop color",
      0xFF101018);
  g_cfgRowColor = makeShared<Config::Values::CColorValue>(
      "plugin:hyproverview:row_color", "Inactive workspace row color",
      0xFF242432);
  g_cfgActiveRowColor = makeShared<Config::Values::CColorValue>(
      "plugin:hyproverview:active_row_color", "Active workspace row color",
      0xFF303044);
  g_cfgSelectionColor = makeShared<Config::Values::CColorValue>(
      "plugin:hyproverview:selection_color",
      "Selection and active workspace color", 0xFF7AA2F7);

  HyprlandAPI::addConfigValueV2(PHANDLE, g_cfgEnabled);
  HyprlandAPI::addConfigValueV2(PHANDLE, g_cfgClickSelect);
  HyprlandAPI::addConfigValueV2(PHANDLE, g_cfgClickApply);
  HyprlandAPI::addConfigValueV2(PHANDLE, g_cfgRightClickCancel);
  HyprlandAPI::addConfigValueV2(PHANDLE, g_cfgMaxVisibleWorkspaces);
  HyprlandAPI::addConfigValueV2(PHANDLE, g_cfgPadding);
  HyprlandAPI::addConfigValueV2(PHANDLE, g_cfgRowGap);
  HyprlandAPI::addConfigValueV2(PHANDLE, g_cfgColumnGap);
  HyprlandAPI::addConfigValueV2(PHANDLE, g_cfgWindowGap);
  HyprlandAPI::addConfigValueV2(PHANDLE, g_cfgRounding);
  HyprlandAPI::addConfigValueV2(PHANDLE, g_cfgAnimationDurationMs);
  HyprlandAPI::addConfigValueV2(PHANDLE, g_cfgBackgroundOpacity);
  HyprlandAPI::addConfigValueV2(PHANDLE, g_cfgBackgroundColor);
  HyprlandAPI::addConfigValueV2(PHANDLE, g_cfgRowColor);
  HyprlandAPI::addConfigValueV2(PHANDLE, g_cfgActiveRowColor);
  HyprlandAPI::addConfigValueV2(PHANDLE, g_cfgSelectionColor);
}

static void registerEventListeners() {
  g_renderListener =
      Event::bus()->m_events.render.stage.listen([](eRenderStage stage) {
        if (stage == RENDER_POST_WINDOWS)
          renderOverview();
      });

  g_workspaceListener = Event::bus()->m_events.workspace.active.listen(
      [](PHLWORKSPACE workspace) {
        if (!g_state.active)
          return;
        if (const auto monitor = overviewMonitor();
            !monitor || !workspace || workspace->m_monitor.lock() != monitor)
          return;
        if (const auto focused = Desktop::focusState()->window();
            focused && focused->m_workspace == workspace)
          g_state.selectedFocus = focused;
        damageOverview();
      });

  g_windowActiveListener = Event::bus()->m_events.window.active.listen(
      [](PHLWINDOW window, Desktop::eFocusReason) {
        if (!g_state.active || !window)
          return;
        if (const auto monitor = overviewMonitor();
            monitor && window->m_workspace &&
            window->m_workspace->m_monitor.lock() == monitor) {
          g_state.selectedFocus = window;
          damageOverview();
        }
      });

  const auto damageOnWindowChange = [](PHLWINDOW) {
    if (g_state.active)
      damageOverview();
  };
  g_windowOpenListener =
      Event::bus()->m_events.window.open.listen(damageOnWindowChange);
  g_windowDestroyListener =
      Event::bus()->m_events.window.destroy.listen(damageOnWindowChange);
  g_windowMoveListener = Event::bus()->m_events.window.moveToWorkspace.listen(
      [](PHLWINDOW, PHLWORKSPACE) {
        if (g_state.active)
          damageOverview();
      });

  g_monitorRemovedListener =
      Event::bus()->m_events.monitor.removed.listen([](PHLMONITOR monitor) {
        if (g_state.active && monitor == overviewMonitor())
          clearState("monitor removed");
      });

  g_mouseMoveListener = Event::bus()->m_events.input.mouse.move.listen(
      [](Vector2D, Event::SCallbackInfo &) {
        if (!g_state.active || !g_config.clickSelect)
          return;
        const auto point = g_pPointerManager->position();
        if (const auto hovered = windowAtOverviewPoint(point);
            hovered && hovered != g_state.selectedFocus.lock()) {
          g_state.selectedFocus = hovered;
          damageOverview();
        }
      });

  g_mouseButtonListener = Event::bus()->m_events.input.mouse.button.listen(
      [](IPointer::SButtonEvent event, Event::SCallbackInfo &info) {
        if (!g_state.active)
          return;

        info.cancelled = true;
        if (event.state != WL_POINTER_BUTTON_STATE_PRESSED)
          return;

        if ((event.button == kBtnRight || event.button == kBtnMiddle) &&
            g_config.rightClickCancel) {
          leaveOverview(true);
          return;
        }
        if (event.button != kBtnLeft)
          return;

        const auto point = g_pPointerManager->position();
        if (const auto window = windowAtOverviewPoint(point); window) {
          g_state.selectedFocus = window;
          if (g_config.clickApply)
            leaveOverview(false);
          else
            damageOverview();
          return;
        }

        if (const auto workspace = workspaceAtOverviewPoint(point); workspace) {
          if (const auto monitor = overviewMonitor(); monitor)
            monitor->changeWorkspace(workspace, false, true, false);
          g_state.selectedFocus = workspace->getLastFocusedWindow();
          leaveOverview(false);
        }
      });

  g_mouseAxisListener = Event::bus()->m_events.input.mouse.axis.listen(
      [](IPointer::SAxisEvent event, Event::SCallbackInfo &info) {
        if (!g_state.active || event.delta == 0.0)
          return;

        info.cancelled = true;
        const auto monitor = overviewMonitor();
        if (!monitor)
          return;
        const auto workspaces = workspacesForMonitor(monitor);
        const auto active = monitor->m_activeWorkspace;
        const auto current = std::ranges::find(workspaces, active);
        if (current == workspaces.end())
          return;

        auto target = current;
        if (event.delta > 0.0) {
          if (target == workspaces.begin())
            return;
          --target;
        } else {
          ++target;
          if (target == workspaces.end())
            return;
        }
        monitor->changeWorkspace(*target, false, true, false);
        if (const auto last = (*target)->getLastFocusedWindow(); last)
          g_state.selectedFocus = last;
        damageOverview();
      });

  g_keyboardKeyListener = Event::bus()->m_events.input.keyboard.key.listen(
      [](IKeyboard::SKeyEvent event, Event::SCallbackInfo &info) {
        if (!g_state.active || g_state.phase == EOverviewPhase::EXITING ||
            event.state != WL_KEYBOARD_KEY_STATE_PRESSED)
          return;

        const auto keyboard = g_pSeatManager ? g_pSeatManager->m_keyboard : nullptr;
        if (!keyboard || !keyboard->m_xkbState)
          return;

        const auto keysym =
            xkb_state_key_get_one_sym(keyboard->m_xkbState, event.keycode + 8);
        if (keysym != XKB_KEY_Return && keysym != XKB_KEY_KP_Enter)
          return;

        info.cancelled = true;
        leaveOverview(false);
      });

  g_configReloadedListener =
      Event::bus()->m_events.config.reloaded.listen([]() {
        syncConfig();
        if (g_state.active)
          damageOverview();
      });
}

} // namespace

APICALL EXPORT std::string PLUGIN_API_VERSION() { return HYPRLAND_API_VERSION; }

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
  PHANDLE = handle;

  if (std::string{__hyprland_api_get_hash()} !=
      __hyprland_api_get_client_hash()) {
    HyprlandAPI::addNotification(
        PHANDLE, "[hypr-overview] Header mismatch, refusing to load.",
        CHyprColor{1.F, 0.2F, 0.2F, 1.F}, 5000);
    throw std::runtime_error("[hypr-overview] Version mismatch");
  }

  registerConfigValues();
  syncConfig();
  registerEventListeners();

  HyprlandAPI::addDispatcherV2(PHANDLE, "hyproverview_toggle", dispatchToggle);
  HyprlandAPI::addDispatcherV2(PHANDLE, "hyproverview_apply", dispatchApply);
  HyprlandAPI::addDispatcherV2(PHANDLE, "hyproverview_cancel", dispatchCancel);
  HyprlandAPI::addDispatcherV2(PHANDLE, "hyproverview_move", dispatchMove);
  HyprlandAPI::addDispatcherV2(PHANDLE, "hyproverview_status", dispatchStatus);

  if (Config::mgr() && Config::mgr()->type() == Config::CONFIG_LUA) {
    if (!HyprlandAPI::addLuaFunction(PHANDLE, "hyproverview", "toggle",
                                     luaToggle) ||
        !HyprlandAPI::addLuaFunction(PHANDLE, "hyproverview", "apply",
                                     luaApply) ||
        !HyprlandAPI::addLuaFunction(PHANDLE, "hyproverview", "cancel",
                                     luaCancel) ||
        !HyprlandAPI::addLuaFunction(PHANDLE, "hyproverview", "move",
                                     luaMove) ||
        !HyprlandAPI::addLuaFunction(PHANDLE, "hyproverview", "active",
                                     luaActive) ||
        !HyprlandAPI::addLuaFunction(PHANDLE, "hyproverview", "status",
                                     luaStatus))
      throw std::runtime_error(
          "[hypr-overview] Failed to register Lua functions");
  }

  return {"hypr-overview", "Niri-style workspace and scrolling-column overview",
          "Joao Gabriel", "2.1.1"};
}

APICALL EXPORT void PLUGIN_EXIT() {
  if (g_state.active)
    leaveOverview(false);

  g_renderListener.reset();
  g_workspaceListener.reset();
  g_windowActiveListener.reset();
  g_windowOpenListener.reset();
  g_windowDestroyListener.reset();
  g_windowMoveListener.reset();
  g_monitorRemovedListener.reset();
  g_mouseMoveListener.reset();
  g_mouseButtonListener.reset();
  g_mouseAxisListener.reset();
  g_keyboardKeyListener.reset();
  g_configReloadedListener.reset();

  HyprlandAPI::removeDispatcher(PHANDLE, "hyproverview_toggle");
  HyprlandAPI::removeDispatcher(PHANDLE, "hyproverview_apply");
  HyprlandAPI::removeDispatcher(PHANDLE, "hyproverview_cancel");
  HyprlandAPI::removeDispatcher(PHANDLE, "hyproverview_move");
  HyprlandAPI::removeDispatcher(PHANDLE, "hyproverview_status");
  HyprlandAPI::removeLuaFunction(PHANDLE, "hyproverview", "toggle");
  HyprlandAPI::removeLuaFunction(PHANDLE, "hyproverview", "apply");
  HyprlandAPI::removeLuaFunction(PHANDLE, "hyproverview", "cancel");
  HyprlandAPI::removeLuaFunction(PHANDLE, "hyproverview", "move");
  HyprlandAPI::removeLuaFunction(PHANDLE, "hyproverview", "active");
  HyprlandAPI::removeLuaFunction(PHANDLE, "hyproverview", "status");
}
