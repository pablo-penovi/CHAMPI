#include "card_slot.h"

#include "daisycola/host.h"

namespace champi
{
void CardSlot::SetInserted(bool inserted)
{
    inserted_.store(inserted);
    daisycola::SdSetPresent(inserted);
}

} // namespace champi
