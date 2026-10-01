# Kyklophoria

Firmware for the Hermetic Modular Alchemy Lab (v2). The project builds two
firmwares; the module runs one at a time:

- **Buzzes**: a wavetable oscillator. Each world is a space of spectra, and
  the position CVs move you through it.
- **Bongs**: a polyphonic modal synthesizer. Each world is a set of resonant
  modes fitted to recordings of a real instrument, played by a modelled
  exciter (hammer, pluck, bow, reed or lips).

Status: beta (firmware 0.4.0). Licence: AGPL-3.0.

## Install

### Flash a firmware

The firmware files are `buzzes.bin` and `bongs.bin`. Flash over the front
USB-C port with `dfu-util`:

1. Hold **B3** while powering on, or press it in the first two seconds after
   power-on. The rings switch to a slow breathe: the module is waiting for
   firmware.
2. Run:

   ```sh
   dfu-util -a 0 -s 0x90040000:leave -D bongs.bin -d ,0483:df11
   ```

   Use `buzzes.bin` for Buzzes.

To switch firmwares, flash the other file the same way.

### The SD card

Worlds go in a folder named `kyklophoria` at the root of the card:

- Buzzes reads `.kykw` files (wavetable worlds).
- Bongs reads `.kykm` files (instruments).

Each firmware lists up to 32 files of its own type and ignores names longer
than 30 characters. Bongs plays the first instrument on the card at power-on.

### The web page

Open the page in desktop Chrome or Edge (it uses Web Serial), connect the
module's USB-C port, and press **Serial**:

- Buzzes: https://ernestg4.github.io/Kyklophoria/?page=wavetable
- Bongs: https://ernestg4.github.io/Kyklophoria/?page=modal

The page shows what the module is doing and controls it. If the module runs
the other firmware, the page says so and links the right page.

## Panel and jacks

The panel has six pots and three buttons, shared by both firmwares:

- **B1** steps through the pages. After a page change each pot does nothing
  until it passes the value stored for it (pot catch).
- **B2 + B3 held for two seconds** opens Settings; B2 or B3 leaves it. In
  Settings, P1 is LED brightness, P3 picks one of 16 preset slots, and P4 held
  fully counter-clockwise for three seconds saves the panel to that slot,
  fully clockwise loads it. Slot 0 loads at power-on.

| Jack | Buzzes | Bongs |
|---|---|---|
| J1 | not used | audio into the exciter, amount on Exciter P6 |
| J2 | clock for the world tour | not used |
| J3 | v/oct (0 V = C4) | v/oct (0 V = C4) |
| J4 | CV out A | trigger in; held high, a gate |
| J5–J8 | CV, ±5 V, added to Play P3–P6 | CV, ±5 V, added to Play P3–P6 |
| J9 / J10 | left / right out | left / right out |

On both, Play P1 is coarse pitch (±3 octaves) and P2 fine pitch (±1
semitone).

## Buzzes

### How it works

A world is a space with four axes (up to six). Every point in it is a
spectrum of 64 harmonics. Your position (Play P3–P6 plus J5–J8, and World
P1–P2 for axes 4 and 5) picks a spectrum, which becomes one cycle of waveform
played at the pitch from J3 and P1–P2. Harmonics above the Nyquist limit at
that pitch are dropped.

Before the position reads the world it passes through a rotation. The four
axes form six rotation planes, set on the Rotate page, so one CV can sweep a
diagonal that no single axis reaches. The Orbit page turns the planes
continuously, and the Kepler page moves the position along an orbit.

For stereo, the left and right channels read the world at angles either
side of your position in one rotation plane (Stereo P1–P2). At zero spread
the output is mono.

Some worlds are formulas, evaluated wherever you stand. Others are sampled
grids of spectra, blended between points.

### Pages

| | Play | Rotate | Stereo | Orbit | Kepler | Couple | World |
|---|---|---|---|---|---|---|---|
| P1 | Coarse | Angle 0,1 | Spread | Orbit 0,1 | Gravity | Coupling | Position 4 |
| P2 | Fine | Angle 0,2 | Stereo plane | Orbit 0,2 | Eccentricity | Reach | Position 5 |
| P3 | Position 0 | Angle 0,3 | Morph | Orbit 0,3 | Orbit plane | Rate | Tour division |
| P4 | Position 1 | Angle 1,2 | Render div | Orbit 1,2 | Softening | Bodies | Tour glide |
| P5 | Position 2 | Angle 1,3 | Level | Orbit 1,3 | Damping | Company | Tour free-run |
| P6 | Position 3 | Angle 2,3 | CV out A depth | Orbit 2,3 | Radius | World morph | |

- **Rotate**: the angle of each rotation plane.
- **Orbit**: how fast each plane turns. Centre is stopped. Either side runs
  from 0.01 to 1 turn per second, in opposite directions.
- **Kepler**: a body orbiting in one plane (Orbit plane) moves the position.
  Gravity is off at the bottom of its range. Turning it up, or moving
  Radius, starts the orbit. Softening sets how much the orbit's ellipse
  turns each revolution.
- **Couple**: Coupling pulls the orbit rates toward simple ratios of each
  other, Reach (1–5) is the largest whole number those ratios may use, and Rate multiplies all
  six rates (three octaves either way, with a detent at 1×). Bodies (1–8) and
  Company add more bodies to the Kepler orbit and set their mass.
- **Stereo**: Morph sets how sharply the world blends between its points,
  from smooth to hard steps. Render div renders a new cycle every 2, 3, 4 or
  6 audio blocks, trading smoothness for CPU. CV out A depth scales J4's
  output.
- **World morph** (Couple P6) blends toward a second world, the morph target,
  chosen on the web page.
- **World tour** (World P3–P5): a loop of up to eight worlds, set up on the
  web page, stepped by clock edges on J2. Division counts edges per step;
  glide sets how much of each step the blend spends moving (at least 4 ms);
  free-run steps without a clock.

### Built-in worlds

Twenty-two worlds are built in; Buzzes starts on Crop. Choose a world on the
web page.

| World | What it is |
|---|---|
| Crop | the first four principal components of a 256-wave bank (Braids, Émilie Gillet) |
| 24-cell | a waveform on each of the 24 vertices of the 4-D solid |
| 16-cell | eight vertices, one per axis direction |
| Tesseract | sixteen vertices, on the cube corners |
| Stack | one waveform idea per axis |
| Field | correlated noise |
| Field II | the same, rougher |
| Torus | a field whose axes all wrap |
| Harmonic | one parameter per axis: saw to square, tilt, a formant |
| FM | FM sidebands: index, ratio, carrier, and a second carrier |
| Vowel | three chained resonances |
| Shapes | saw, square, triangle and pulse on a grid, with wavefolding and phase distortion |
| Shapes R | the same grid, with wavefolding and ring modulation |
| Lock | real waveforms on the 24-cell, one family per rotation plane |
| Unison | 1 to 7 stacked voices, spread from dense to wide |
| Plate | a struck plate |
| Bar | a struck bar, tuned to the harmonic series |
| Drum | a struck membrane, tuned |
| Saw | a saw: tilt, parity, comb, fold point |
| Pulse | a pulse: duty, tilt, comb, fold point |
| Edge | a blend of saw and pulse |
| Grit | the Shapes grid with bit and sample-rate reduction; it aliases on purpose |

### Your own worlds

On the web page:

- **build** tab: import single-cycle WAV files (up to 24; each file is one
  cycle), then drag each one to where you want it in the space. **play it
  now** sends the world to the module; **add to worlds** keeps it in a slot;
  **save** writes a `.kykw` file to your computer. An effect (wavefold, ring
  modulation, phase distortion, bit reduction or rate reduction) can go on axis
  4 or 5. The tab plays any waveform three ways (as imported, band-limited,
  and as the module renders it) so you can hear what the import kept.
- **worlds** tab: the built-in worlds and the module's 32 slots. Play a world,
  set it as the morph target, open it in the build tab, save a slot to the SD
  card, or load a card file into a slot. Dropping a `.kykw` file on a slot
  loads it. The tour is set up here too.

Slots are held in RAM and cleared at power-off; save to the card to keep a
world. The module creates the `kyklophoria` folder if it is missing and will
not replace a file with the same name unless you confirm it on the page.

## Bongs

### How it works

A world is an instrument: for each note, a set of resonant modes (frequency,
decay and level) fitted to recordings of that instrument. Some worlds are a
family of several instruments. An exciter drives the modes, and the modes
ring as the instrument would.

The four Play-page axes (P3–P6, plus J5–J8) shape the sound:

| Axis | Pot / jack | What it does |
|---|---|---|
| Body | P3 / J5 | the instrument within a family; on a world with a modelled pickup, the pickup's position; on other note worlds, where along the string it is struck; in a row of bodies, which body |
| Velocity | P4 / J6 | the velocity of the next strike, and the energy (bow speed, breath) of the sustained exciters |
| Decay | P5 / J7 | how long every mode rings, from 1/256 of the recording's decay to 4 times it |
| Coil | P6 / J8 | on a world with a modelled pickup, the coil's resonance (half to double); on other note worlds, brightness (±3 dB per octave) |

On the panel and the page these pots are labelled Position 0–3.

### Playing

- **Strike** with a trigger into J4 (rising past 1 V), a tap on B2, or the
  pads on the web page. With nothing in J4 for two seconds, a pitch jump of
  more than 0.4 semitone also strikes, so a sequencer's pitch CV alone plays
  notes. A strike waits up to 30 ms for the pitch CV to settle.
- **Pitch lock** (B3, or Resonator P5): locked, each note takes the nearest
  semitone when struck and keeps it, and the coarse and fine pots move in
  whole steps. Bend follows the pitch exactly.
- **Voices** (Resonator P1): 1, 2 or 4. A new note takes the next voice; a
  repeated note strikes its own voice again.

### Exciters (Exciter P1)

| Type | What it does |
|---|---|
| Recorded | plays the recorded attack from the instrument's recordings |
| Hammer | a modelled felt hammer |
| Pluck | a modelled finger plucking the string |
| Bow | a bow, held on the string while there is energy |
| Reed | a reed, blown while there is energy |
| Lips | buzzing lips, blown while there is energy |
| Trained | a hammer trained note by note to the recordings, on worlds that have one; the recorded attack elsewhere |

Trained is the default. The other Exciter pots shape the active type:

- **Timbre**: the felt's hardness, the plucking finger's stiffness, the bow's
  pressure, the reed's embouchure, or the lips' register.
- **Position**: where the hammer, finger or bow meets the string.
- **Noise**: the contact noise of the hammer and the pluck.
- **Mass**: the hammer's mass, how hard the finger pulls before it lets go,
  the reed's air-column impedance, or the lips' Q.
- **J1 in** (P6): how much of J1's audio is added as a force on the string
  or air column.

**Bow, Reed and Lips** play the most recent note continuously, with Play P4
(and J6) as the energy. At zero energy the note rings freely. Holding J4
high, holding B2, or holding a pad on the web page makes a gate: the
exciter plays while the gate is held and lets the note ring when it is
released. After a gate has been used, it controls the exciter for ten
seconds; with no gate in use, energy alone controls it.

### Pages

| | Play | Exciter | Resonator | Space | Motion | Kepler |
|---|---|---|---|---|---|---|
| P1 | Coarse | Exciter | Voices | Spread | Body-Velocity | Gravity |
| P2 | Fine | Timbre | Release | Listen | Body-Decay | Eccentricity |
| P3 | Body | Position | Dig in | Ear orbit | Body-Coil | Orbit plane |
| P4 | Velocity | Noise | Family | | Velocity-Decay | Damping |
| P5 | Decay | Mass | Pitch | | Velocity-Coil | Radius |
| P6 | Coil | J1 in | Level | | Decay-Coil | Coupling |

- **Resonator**: Release is how long a note fades when its voice is taken by a
  new note (5 ms to 1 s). Dig in strikes harder the faster you play. Family
  switches between the instruments of a family, or morphs between them.
  Pitch is Lock or Bend.
- **Space**: stereo from two listening points along the string. Spread sets
  how far apart they are (zero is mono), Listen sets where their centre is,
  and Ear orbit picks which Motion rotation swings that centre back and forth.
  Worlds with a modelled pickup play in mono.
- **Motion**: each pot turns one pair of axes, moving the sound through body,
  velocity, decay and coil on its own (centre is stopped).
- **Kepler**: an orbiting body moves the position in one pair of axes.
  Coupling pulls the Motion rates toward simple ratios.

### The web page

- **play** tab: the instruments in the module's slots, a list of the card's
  instruments to load, the modes of the note playing, strike pads (each
  works as a gate while held), and a row of sliders and menus for every panel
  page. Moving one moves the module's pot value; the pot catches it when you
  next turn it.
- **worlds** tab: the module's 32 slots and the card.

## Build from source

Requirements: `arm-none-eabi-gcc`, `dfu-util`, a C++ compiler, Python 3, and
[alchemy-sdk](https://github.com/hermetic-modular/alchemy-sdk) v0.11 cloned
next to this repository (`../alchemy-sdk`). The firmware needs one change to
the SDK that is not yet released: `AlchemyLabV2::Init(sample_rate,
block_size, boost)` passing the boost flag through to `DaisySeed::Init`
(480 MHz). Node.js is optional (web page tests). npm is not used.

```sh
cd shell/alchemy
make libdaisy                 # once, after cloning the SDK
make MODE=modal               # Bongs:  build-modal/bongs.bin (the default)
make MODE=wavetable           # Buzzes: build-wavetable/buzzes.bin
make both                     # both
make MODE=modal program-dfu   # flash, with the module waiting in DFU mode

cd ../..
make host                     # desktop tools in build/host
make test                     # the test suite
KYK_NODE=1 make test          # plus the web page tests
```

To run the web page locally: `python3 -m http.server 8080 -d web`, then open
http://localhost:8080/?page=modal or `?page=wavetable`.

Bongs worlds are built with ModalBake, on the `modalbake` branch of this
repository.

## Repository layout

- `core/`: the engine, header-only, no hardware, no allocation.
- `shell/alchemy/`: the module firmware.
- `shell/desktop/`: a desktop build that renders to WAV and serves the web
  page's protocol over stdio.
- `shell/common/`: code both shells share.
- `web/`: the web page (one static HTML file and its link layer).
- `tools/`, `tests/`, `docs/`: tools, the test suite, design notes.

## Credits

Combust: design and development. Émilie Gillet / Mutable Instruments: the
Braids wave bank behind the Crop world. Luke / Hermetic Modular: the Alchemy
Lab and its SDK. The shell, HostLink transport and web wire layer come from
[Audiothurgist](https://codeberg.org/combust/Audiothurgist).

## Licence

AGPL-3.0 (`LICENSE`). Third-party terms are in `THIRD_PARTY.md`: the Alchemy
SDK and libDaisy are MIT and are build-time dependencies, not included here;
the Braids wave bank is MIT.
