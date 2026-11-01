#include "world/actor_update.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include "core/x87.h"
#include "render/effect_table.h"
#include "res/act.h"
#include "world/actor_hit.h"
#include "world/actor_window.h"

namespace ragexe::world {

namespace {

constexpr std::uint32_t kSightIdle = 0x8ca0;
constexpr std::int32_t kSightEffect = 0x16;
constexpr std::int32_t kAcolyteSightEffect = 0x21;
constexpr std::int32_t kSightMessage = 0x2c;
constexpr std::uint32_t kKeyTime = 100;
constexpr std::uint32_t kUnkeyed = 0xffff0000;
constexpr std::uint32_t kWhite = 0xffffffff;
constexpr std::int32_t kBrighten = 8;
constexpr std::int32_t kGaugeLeft = 0x1e;
constexpr std::int32_t kSilent = 20;

std::int32_t Wrap(std::int64_t v) {
  return static_cast<std::int32_t>(static_cast<std::uint32_t>(v));
}

std::int32_t Clamped(std::int32_t v) {
  if (v <= 0) return 0;
  return v < 0xff ? v : 0xff;
}

// CActRes::GetFrame (00421a60): GetAction (004219d0) and GetActFrameAt (004206e0) answer an
// empty action and an empty frame, g_ActFrame (00628eb8), for those the act lacks, and that
// frame's event is 0.
std::int32_t FrameEvent(const res::Act& act, std::int32_t action, std::int64_t frame) {
  const auto a = static_cast<std::uint32_t>(action);
  if (a >= act.actions.size()) return 0;
  const auto& frames = act.actions[a];
  if (frame < 0 || std::cmp_greater_equal(frame, frames.size())) return 0;
  return frames[static_cast<std::size_t>(frame)].event;
}

// CActRes::GetFrameCount (00421a80): the action's frames, 1 for fewer than two.
std::int32_t FrameCount(const res::Act& act, std::int32_t action) {
  const auto a = static_cast<std::uint32_t>(action);
  if (a >= act.actions.size() || act.actions[a].size() <= 1) return 1;
  return static_cast<std::int32_t>(act.actions[a].size());
}

// CActRes::GetEventName (00420f20): "" past the act's events.
std::string_view EventName(const res::Act& act, std::int32_t event) {
  const auto e = static_cast<std::uint32_t>(event);
  return e < act.events.size() ? std::string_view(act.events[e]) : std::string_view();
}

void PlayFrame(const GameActor& actor, const res::Act& act, std::int64_t frame,
               const StateContext& context) {
  const render::RenderObject& o = actor.object;
  const std::int32_t event = FrameEvent(act, o.action, frame);
  if (event == -1) return;
  const std::string_view name = EventName(act, event);
  if (name.find(".wav") == std::string_view::npos) return;
  if (context.sounds == nullptr || context.player == nullptr) return;
  context.sounds->push_back(
      {name, o.position[0] - (*context.player)[0], o.position[2] - (*context.player)[2]});
}

// The key colour (+0x68) back to none 100 ms after the actor was keyed (+0x6c), unsigned.
void EndKey(render::RenderObject& o, std::uint32_t now) {
  if (o.keyed && now > o.keyed_at + kKeyTime) {
    o.key_color = kUnkeyed;
    o.keyed = false;
  }
}

void BrightenUnlessWhite(render::RenderObject& o) {
  if (o.color != kWhite) BrightenColor(o, kBrighten, kBrighten, kBrighten, kBrighten);
}

}  // namespace

void HoldWhileStoneOrFrozen(GameActor& actor) {
  if (actor.body_state > 0 && actor.body_state <= 2) actor.object.held = true;
}

void RepeatSightEffect(GameActor& actor, const StateContext& context, std::uint32_t now) {
  if (actor.effect_state == 0) {
    actor.sight_ticks = kSightIdle;
    return;
  }
  if ((actor.effect_state & 1) != 0) {
    const std::int32_t job = GameActorJob(actor);
    const bool acolyte = job == 4 || job == 8 || job == 15;
    const std::int32_t id = acolyte ? kAcolyteSightEffect : kSightEffect;
    if (std::cmp_greater_equal(actor.sight_ticks, render::EffectDuration(id))) {
      actor.sight_ticks = 0;
      const std::int32_t effect = LaunchGameActorEffect(actor, id, context);
      if (effect != 0 && context.launched_messages != nullptr) {
        context.launched_messages->push_back({.effect = effect,
                                              .id = kSightMessage,
                                              .a = static_cast<std::int32_t>((now >> 4) % 10 + 1)});
      }
    }
  }
  ++actor.sight_ticks;
}

void BrightenColor(render::RenderObject& object, std::int32_t alpha, std::int32_t red,
                   std::int32_t green, std::int32_t blue) {
  if (object.tinted) return;
  const std::uint32_t c = object.color;
  const auto channel = [c](int shift, std::int32_t by) {
    return static_cast<std::uint32_t>(Clamped(static_cast<std::int32_t>(c >> shift & 0xff) + by))
           << shift;
  };
  object.color = channel(24, alpha) | channel(16, red) | channel(8, green) | channel(0, blue);
}

void PlayFrameSounds(GameActor& actor, const StateContext& context) {
  render::RenderObject& o = actor.object;
  if (context.act != nullptr && (o.frame != o.sounded_frame || o.sounded_action != o.base_action)) {
    const res::Act& act = *context.act;
    if (o.sounded_action != o.base_action) o.sounded_frame = 0;
    if (o.frame > o.sounded_frame) {
      for (std::int64_t f = std::int64_t{o.sounded_frame} + 1; f <= o.frame; ++f)
        PlayFrame(actor, act, f, context);
    } else if (o.frame < o.sounded_frame) {
      const std::int32_t count = FrameCount(act, o.action);
      if (o.sounded_frame >= count) o.sounded_frame = count - 1;
      for (std::int64_t f = std::int64_t{o.sounded_frame} + 1; f < count; ++f)
        PlayFrame(actor, act, f, context);
      for (std::int64_t f = 0; f <= o.frame; ++f) PlayFrame(actor, act, f, context);
    }
  }
  o.sounded_action = o.base_action;
  o.sounded_frame = o.frame;
}

void RepeatNpcEffect(GameActor& actor, const StateContext& context, std::uint32_t now) {
  const std::uint32_t period = static_cast<std::uint32_t>(actor.field_0x1ec) * 1000u;
  if (now > period + actor.repeated_at) {
    actor.repeated_at = now;
    LaunchGameActorEffect(actor, actor.speed, context);
  }
}

void MovePlayerGauge(PlayerActor& player, const StateContext& context) {
  if (player.gauge == 0 || context.windows == nullptr) return;
  const render::RenderObject& o = player.object;
  const std::int32_t rise = Wrap(std::int64_t{context.screen_height} * 12) / 480;
  const float product = core::RoundedProduct(o.pixel_scale, static_cast<double>(rise));
  const float y = core::RoundedSum(product, static_cast<double>(o.screen_y));
  context.windows->calls.push_back({WindowCallKind::kMove, player.gauge,
                                    Wrap(std::int64_t{o.screen_x} - kGaugeLeft),
                                    core::Ftol(static_cast<double>(y))});
}

StateResult UpdateNpc(GameActor& actor, const StateContext& context, std::uint32_t now,
                      std::uint32_t server_time) {
  if (!UpdateGameActorFade(actor, now)) return StateResult::kFaded;
  EndKey(actor.object, now);
  StateResult result = StateResult::kDone;
  switch (GameActorJob(actor)) {
    case 0x2d:
    case 0x8b: break;
    case 0x68: RepeatNpcEffect(actor, context, now); break;
    case 0x6f:
      DeliverGameActorHits(actor, context, now);
      StepGameActorWindows(actor, context, now);
      result = ProcessGameActorState(actor, context, now, server_time);
      break;
    default:
      HoldWhileStoneOrFrozen(actor);
      RepeatSightEffect(actor, context, now);
      DeliverGameActorHits(actor, context, now);
      BrightenUnlessWhite(actor.object);
      StepGameActorWindows(actor, context, now);
      result = ProcessGameActorState(actor, context, now, server_time);
      break;
  }
  if (actor.field_0x1f4 != kSilent) PlayFrameSounds(actor, context);
  return result;
}

StateResult UpdatePlayer(PlayerActor& player, const StateContext& context, std::uint32_t now,
                         std::uint32_t server_time) {
  RepeatSightEffect(player, context, now);
  DeliverGameActorHits(player, context, now);
  StepGameActorWindows(player, context, now);
  MovePlayerGauge(player, context);
  EndKey(player.object, now);
  BrightenUnlessWhite(player.object);
  const StateResult result = ProcessGameActorState(player, context, now, server_time);
  PlayFrameSounds(player, context);
  return result;
}

}  // namespace ragexe::world
