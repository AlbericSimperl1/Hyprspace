#pragma once
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprutils/animation/AnimationConfig.hpp>

class CHyprspaceWidget {

  bool active = false;

  int64_t ownerID;

  // animation override stuff
  Hyprutils::Animation::SAnimationPropertyConfig curAnimationConfig;
  Hyprutils::Animation::SAnimationPropertyConfig curAnimation;

  // for checking mouse hover for workspace drag and move
  // modified on draw call, accessed on mouse click and release
  std::vector<std::tuple<int, CBox>> workspaceBoxes;

  // for storing the fullscreen state of windows prior to overview activation
  // (which unfullscreens all windows)
  std::vector<std::tuple<PHLWINDOWREF, Fullscreen::eFullscreenMode>>
      prevFullscreen;

  // for storing the layer alpha values prior to overview activation (which sets
  // all panel to transparent when configured)
  std::vector<std::tuple<PHLLS, float>> oLayerAlpha;

  // for click-to-exit
  std::chrono::system_clock::time_point lastPressedTime =
      std::chrono::high_resolution_clock::now();

  bool swiping = false;
  // whether if the panel is active before the current swiping event
  bool activeBeforeSwipe = false;
  double avgSwipeSpeed = 0.;
  // number of swiping speed frames recorded
  int swipePoints = 0;
  // on second thought, this seems redundant as we could just write to
  // curYOffset while swiping
  double curSwipeOffset = 10.;

  PHLANIMVAR<float> workspaceScrollOffset;

  // GNOME-style stage (real workspace rendered scaled), set in draw(), used for
  // input mapping
  CBox stageBoxGlobal = {0, 0, 0, 0};
  double stageRatio = 1.0;
  bool stageShown = false;

  // whether updateLayout() currently holds a reserved area on the owner monitor
  bool reservedApplied = false;

public:
  // for slide-in animation and swiping
  PHLANIMVAR<float> curYOffset;

  CHyprspaceWidget(uint64_t);
  ~CHyprspaceWidget();

  PHLMONITOR getOwner();
  bool isActive();

  void show();
  void hide();

  void updateConfig();

  // should be called active or not
  void draw();

  // reserves area on owner monitor
  void updateLayout();

  // panel geometry
  // true when the panel is docked to the left/right edge (workspaces stacked
  // top to bottom)
  bool isVertical();
  // panel box in monitor-local pixel coordinates (slide animation included)
  CBox panelBox();
  // panel box in global logical coordinates (for input hit testing)
  CBox panelHitBox();

  bool buttonEvent(bool, Vector2D coords);
  bool axisEvent(double, wl_pointer_axis axis, Vector2D coords);

  bool isSwiping();

  bool beginSwipe(IPointer::SSwipeBeginEvent);
  bool updateSwipe(IPointer::SSwipeUpdateEvent);
  bool endSwipe(IPointer::SSwipeEndEvent);
};
