#include <hyprland/src/desktop/state/GlobalWindowController.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>

#include "Globals.hpp"
#include "Overview.hpp"

bool CHyprspaceWidget::buttonEvent(bool pressed, Vector2D coords) {
  bool Return;

  const auto dragTarget = g_layoutManager->dragController()->target();
  const auto targetWindow = dragTarget ? dragTarget->window() : nullptr;

  // this is for click to exit, we set a timeout for button release
  bool couldExit = false;
  if (pressed)
    lastPressedTime = std::chrono::high_resolution_clock::now();
  else if (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::high_resolution_clock::now() - lastPressedTime)
               .count() < 200)
    couldExit = true;

  int targetWorkspaceID = SPECIAL_WORKSPACE_START - 1;

  // find which workspace the mouse hovers over
  for (auto &w : workspaceBoxes) {
    auto wi = std::get<0>(w);
    auto wb = std::get<1>(w);
    if (wb.containsPoint(coords)) {
      targetWorkspaceID = wi;
      break;
    }
  }

  auto targetWorkspace =
      State::workspaceState()->query().id(targetWorkspaceID).run();

  // create new workspace
  if (targetWorkspace == nullptr &&
      targetWorkspaceID >= SPECIAL_WORKSPACE_START) {
    targetWorkspace =
        State::workspaceState()->create(targetWorkspaceID, getOwner()->m_id);
  }

  // if the cursor is hovering over workspace, clicking should switch workspace
  // instead of starting window drag
  if (config.autoDrag->value() && (targetWorkspace == nullptr || !pressed)) {
    if (g_layoutManager->dragController()->target())
      g_layoutManager->endDragTarget();

    if (pressed) {
      const auto PWINDOW = Desktop::viewState()->hitTest().windowAt(
          coords, Desktop::View::WINDOW_ONLY, nullptr);
      if (PWINDOW) {
        const auto LT = PWINDOW->layoutTarget();
        if (LT)
          g_layoutManager->beginDragTarget(LT, MBIND_MOVE);
      }
    }
  }
  Return = false;

  // release window on workspace to drop it in
  if (targetWindow && targetWorkspace != nullptr && !pressed) {
    Desktop::globalWindowController()->moveWindowToWorkspace(targetWindow,
                                                             targetWorkspace);
    if (targetWindow->m_isFloating) {
      auto targetPos = getOwner()->m_position + (getOwner()->m_size / 2.) -
                       (targetWindow->m_reportedSize / 2.);
      targetWindow->move(targetPos);
    }
    if (config.switchOnDrop->value()) {
      State::monitorState()
          ->query()
          .id(targetWorkspace->m_monitor->m_id)
          .run()
          ->changeWorkspace(targetWorkspace->m_id);
      if (config.exitOnSwitch->value() && active)
        hide();
    }
    updateLayout();
  }
  // click workspace to change to workspace and exit overview
  else if (targetWorkspace && !pressed) {
    if (targetWorkspace->m_isSpecialWorkspace)
      getOwner()->activeSpecialWorkspaceID() == targetWorkspaceID
          ? getOwner()->setSpecialWorkspace(nullptr)
          : getOwner()->setSpecialWorkspace(targetWorkspaceID);
    else {
      State::monitorState()
          ->query()
          .id(targetWorkspace->m_monitor->m_id)
          .run()
          ->changeWorkspace(targetWorkspace->m_id);
    }
    if (config.exitOnSwitch->value() && active)
      hide();
  }
  // click elsewhere to exit overview
  else if (config.exitOnClick->value() && targetWorkspace == nullptr &&
           active && couldExit && !pressed)
    hide();

  return Return;
}

bool CHyprspaceWidget::axisEvent(double delta, wl_pointer_axis axis,
                                 Vector2D coords) {

  // scroll through panel if cursor is on it
  if (panelHitBox().containsPoint(coords)) {
    // only the scroll axis along the panel pans it; the other axis is ignored
    // here
    const wl_pointer_axis panAxis = isVertical()
                                        ? WL_POINTER_AXIS_VERTICAL_SCROLL
                                        : WL_POINTER_AXIS_HORIZONTAL_SCROLL;
    if (axis == panAxis)
      *workspaceScrollOffset = workspaceScrollOffset->goal() - delta * 2;
  }
  // otherwise, scroll to switch active workspace (vertical scroll only)
  else if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
    if (delta < 0) {
      SWorkspaceIDName wsIDName = getWorkspaceIDNameFromString("r-1");
      if (State::workspaceState()->query().id(wsIDName.id).run() == nullptr) {
        auto newWorkspace =
            State::workspaceState()->create(wsIDName.id, ownerID);
        (void)newWorkspace;
      }
      getOwner()->changeWorkspace(wsIDName.id);
    } else {
      SWorkspaceIDName wsIDName = getWorkspaceIDNameFromString("r+1");
      if (State::workspaceState()->query().id(wsIDName.id).run() == nullptr) {
        auto newWorkspace =
            State::workspaceState()->create(wsIDName.id, ownerID);
        (void)newWorkspace;
      }
      getOwner()->changeWorkspace(wsIDName.id);
    }
  }

  return false;
}

bool CHyprspaceWidget::isSwiping() { return swiping; }

bool CHyprspaceWidget::beginSwipe(IPointer::SSwipeBeginEvent e) {
  swiping = true;
  activeBeforeSwipe = active;
  avgSwipeSpeed = 0;
  swipePoints = 0;
  return false;
}

bool CHyprspaceWidget::updateSwipe(IPointer::SSwipeUpdateEvent e) {
  constexpr int fingers = 3;
  const auto distance = HyprConfig::getWorkspaceSwipeDistance().value();

  const bool vertical = isVertical();

  // restrict swipe to the axis with the most significant movement to prevent
  // misinput horizontal panel: open/close with a vertical swipe, vertical
  // panel: open/close with a horizontal swipe
  const bool openAxisDominant =
      vertical ? (std::abs(e.delta.x) > std::abs(e.delta.y))
               : (std::abs(e.delta.x) < std::abs(e.delta.y));

  if (openAxisDominant) {
    if (swiping && e.fingers == (uint32_t)fingers) {

      float currentScaling = State::monitorState()
                                 ->query()
                                 .vec(g_pInputManager->getMouseCoordsInternal())
                                 .run()
                                 ->m_size.x /
                             distance;

      const double axisDelta = vertical ? e.delta.x : e.delta.y;
      const bool flipped =
          vertical ? config.onRight->value() : config.onBottom->value();

      double scrollDifferential = axisDelta *
                                  (config.reverseSwipe->value() ? -1 : 1) *
                                  (flipped ? -1 : 1) * currentScaling;

      curSwipeOffset += scrollDifferential;
      curSwipeOffset = std::clamp<double>(
          curSwipeOffset, -10,
          ((config.panelHeight->value() + config.reservedArea->value()) *
           getOwner()->m_scale));

      avgSwipeSpeed = (avgSwipeSpeed * swipePoints + scrollDifferential) /
                      (swipePoints + 1);

      curYOffset->setValueAndWarp(
          ((config.panelHeight->value() + config.reservedArea->value()) *
           getOwner()->m_scale) -
          curSwipeOffset);

      if (curSwipeOffset < 10 && active)
        hide();
      else if (curSwipeOffset > 10 && !active)
        show();

      return false;
    }
  } else {
    // scroll through panel
    if (e.fingers == (uint32_t)fingers && active) {
      if (panelHitBox().containsPoint(
              g_pInputManager->getMouseCoordsInternal())) {
        const double panDelta = vertical ? e.delta.y : e.delta.x;
        workspaceScrollOffset->setValueAndWarp(workspaceScrollOffset->goal() +
                                               panDelta * 2);
        return false;
      }
    }
  }
  // otherwise, do not cancel the event and perform workspace swipe normally
  return true;
}

// janky asf
bool CHyprspaceWidget::endSwipe(IPointer::SSwipeEndEvent e) {
  swiping = false;
  // force cancel swipe
  if (e.cancelled) {
    if (active)
      hide();
    curSwipeOffset = -10.;
  } else {
    const auto swipeForceSpeed =
        HyprConfig::getWorkspaceSwipeMinSpeedToForce().value();
    const auto cancelRatio = HyprConfig::getWorkspaceSwipeCancelRatio().value();
    double swipeTravel =
        (config.panelHeight->value() + config.reservedArea->value()) *
        getOwner()->m_scale;
    if (activeBeforeSwipe) {
      if ((curSwipeOffset < swipeTravel * cancelRatio) ||
          avgSwipeSpeed < -swipeForceSpeed) {
        if (active)
          hide();
        else {
          *curYOffset =
              (config.panelHeight->value() + config.reservedArea->value()) *
              getOwner()->m_scale;
          curSwipeOffset = -10.;
        }
      } else {
        // cancel
        if (!active)
          show();
        else {
          *curYOffset = 0;
          curSwipeOffset =
              (config.panelHeight->value() + config.reservedArea->value()) *
              getOwner()->m_scale;
        }
      }
    } else {
      if ((curSwipeOffset > swipeTravel * (1.f - cancelRatio)) ||
          avgSwipeSpeed > swipeForceSpeed) {
        if (!active)
          show();
        else {
          *curYOffset = 0;
          curSwipeOffset =
              (config.panelHeight->value() + config.reservedArea->value()) *
              getOwner()->m_scale;
        }
      } else {
        // cancel
        if (active)
          hide();
        else {
          *curYOffset =
              (config.panelHeight->value() + config.reservedArea->value()) *
              getOwner()->m_scale;
          curSwipeOffset = -10.;
        }
      }
    }
  }
  avgSwipeSpeed = 0;
  swipePoints = 0;
  return false;
}
