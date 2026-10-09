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
            dragged_    = 0;
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
    dragging_       = true;
    const int total = int((start_y_ - y) / kMmPerDetent);
    Turn(encoder_, total - dragged_);
    dragged_ = total;
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

bool MouseControl::Scroll(float x, float y, float steps)
{
    const Hit hit = HitTest(x, y);
    if(hit.kind != Hit::Kind::kEncoder)
        return false;
    // Smooth scrolling sends fractions of a step: add them up.
    scroll_ += steps;
    const int detents = int(scroll_);
    scroll_ -= detents;
    Turn(hit.index, detents);
    return true;
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
    if(!detents)
        return;
    panel_.TurnEncoder(encoder, detents);
    turned_[encoder - 1] += detents;
}

void MouseControl::EndClick()
{
    if(click_encoder_)
        panel_.SetEncoderPushed(click_encoder_, false);
    click_encoder_ = 0;
}

} // namespace champi
