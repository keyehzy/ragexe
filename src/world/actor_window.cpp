#include "world/actor_window.h"

#include <cstdint>

#include "core/x87.h"

namespace ragexe::world {

namespace {

constexpr std::int32_t kGaugeWidth = 0x3c;
constexpr std::int32_t kGaugeHeight = 6;
constexpr std::int32_t kGaugeLeft = 0x1e;
constexpr std::int32_t kTitleLeft = 0x46;
constexpr float kTitleRise = 17.0f;  // 005e9668
constexpr std::uint32_t kBalloonTime = 5000;
constexpr std::int32_t kScreenMargin = 3;
constexpr std::int32_t kCloseGauge = 0x53;

std::int32_t Wrap(std::int64_t v) {
  return static_cast<std::int32_t>(static_cast<std::uint32_t>(v));
}

// The actor's point on screen less WindowRise × +0x48: fild of +0x94, less the product, rounded
// once to single precision.
float Above(const render::RenderObject& o, const StateContext& context) {
  const float rise = WindowRise(context.screen_height) * o.pixel_scale;
  return core::RoundedSum(-rise, static_cast<double>(o.screen_y));
}

void Call(const StateContext& context, WindowCallKind kind, std::int32_t window, std::int32_t a,
          std::int32_t b) {
  context.windows->calls.push_back({kind, window, a, b});
}

void Move(const StateContext& context, std::int32_t window, std::int32_t x, float y) {
  Call(context, WindowCallKind::kMove, window, x, core::Ftol(static_cast<double>(y)));
}

void Close(std::int32_t& window, const StateContext& context) {
  if (window == 0) return;
  if (context.closed_windows != nullptr) context.closed_windows->push_back(window);
  window = 0;
}

template <typename Actor, typename SetState>
void Step(Actor& actor, const StateContext& context, std::uint32_t now, SetState set_state) {
  const render::RenderObject& o = actor.object;
  const bool told = context.windows != nullptr;
  if (actor.balloon != 0) {
    if (now < actor.balloon_at + kBalloonTime) {
      if (told) {
        std::int32_t x = Wrap(std::int64_t{o.screen_x} - actor.balloon_width / 2);
        const float above =
            core::RoundedSum(Above(o, context), -static_cast<double>(actor.balloon_height));
        std::int32_t y = core::Ftol(static_cast<double>(above));
        const auto& size = context.windows->sizes[static_cast<std::size_t>(actor.balloon - 1)];
        const std::int32_t right =
            Wrap(std::int64_t{context.screen_width} - size[0] + kScreenMargin);
        const std::int32_t bottom =
            Wrap(std::int64_t{context.screen_height} - size[1] + kScreenMargin);
        if (x < -kScreenMargin) x = -kScreenMargin;
        if (x > right) x = right;
        if (y < -kScreenMargin) y = -kScreenMargin;
        if (y > bottom) y = bottom;
        Call(context, WindowCallKind::kMove, actor.balloon, x, y);
      }
    } else {
      Close(actor.balloon, context);
    }
  }
  if (actor.chat_title != 0 && told) {
    Move(context, actor.chat_title, Wrap(std::int64_t{o.screen_x} - kTitleLeft),
         Above(o, context) - kTitleRise);
  }
  if (actor.shop_title != 0 && told) {
    Move(context, actor.shop_title, Wrap(std::int64_t{o.screen_x} - kTitleLeft),
         (Above(o, context) - kTitleRise) - kTitleRise);
  }
  if (actor.cast_gauge == 0) return;
  if (now > actor.cast_end) {
    Close(actor.cast_gauge, context);
    if (actor.state == 8) set_state(0);
    return;
  }
  if (!told) return;
  Move(context, actor.cast_gauge, Wrap(std::int64_t{o.screen_x} - kGaugeLeft), Above(o, context));
  std::int32_t whole = Wrap(std::int64_t{actor.cast_end} - actor.cast_start);
  if (whole < 0) whole = 1;
  std::int32_t done = Wrap(std::int64_t{now} + whole - actor.cast_end);
  if (done > whole) done = whole;
  Call(context, WindowCallKind::kAmount, actor.cast_gauge, done, whole);
}

}  // namespace

float WindowRise(std::int32_t screen_height) {
  const std::int32_t scaled = Wrap(std::int64_t{screen_height} * 81);
  const std::int32_t rise = scaled / 480;
  return static_cast<float>(rise);
}

void StartGameActorCast(GameActor& actor, std::int32_t duration, const StateContext& context,
                        std::uint32_t now) {
  if (actor.cast_gauge == 0 && context.windows != nullptr) {
    ActorWindows& windows = *context.windows;
    windows.sizes.push_back({kGaugeWidth, kGaugeHeight});
    actor.cast_gauge = static_cast<std::int32_t>(windows.sizes.size());
    Call(context, WindowCallKind::kCreate, actor.cast_gauge, kGaugeWidth, kGaugeHeight);
    const render::RenderObject& o = actor.object;
    Move(context, actor.cast_gauge, Wrap(std::int64_t{o.screen_x} - kGaugeLeft), Above(o, context));
    Call(context, WindowCallKind::kAdd, actor.cast_gauge, 0, 0);
  }
  actor.cast_end = now + static_cast<std::uint32_t>(duration);
  actor.cast_start = now;
}

bool CloseGameActorWindow(GameActor& actor, std::int32_t message, const StateContext& context) {
  switch (message) {
    case 0x21: Close(actor.chat_title, context); return true;
    case 0x36: Close(actor.shop_title, context); return true;
    case kCloseGauge: Close(actor.cast_gauge, context); return true;
    default: return false;
  }
}

void StepGameActorWindows(GameActor& actor, const StateContext& context, std::uint32_t now) {
  Step(actor, context, now,
       [&](std::int32_t state) { SetGameActorState(actor, state, context, now); });
}

void StepGameActorWindows(PlayerActor& player, const StateContext& context, std::uint32_t now) {
  Step(player, context, now,
       [&](std::int32_t state) { SetPlayerState(player, state, context, now); });
}

}  // namespace ragexe::world
