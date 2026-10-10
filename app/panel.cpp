#include "panel.h"

#include <algorithm>
#include <cmath>

namespace champi
{
namespace
{
constexpr float kToggleMargin = 1.5f; // the toggle's lever is small: take clicks just round it too

bool InRect(const layout::Rect& r, float x, float y, float margin = 0)
{
    return x >= r.x - margin && x < r.x + r.w + margin && y >= r.y - margin && y < r.y + r.h + margin;
}

bool InCircle(const layout::Circle& c, float x, float y)
{
    const float dx = x - c.x, dy = y - c.y;
    return dx * dx + dy * dy <= c.d * c.d / 4;
}

float Encode(uint8_t v, int divisor)
{
    return std::pow(std::min(1.0f, v * divisor / 255.0f), 1 / 2.2f);
}
} // namespace

layout::Rect KeyCap(int key)
{
    const layout::Point& c = layout::kKey[key - 1];
    return {c.x - kKeyCap / 2, c.y - kKeyCap / 2, kKeyCap, kKeyCap};
}

layout::Circle Knob(int encoder)
{
    const layout::Circle& hole = layout::kEncoder[encoder - 1];
    return {hole.x, hole.y, encoder == 5 ? kScrubWheel : kKnobRing};
}

layout::Circle LineInJack()
{
    return {layout::kLineInJack.x, layout::kLineInJack.y, kJack};
}

layout::Circle PhonesJack()
{
    return {layout::kPhonesJack.x, layout::kPhonesJack.y, kJack};
}

layout::Rect UsbSocket()
{
    return {layout::kUsbSocket.x - 4.5f, kFrontEdgeY - 1.4f, 9, 2.8f};
}

layout::Rect SdSlot()
{
    return {layout::kSdSlot.x - 6, kFrontEdgeY - 0.9f, 12, 1.8f};
}

Hit HitTest(float x, float y)
{
    for(int k = 1; k <= kNumKeys; k++)
        if(InRect(KeyCap(k), x, y))
            return {Hit::Kind::kKey, k};
    for(int e = 1; e <= kNumEncoders; e++)
        if(InCircle(Knob(e), x, y))
            return {Hit::Kind::kEncoder, e};
    if(InRect(layout::kToggleSlot, x, y, kToggleMargin))
        return {Hit::Kind::kToggle, 0};
    if(InCircle(LineInJack(), x, y))
        return {Hit::Kind::kLineIn, 0};
    if(InCircle(PhonesJack(), x, y))
        return {Hit::Kind::kPhones, 0};
    // Both are thin: take clicks a little round them too.
    if(InRect(UsbSocket(), x, y, 1))
        return {Hit::Kind::kUsb, 0};
    if(InRect(SdSlot(), x, y, 1.5f))
        return {Hit::Kind::kSdCard, 0};
    return {};
}

float Light::Brightness() const
{
    return std::max({r, g, b});
}

Light LedLight(const daisycola::Rgb& led, int divisor)
{
    return {Encode(led.r, divisor), Encode(led.g, divisor), Encode(led.b, divisor)};
}

float TurnGain(float rate)
{
    return std::clamp(rate / kAccelFrom, 1.0f, kMaxTurnGain);
}

int QueueTurn(PanelState& panel, int encoder, int detents)
{
    const int pending = panel.PendingDetents(encoder);
    if(detents > 0)
        detents = std::min(detents, std::max(0, kMaxPendingDetents - pending));
    else if(detents < 0)
        detents = std::max(detents, std::min(0, -kMaxPendingDetents - pending));
    if(detents)
        panel.TurnEncoder(encoder, detents);
    return detents;
}

float TurnRate::Update(float detents, Clock::time_point now)
{
    using std::chrono::duration;
    constexpr float kSmoothing = 0.05f, kPause = 0.15f; // seconds
    const float     dt         = started_ ? duration<float>(now - last_).count() : kPause;
    started_                   = true;
    last_                      = now;
    if(dt >= kPause)
    {
        rate_ = 0; // a fresh start: the first step is never accelerated
        return rate_;
    }
    const float instant = std::abs(detents) / std::max(dt, 0.001f);
    const float weight  = 1 - std::exp(-std::max(dt, 0.001f) / kSmoothing);
    rate_ += (instant - rate_) * weight;
    return rate_;
}

bool MouseControl::Press(float x, float y, Clock::time_point now)
{
    const Hit hit = HitTest(x, y);
    switch(hit.kind)
    {
        case Hit::Kind::kKey:
            panel_.SetKey(key_ = hit.index, true);
            return true;
        case Hit::Kind::kEncoder:
            EndClick();
            encoder_    = hit.index;
            start_y_    = y;
            last_y_     = y;
            dragged_    = 0;
            drag_rate_  = {};
            dragging_   = false;
            pushed_     = false;
            pressed_at_ = now;
            return true;
        case Hit::Kind::kToggle:
            panel_.SetToggle(!panel_.Toggle());
            return true;
        case Hit::Kind::kLineIn:
            panel_.SetLineIn(!panel_.LineIn());
            return true;
        case Hit::Kind::kPhones:
            panel_.SetPhones(!panel_.Phones());
            return true;
        case Hit::Kind::kUsb:
            charger_.SetUsbPower(!charger_.UsbPower());
            return true;
        case Hit::Kind::kSdCard:
            card_.SetInserted(!card_.Inserted());
            return true;
        case Hit::Kind::kNone:
            break;
    }
    return false;
}

void MouseControl::Move(float x, float y, Clock::time_point now)
{
    if(!encoder_)
        return;
    if(!dragging_ && std::abs(y - start_y_) < kDragStart)
        return;
    dragging_          = true;
    const float detents = (last_y_ - y) / kMmPerDetent;
    last_y_             = y;
    dragged_ += detents * TurnGain(drag_rate_.Update(detents, now));
    const int whole = int(dragged_);
    dragged_ -= whole;
    Turn(encoder_, whole);
}

void MouseControl::Release(Clock::time_point now)
{
    if(key_)
        panel_.SetKey(key_, false);
    if(encoder_)
    {
        if(pushed_)
            panel_.SetEncoderPushed(encoder_, false);
        else if(!dragging_)
        {
            panel_.SetEncoderPushed(click_encoder_ = encoder_, true);
            click_ends_ = now + kClickPush;
        }
    }
    key_ = encoder_ = 0;
}

bool MouseControl::Scroll(float x, float y, float steps, Clock::time_point now)
{
    const Hit hit = HitTest(x, y);
    if(hit.kind == Hit::Kind::kUsb)
    {
        battery_scroll_ += steps;
        const int whole = int(battery_scroll_);
        battery_scroll_ -= whole;
        const int mv = std::clamp(int(charger_.BatteryMillivolts()) + whole * int(kBatteryStepMv),
                                  int(kBatteryMinMv), int(kBatteryFullMv));
        charger_.SetBatteryMillivolts(uint32_t(mv));
        charger_.SetChargeDone(uint32_t(mv) >= kBatteryFullMv);
        return true;
    }
    if(hit.kind != Hit::Kind::kEncoder)
        return false;
    // Smooth scrolling sends fractions of a step: add them up.
    scroll_ += steps * TurnGain(scroll_rate_.Update(steps, now));
    const int detents = int(scroll_);
    scroll_ -= detents;
    Turn(hit.index, detents);
    return true;
}

void MouseControl::Cancel()
{
    if(key_)
        panel_.SetKey(key_, false);
    if(encoder_ && pushed_)
        panel_.SetEncoderPushed(encoder_, false);
    key_ = encoder_ = 0;
    EndClick();
}

void MouseControl::Tick(Clock::time_point now)
{
    if(click_encoder_ && now >= click_ends_)
        EndClick();
    if(encoder_ && !dragging_ && !pushed_ && now - pressed_at_ >= kHoldToPush)
    {
        panel_.SetEncoderPushed(encoder_, true);
        pushed_ = true;
    }
}

void MouseControl::Turn(int encoder, int detents)
{
    turned_[encoder - 1] += QueueTurn(panel_, encoder, detents);
}

void MouseControl::EndClick()
{
    if(click_encoder_)
        panel_.SetEncoderPushed(click_encoder_, false);
    click_encoder_ = 0;
}

} // namespace champi
