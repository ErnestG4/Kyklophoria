# Third-party material

Kyklophoria itself is AGPL-3.0 (`LICENSE`). Nothing below is vendored into
this repository; each is either a build-time sibling or a bake-time input.
Listed so the terms are traceable anyway.

## Build-time siblings, not redistributed here

| | licence | how it is used |
|---|---|---|
| [alchemy-sdk](https://github.com/hermetic-modular/alchemy-sdk) (Hermetic Modular) | MIT | the Alchemy Lab BSP and framework; the firmware links against it, and `shell/common/kyk_ext.h` compiles against its header-only HostLink wire layer |
| [libDaisy](https://github.com/electro-smith/libDaisy) (Electrosmith) | MIT | vendored inside the SDK; CMSIS-DSP arrives with it |
| [Audiothurgist](https://codeberg.org/combust/Audiothurgist) | AGPL-3.0 | same author. The Alchemy Lab shell pattern, the HostLink extension approach and the web wire layer in `web/link.js` derive from it |

MIT permits inclusion in an AGPL work, so the combined firmware is
distributable under AGPL-3.0 with the MIT notices preserved. Both upstreams
ship their own `LICENSE`; neither is copied here because neither is copied
here.

## Bake-time corpora

`tools/kykeigen` reads a corpus and emits a `*.kyk` space. The corpus is an
input to an analysis, not a dependency of the firmware, and the tool does not
copy corpus source into its output — a baked space holds principal components
of log-magnitude spectra, not waveforms.

| corpus | licence | note |
|---|---|---|
| [Braids](https://github.com/pichenettes/eurorack) wave bank, Émilie Gillet / Mutable Instruments | MIT | 256 single cycles read from `braids/resources.cc` in a sibling checkout. The first corpus the eigenspace was baked from |

If a baked space derived from Braids is ever **shipped** rather than generated
locally, credit Émilie Gillet with it. The README already does.

## Not used

No npm packages. No pip packages. The tooling is C++ and the Python standard
library; the two node files under `web/` and `tools/bridge/` use only node's
built-in modules and are optional twins of the Python ones.
