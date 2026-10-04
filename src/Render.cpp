#include "Globals.hpp"
#include "Overview.hpp"
#include <algorithm>
#include <climits>
#include <hyprland/src/config/shared/complex/ComplexDataTypes.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/render/pass/BorderPassElement.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprland/src/render/pass/RendererHintsPassElement.hpp>
#include <hyprland/src/render/pass/SurfacePassElement.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>
#include <hyprlang.hpp>
#include <hyprutils/utils/ScopeGuard.hpp>

void renderRect(CBox box, CHyprColor color) {
  CRectPassElement::SRectData rectdata;
  rectdata.color = color;
  rectdata.box = box;
  g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(rectdata));
}

void renderRectWithBlur(CBox box, CHyprColor color) {
  CRectPassElement::SRectData rectdata;
  rectdata.color = color;
  rectdata.box = box;
  rectdata.blur = true;
  g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(rectdata));
}

void renderBorder(CBox box, const Config::CGradientValueData &gradient,
                  int size) {
  CBorderPassElement::SBorderData data;
  data.box = box;
  data.grad1 = gradient;
  data.round = 0;
  data.a = 1.f;
  data.borderSize = size;
  g_pHyprRenderer->m_renderPass.add(makeUnique<CBorderPassElement>(data));
}

// aspect-fit ("contain"): uniformly scale the box down so it fits inside bounds
// (never up), then shift it so it lies completely inside bounds. Aspect ratio
// is always preserved.
static CBox containBox(CBox box, const CBox &bounds) {
  if (!(box.w > 0 && box.h > 0) || !(bounds.w > 0 && bounds.h > 0))
    return box;

  const double s = std::min({1.0, bounds.w / box.w, bounds.h / box.h});
  box.w *= s;
  box.h *= s;
  box.x = std::clamp(box.x, bounds.x, bounds.x + bounds.w - box.w);
  box.y = std::clamp(box.y, bounds.y, bounds.y + bounds.h - box.h);
  return box;
}

void renderWindowStub(PHLWINDOW pWindow, PHLMONITOR pMonitor,
                      PHLWORKSPACE pWorkspaceOverride, CBox rectOverride,
                      CBox clipBox, const Time::steady_tp &time) {
  if (!pWindow || !pMonitor || !pWorkspaceOverride)
    return;
  if (!pWindow->m_isMapped || !pWindow->wlSurface() ||
      !pWindow->wlSurface()->resource())
    return;

  // fit the preview completely inside the workspace tile instead of cropping it
  if (config.fitWindows->value())
    rectOverride = containBox(rectOverride, clipBox);

  Render::SRenderModifData renderModif;

  const auto oRealPosition =
      pWindow->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
  const auto oSize =
      pWindow->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
  const float logicalW = std::max((float)oSize.x, 5.F);
  const float scaleMod =
      rectOverride.w / std::max(logicalW * pMonitor->m_scale, 5.F);
  if (!(scaleMod > 0.F) || !(rectOverride.w > 0 && rectOverride.h > 0))
    return;

  const Vector2D logicalTL = oRealPosition + pWindow->m_floatingOffset;
  const Vector2D scaledTL =
      (logicalTL - pMonitor->m_position) * pMonitor->m_scale;
  const Vector2D translate = rectOverride.pos() / scaleMod - scaledTL;

  renderModif.modifs.push_back(std::make_pair(
      Render::SRenderModifData::eRenderModifType::RMOD_TYPE_TRANSLATE,
      std::any(translate)));
  renderModif.modifs.push_back(std::make_pair(
      Render::SRenderModifData::eRenderModifType::RMOD_TYPE_SCALE,
      std::any(scaleMod)));
  renderModif.enabled = true;

  g_pHyprRenderer->m_renderPass.add(makeUnique<CRendererHintsPassElement>(
      CRendererHintsPassElement::SData{.renderModif = renderModif}));
  Hyprutils::Utils::CScopeGuard x([] {
    g_pHyprRenderer->m_renderPass.add(
        makeUnique<CRendererHintsPassElement>(CRendererHintsPassElement::SData{
            .renderModif = Render::SRenderModifData{}}));
  });

  g_pHyprRenderer->damageWindow(pWindow);

  CSurfacePassElement::SRenderData renderdata = {pMonitor, time};
  renderdata.pos = oRealPosition + pWindow->m_floatingOffset;
  renderdata.w = std::max(oSize.x, 5.0);
  renderdata.h = std::max(oSize.y, 5.0);
  renderdata.surface = pWindow->wlSurface()->resource();
  renderdata.dontRound = Fullscreen::controller()->isFullscreen(
      pWindow, Fullscreen::FSMODE_FULLSCREEN);
  renderdata.fadeAlpha = 1.F;
  renderdata.alpha = 0.999F;
  renderdata.decorate = false;
  renderdata.rounding =
      renderdata.dontRound ? 0
                           : pWindow->rounding() * scaleMod * pMonitor->m_scale;
  renderdata.roundingPower =
      renderdata.dontRound ? 2.0F : pWindow->roundingPower();
  renderdata.blur = false;
  renderdata.pWindow = pWindow;
  renderdata.clipBox = clipBox;
  renderdata.useNearestNeighbor = false;
  renderdata.squishOversized = true;
  renderdata.surfaceCounter = 0;

  pWindow->wlSurface()->resource()->breadthfirst(
      [&renderdata, &pWindow](SP<CWLSurfaceResource> s, const Vector2D &offset,
                              void *data) {
        if (!s || !s->m_current.texture)
          return;

        if (s->m_current.size.x < 1 || s->m_current.size.y < 1)
          return;

        renderdata.localPos = offset;
        renderdata.texture = s->m_current.texture;
        renderdata.surface = s;
        renderdata.mainSurface = s == pWindow->wlSurface()->resource();
        g_pHyprRenderer->m_renderPass.add(
            makeUnique<CSurfacePassElement>(renderdata));
        renderdata.surfaceCounter++;
      },
      nullptr);
}

void renderLayerStub(PHLLS pLayer, PHLMONITOR pMonitor, CBox rectOverride,
                     CBox clipBox, const Time::steady_tp &time) {
  if (!pLayer || !pMonitor)
    return;

  if (!pLayer->m_mapped || !pLayer->m_layerSurface || !pLayer->wlSurface() ||
      !pLayer->wlSurface()->resource())
    return;

  Vector2D oRealPosition =
      pLayer->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
  Vector2D oSize = pLayer->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT);

  const float curScaling = rectOverride.w / (oSize.x);
  if (!(curScaling > 0.F) || !(rectOverride.w > 0 && rectOverride.h > 0))
    return;

  Render::SRenderModifData renderModif;

  renderModif.modifs.push_back(std::make_pair(
      Render::SRenderModifData::eRenderModifType::RMOD_TYPE_TRANSLATE,
      std::any(pMonitor->m_position + (rectOverride.pos() / curScaling) -
               oRealPosition)));
  renderModif.modifs.push_back(std::make_pair(
      Render::SRenderModifData::eRenderModifType::RMOD_TYPE_SCALE,
      std::any(curScaling)));
  renderModif.enabled = true;

  g_pHyprRenderer->m_renderPass.add(makeUnique<CRendererHintsPassElement>(
      CRendererHintsPassElement::SData{.renderModif = renderModif}));
  Hyprutils::Utils::CScopeGuard x([] {
    g_pHyprRenderer->m_renderPass.add(
        makeUnique<CRendererHintsPassElement>(CRendererHintsPassElement::SData{
            .renderModif = Render::SRenderModifData{}}));
  });

  CSurfacePassElement::SRenderData renderdata = {pMonitor, time, oRealPosition};
  renderdata.fadeAlpha = 1.F;
  renderdata.alpha = 0.999F;
  renderdata.blur = false;
  renderdata.surface = pLayer->wlSurface()->resource();
  renderdata.decorate = false;
  renderdata.w = oSize.x;
  renderdata.h = oSize.y;
  renderdata.pLS = pLayer;
  renderdata.clipBox = clipBox;
  renderdata.surfaceCounter = 0;

  pLayer->wlSurface()->resource()->breadthfirst(
      [&renderdata, &pLayer](SP<CWLSurfaceResource> s, const Vector2D &offset,
                             void *data) {
        if (!s || !s->m_current.texture)
          return;

        if (s->m_current.size.x < 1 || s->m_current.size.y < 1)
          return;

        renderdata.localPos = offset;
        renderdata.texture = s->m_current.texture;
        renderdata.surface = s;
        renderdata.mainSurface = s == pLayer->wlSurface()->resource();
        g_pHyprRenderer->m_renderPass.add(
            makeUnique<CSurfacePassElement>(renderdata));
        renderdata.surfaceCounter++;
      },
      &renderdata);
}

// NOTE: rects and clipbox positions are relative to the monitor, while
// damagebox and layers are not, what the fuck? xd
void CHyprspaceWidget::draw() {

  workspaceBoxes.clear();

  if (!active && !curYOffset->isBeingAnimated())
    return;

  auto owner = getOwner();

  if (!owner)
    return;

  // Full-monitor clip in monitor-local coords. Never use default CBox() to
  // "clear" clipBox — hyprutils::CBox() only sets w/h to 0 and leaves x/y
  // uninitialized, which corrupts scissor state.
  const CBox monitorClip = {{0, 0}, owner->m_transformedSize};

  const auto time = Time::steadyNow();

  owner->m_blurFBShouldRender = true;

  const bool vertical = isVertical();
  const bool onBottom = config.onBottom->value();
  const bool onRight = config.onRight->value();
  const double scale = owner->m_scale;
  const double marginPx = config.workspaceMargin->value() * scale;

  int bottomInvert = 1;
  if (onBottom)
    bottomInvert = -1;

  // Background box (monitor-local pixels, slide animation included)
  CBox widgetBox = panelBox();

  g_pHyprRenderer->m_renderData.clipBox = monitorClip;

  if (!config.disableBlur->value()) {
    renderRectWithBlur(widgetBox, config.panelBaseColor->value());
  } else {
    renderRect(widgetBox, config.panelBaseColor->value());
  }

  // Panel Border
  if (config.panelBorderWidth->value() > 0) {
    const double borderW =
        static_cast<double>(config.panelBorderWidth->value());
    CBox borderBox;
    if (vertical) {
      // line along the panel edge facing the screen centre
      borderBox = {onRight ? widgetBox.x - borderW : widgetBox.x + widgetBox.w,
                   0, borderW, owner->m_transformedSize.y};
    } else {
      borderBox = {
          widgetBox.x,
          owner->m_position.y + (onBottom * owner->m_transformedSize.y) +
              (config.panelHeight->value() + config.reservedArea->value() -
               curYOffset->value() * scale) *
                  bottomInvert,
          owner->m_transformedSize.x, borderW};
      borderBox.y -= owner->m_position.y;
    }

    renderRect(borderBox, config.panelBorderColor->value());
  }

  // damage the entire monitor to ensure full redraw during overview
  g_pHyprRenderer->damageMonitor(owner);

  // the list of workspaces to show
  std::vector<int> workspaces;

  if (config.showSpecialWorkspace->value()) {
    workspaces.push_back(SPECIAL_WORKSPACE_START);
  }

  // find the lowest and highest workspace id to determine which empty
  // workspaces to insert
  int lowestID = INT_MAX;
  int highestID = 1;
  for (auto &ws : State::workspaceState()->workspaces()) {
    if (!ws)
      continue;
    // normal workspaces start from 1, special workspaces ends on -2
    if (ws->m_id < 1)
      continue;
    if (ws->m_monitor->m_id == ownerID) {
      workspaces.push_back(ws->m_id);
      if (highestID < ws->m_id)
        highestID = ws->m_id;
      if (lowestID > ws->m_id)
        lowestID = ws->m_id;
    }
  }

  // include empty workspaces that are between non-empty ones
  if (config.showEmptyWorkspace->value()) {
    int wsIDStart = 1;
    int wsIDEnd = highestID;

    // hyprsplit/split-monitor-workspaces compatibility
    if (numWorkspaces > 0) {
      wsIDStart = std::min<int>(numWorkspaces * ownerID + 1, lowestID);
      wsIDEnd = std::max<int>(
          numWorkspaces * ownerID + 1,
          highestID); // always show the initial workspace for current monitor
    }

    for (int i = wsIDStart; i <= wsIDEnd; i++) {
      if (i == owner->activeSpecialWorkspaceID())
        continue;
      const auto pWorkspace = State::workspaceState()->query().id(i).run();
      if (pWorkspace == nullptr)
        workspaces.push_back(i);
    }
  }

  // add a new empty workspace at last
  if (config.showNewWorkspace->value()) {
    // get the lowest empty workspce id after the highest id of current
    // workspace
    while (State::workspaceState()->query().id(highestID).run() != nullptr)
      highestID++;
    workspaces.push_back(highestID);
  }

  std::sort(workspaces.begin(), workspaces.end());

  // render workspace boxes
  // horizontal panel: tile size follows the panel height, tiles run left to
  // right vertical panel:   tile size follows the panel width,  tiles run top
  // to bottom
  int wsCount = workspaces.size();
  const double panelInner =
      config.panelHeight->value() - 2 * config.workspaceMargin->value();
  const double mainSize =
      vertical ? owner->m_transformedSize.x : owner->m_transformedSize.y;
  double monitorSizeScaleFactor =
      (panelInner / mainSize) * scale; // scale box with panel thickness
  double workspaceBoxW = owner->m_transformedSize.x * monitorSizeScaleFactor;
  double workspaceBoxH = owner->m_transformedSize.y * monitorSizeScaleFactor;

  const double groupLength =
      (vertical ? workspaceBoxH : workspaceBoxW) * wsCount +
      marginPx * (wsCount - 1);
  const double panelLength = vertical ? widgetBox.h : widgetBox.w;
  const double reservedPx = config.reservedArea->value() * scale;

  double curWorkspaceRectOffsetX;
  double curWorkspaceRectOffsetY;
  if (vertical) {
    curWorkspaceRectOffsetX =
        !onRight ? ((reservedPx + marginPx) - curYOffset->value())
                 : (owner->m_transformedSize.x - (reservedPx + marginPx) -
                    workspaceBoxW + curYOffset->value());
    curWorkspaceRectOffsetY =
        config.centerAligned->value()
            ? workspaceScrollOffset->value() + (panelLength / 2.) -
                  (groupLength / 2.)
            : workspaceScrollOffset->value() + config.workspaceMargin->value();
  } else {
    curWorkspaceRectOffsetX =
        config.centerAligned->value()
            ? workspaceScrollOffset->value() + (panelLength / 2.) -
                  (groupLength / 2.)
            : workspaceScrollOffset->value() + config.workspaceMargin->value();
    curWorkspaceRectOffsetY =
        !onBottom ? ((reservedPx + marginPx) - curYOffset->value())
                  : (owner->m_transformedSize.y - (reservedPx + marginPx) -
                     workspaceBoxH + curYOffset->value());
  }
  double workspaceOverflowSize =
      std::max<double>(((groupLength - panelLength) / 2) + marginPx, 0);

  *workspaceScrollOffset =
      std::clamp<double>(workspaceScrollOffset->goal(), -workspaceOverflowSize,
                         workspaceOverflowSize);

  if (!(workspaceBoxW > 0 && workspaceBoxH > 0))
    return;

  // move on to the next tile position along the panel
  const auto advance = [&]() {
    if (vertical)
      curWorkspaceRectOffsetY += workspaceBoxH + marginPx;
    else
      curWorkspaceRectOffsetX += workspaceBoxW + marginPx;
  };

  for (auto wsID : workspaces) {
    const auto ws = State::workspaceState()->query().id(wsID).run();
    CBox curWorkspaceBox = {curWorkspaceRectOffsetX, curWorkspaceRectOffsetY,
                            workspaceBoxW, workspaceBoxH};

    // workspace background rect (NOT background layer) and border
    if (ws == owner->m_activeWorkspace) {
      if (config.workspaceBorderSize->value() >= 1 &&
          CHyprColor(config.workspaceActiveBorder->value()).a > 0) {
        renderBorder(
            curWorkspaceBox,
            Config::CGradientValueData(config.workspaceActiveBorder->value()),
            config.workspaceBorderSize->value());
      }
      if (!config.disableBlur->value()) {
        renderRectWithBlur(
            curWorkspaceBox,
            config.workspaceActiveBackground
                ->value()); // cant really round it until I find a proper way to
                            // clip windows to a rounded rect
      } else {
        renderRect(curWorkspaceBox, config.workspaceActiveBackground->value());
      }
      if (!config.drawActiveWorkspace->value()) {
        advance();
        continue;
      }
    } else {
      if (config.workspaceBorderSize->value() >= 1 &&
          CHyprColor(config.workspaceInactiveBorder->value()).a > 0) {
        renderBorder(
            curWorkspaceBox,
            Config::CGradientValueData(config.workspaceInactiveBorder->value()),
            config.workspaceBorderSize->value());
      }
      if (!config.disableBlur->value()) {
        renderRectWithBlur(curWorkspaceBox,
                           config.workspaceInactiveBackground->value());
      } else {
        renderRect(curWorkspaceBox,
                   config.workspaceInactiveBackground->value());
      }
    }

    // render all layer surfaces of one layer level into the current tile
    const auto drawLayerSet = [&](size_t level) {
      for (auto &ls : owner->m_layerSurfaceLayers[level]) {
        CBox layerBox = {
            curWorkspaceBox.pos() +
                (ls->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT) -
                 owner->m_position) *
                    monitorSizeScaleFactor,
            ls->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT) *
                monitorSizeScaleFactor};
        renderLayerStub(ls.lock(), owner, layerBox, curWorkspaceBox, time);
      }
    };

    // render one window into the current tile (aspect-fit is applied in
    // renderWindowStub)
    const auto drawWindow = [&](const auto &w) {
      const auto wPos =
          w->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
      const auto wSize = w->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
      double wX = curWorkspaceRectOffsetX + ((wPos.x - owner->m_position.x) *
                                             monitorSizeScaleFactor * scale);
      double wY = curWorkspaceRectOffsetY + ((wPos.y - owner->m_position.y) *
                                             monitorSizeScaleFactor * scale);
      double wW = wSize.x * monitorSizeScaleFactor * scale;
      double wH = wSize.y * monitorSizeScaleFactor * scale;
      if (!(wW > 0 && wH > 0))
        return;
      CBox curWindowBox = {wX, wY, wW, wH};
      renderWindowStub(w, owner, owner->m_activeWorkspace, curWindowBox,
                       curWorkspaceBox, time);
    };

    // background and bottom layers
    if (!config.hideBackgroundLayers->value()) {
      drawLayerSet(0);
      drawLayerSet(1);
    }

    // the mini panel to cover the awkward empty space reserved by the panel
    if (owner->m_activeWorkspace == ws && config.affectStrut->value()) {
      CBox miniPanelBox = {curWorkspaceRectOffsetX, curWorkspaceRectOffsetY,
                           widgetBox.w * monitorSizeScaleFactor,
                           widgetBox.h * monitorSizeScaleFactor};
      if (vertical) {
        if (onRight)
          miniPanelBox.x += workspaceBoxW - miniPanelBox.w;
      } else if (onBottom)
        miniPanelBox.y += workspaceBoxH - miniPanelBox.h;

      if (!config.disableBlur->value()) {
        renderRectWithBlur(miniPanelBox, CHyprColor(0, 0, 0, 0));
      } else {
        // what
        renderRect(miniPanelBox, CHyprColor(0, 0, 0, 0));
      }
    }

    if (ws != nullptr) {
      // draw tiled windows
      for (auto &w : Desktop::windowState()->windows()) {
        if (!w)
          continue;
        if (w->m_workspace == ws && !w->m_isFloating)
          drawWindow(w);
      }
      // draw floating windows
      for (auto &w : Desktop::windowState()->windows()) {
        if (!w)
          continue;
        if (w->m_workspace == ws && w->m_isFloating &&
            ws->getLastFocusedWindow() != w)
          drawWindow(w);
      }
      // draw last focused floating window on top
      if (ws->getLastFocusedWindow())
        if (ws->getLastFocusedWindow()->m_isFloating)
          drawWindow(ws->getLastFocusedWindow());
    }

    if (owner->m_activeWorkspace != ws || !config.hideRealLayers->value()) {
      // this layer is hidden for real workspace when panel is displayed
      if (!config.hideTopLayers->value())
        drawLayerSet(2);

      if (!config.hideOverlayLayers->value())
        drawLayerSet(3);
    }

    // Resets workspaceBox to scaled absolute coordinates for input detection.
    // While rendering is done in pixel coordinates, input detection is done in
    // scaled coordinates, taking into account monitor scaling.
    // Since the monitor position is already given in scaled coordinates,
    // we only have to scale all relative coordinates, then add them to the
    // monitor position to get a scaled absolute position.
    curWorkspaceBox.scale(1 / owner->m_scale);

    curWorkspaceBox.x += owner->m_position.x;
    curWorkspaceBox.y += owner->m_position.y;
    workspaceBoxes.emplace_back(std::make_tuple(wsID, curWorkspaceBox));

    // set the current position to the next workspace box
    advance();
  }

  g_pHyprRenderer->m_renderData.clipBox = monitorClip;
}
