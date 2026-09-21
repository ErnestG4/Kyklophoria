/* modal_bank.h — the runtime, which is Kyklophoria's.
 *
 * This file was the prototype: a bank of second-order resonators, the
 * pickup, the burst player, the wash, kept in step by hand with
 * core/kyk_resonate.h in Kyklophoria's `modal` branch. Two copies of one
 * thing drifted every time one was fixed (the wash gain, the retune carry,
 * the pickup's flux, the bank loop, the burst's rate) and the fix had to
 * be made twice. There is one copy now — runtime/kyk_resonate.h, verbatim
 * from Kyklophoria; `make check-runtime` says whether it still is — and
 * these are the names the tools here have always used for it. A voice or
 * a world made here is initialised, since the tools make them on the
 * stack; on the module they live in SDRAM and are Init()ed by hand.
 */
#pragma once
#include "kyk_resonate.h"

namespace mb {

using ModalBank   = kyk::ResonatorBank;
using Pickup      = kyk::Pickup;
using BurstPlayer = kyk::BurstPlayer;
using NoiseLayer  = kyk::NoiseLayer;

struct ModalVoice : kyk::ResonatorVoice { ModalVoice() { Init(); } };
struct World : kyk::ResonatorWorld { World() { Init(); } };

} // namespace mb
