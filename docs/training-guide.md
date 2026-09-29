# Training guide: fitting exciter and resonator worlds from recordings

How ModalBake turns sample libraries into the `.kykm` worlds that
Kyklophoria's resonator mode plays, written so that someone who has never
seen the project can set it up and run it. Everything here was checked
against the code, the campaign scripts and the git history on 28 September
2026. Where something is not recorded in the repository, the guide says so
instead of guessing.

"Training" here means fitting a small physical model to each note's
recording. There is no neural network, and nothing is learned across
notes. Every note of every instrument gets its own model, whose numbers
are found by gradient descent on the GPU (torch, Adam): the optimiser is
only how the numbers are searched for, and any search would do (the
exciter trainer, excfit, uses Nelder-Mead and no gradients at all):

- **The resonator** is a bank of decaying sinusoids: a frequency, decay,
  amplitude and phase for each mode. Electric pianos also get a pickup
  stage.
- **The exciter** is today a stored attack burst: the first 60-400 ms of the
  recording itself, cut and faded. The modes ramp in underneath it.

The coupled physical exciters (hammer, pluck, bow, reed, lips) exist as
tested prototypes. They are not fitted to anything, and the engine does not
use them yet (see section 5).

---

## 0. The pipeline at a glance

```
 samples/  (recordings, gitignored)
    |
    |  organise.py / splitnotes.py / pitchman.py        -> out/gen/<set>/<dyn>/<Note>.wav
    |  labelcheck.py  (moves out notes whose pitch disagrees with their name)
    v
 per-note fit on the GPU
    |  fitset.py   (plain modes, one record per take)   -> out/fit/<set>/<id>.mmr + fits.tsv
    |  fitvel.py   (electric: all takes of a note jointly, metal + pickup)
    v
 the recorded attack ("exciter")
    |  bursts.py --ms auto   (trim, sign, cut + fade the burst, anchor modes at the seam)
    v
 grading and repair
    |  gate.py, fitcheck.py, pitchcheck.py, fundcheck.py, ringers.py
    |  refitn.py  (refit bad records seeded from good neighbours) -> bursts.py again
    v
 export to the module format
    |  export.py records <set> <world>.kykm [--gate] [--clean] [--voice] [--level-of=..]
    |      layer() velocity layers, intune, align, voicing, body curve, headroom
    |  export.py family  (strings of one instrument, or a row of instruments)
    v
 out/worlds/*.kykm   (format v6/v7)
    |  card.py + manifests/card.tsv   -> out/card/kyklophoria/   (at most 24 worlds, gated)
    |  build/worldfill  (a point for every semitone, format v8)  -> out/card8/kyklophoria/
    v
 SD card /kyklophoria/  ->  Kyklophoria (modal branch), or kykdesk --resonate on the desktop
```

What each stage learns from the recording, and what is set by hand:

| part | how it is obtained | where |
|---|---|---|
| mode frequencies, decays, amplitudes, phases | **fitted** by Adam on a multi-resolution STFT loss, per note | `modalfit.fit` |
| mode decays | **fitted**, but held within a band of the decay measured on each partial's STFT bin track | `MB_DECAY_PRIOR`, `MB_DECAY_BAND` |
| pickup (voicing h, field width w, coil gain K, coil fc and Q) and one swing per take | **fitted** jointly across a note's velocity takes. The form (magnetic `bell` or electrostatic `gap`) is chosen by hand | `modalfit.fit_shaped`, `fitvel.py` |
| attack burst | **cut from the recording**, not optimised. Its length, fade and thump threshold are hand-set rules | `bursts.py` |
| velocity layers | measured: each take's level relative to the loudest take, read from the source files | `export.layer()` |
| tuning, voicing, headroom, body curve | rules and measurements applied at export | `export.py`, `body.py` |
| wash (noise bands of dense bodies such as gongs) | measured per octave band from the residual; used only on acoustic body rows | `noise.py` |
| synthesised exciter (proto/, bench only) | contact model hand-set by a keyboard law. Its noise bands and the modes' ramp are fitted by least squares on the CPU | `proto/fit.py` |
| coupled exciters (Kyklophoria) | physics with hand-set parameters, tested on synthetic resonators, never fitted | `core/kyk_exciter.h` |

---

## 1. Setup

### 1.1 Machine

The reference machine:

- **OS:** Ubuntu 24.04 under WSL2 on Windows. WSL is capped at 12 GB of RAM (the campaign scripts say so), which is what limits how many fits can run at once. Any Linux should work.
- **GPU:** NVIDIA RTX 4090 (24 GB), Windows driver 596.49.
- **Python:** 3.12.3 in a venv at `~/fmexplorer`. It is referenced by path everywhere (`PY=${PY:-$HOME/fmexplorer/bin/python}` in every script, and `PYFIT` in the Makefile), so either create a venv at that path or pass `PY=/your/python` to the scripts and `PYFIT=...` to make.

The fitters fall back to the CPU when `torch.cuda.is_available()` is false
(`device = 'cuda' if ... else 'cpu'`). Nobody has timed a CPU run, so expect
it to be much slower.

### 1.2 Python packages

The venv needs `torch` (with CUDA), `numpy`, `scipy` and `soundfile`
(libsndfile, which decodes wav, aiff, flac and mp3). Those four are
everything the tools import; nothing else is required. The reference venv
holds:

```
torch 2.11.0+cu130 (CUDA 13.0)   numpy 2.4.4   scipy 1.17.1   soundfile 0.13.1
```

A fresh setup, for example:

```sh
python3 -m venv ~/fmexplorer
~/fmexplorer/bin/pip install torch numpy scipy soundfile   # a CUDA build of torch for your driver
~/fmexplorer/bin/python -c "import torch; print(torch.cuda.is_available())"
```

**Which Python runs which tool.** The README's rule is "Python stdlib,
except the fitters". In practice most tools need numpy, scipy or soundfile:
organise, splitnotes, labelcheck, pitchman, pitchcheck, notecheck, bursts,
export, ringers, noise, body, playvel, earcheck, specaudit, subset and
fitcheck. Run all of them with the venv's Python. Only a few are genuinely
stdlib-only, and the scripts run those with `python3`:

- `gate.py`
- `card.py`
- `fundcheck.py`
- `pack.py`
- `meshgen.py`

### 1.3 C++ toolchain and the sibling checkouts

You need g++ with C++17 (the reference machine has 13.3). `arm-none-eabi-g++`
is needed only for `make armcost`, which counts M7 cycles; training does not
need it.

The tools find their neighbours by relative path, so the checkouts must sit
side by side:

```
<parent>/
  ModalBake/        (branch bake)   this repo
  Kyklophoria/      (branch modal)  notecheck.py and specaudit.py run ../Kyklophoria/build/host/kykdesk;
                                    make check-runtime compares against ../Kyklophoria/core/kyk_resonate.h
  alchemy-sdk/      needed by Kyklophoria's Makefile to build kykdesk (SDK_DIR ?= ../alchemy-sdk)
  epi/              optional: DatanoiseTV's Epi, for build/epigen (the synthetic tine and reed sets)
  faust/, eigen/    optional: only for the FEM corpus stages (make corpus/align/bake), not for training
```

Build what the training pipeline calls:

```sh
cd ModalBake
make -C ../Kyklophoria build/host/kykdesk     # the desktop renderer (notecheck, specaudit, proto)
make build/modaltest                           # export.py uses it to check headroom through the runtime
g++ -std=c++17 -O2 -Wall tools/worldfill/main.cpp  -o build/worldfill    # no make rule exists
g++ -std=c++17 -O2 -Wall tools/worldaudit/main.cpp -o build/worldaudit   # no make rule exists
make build/epigen                              # optional, needs ../epi
make check-runtime                             # runtime/kyk_resonate.h must equal Kyklophoria's
```

- **No make rule for worldfill or worldaudit.** The Makefile does not build
  `worldfill` or `worldaudit`. The two lines above are inferred from the
  sources, which include `../../runtime/kyk_resonate.h` and need nothing
  else, and from the flags of the `build/modaltest` rule.
- **Keep the runtime copy in sync.** `runtime/kyk_resonate.h` is meant to
  be a verbatim copy of `../Kyklophoria/core/kyk_resonate.h`, and everything
  in `build/` that renders through it (`modaltest`, `worldfill`,
  `worldaudit`, `proto`) uses the copy. Run `make check-runtime` before
  exporting. If it fails, copy the newer header over the older one and
  rebuild those binaries. `export.py` calls `build/modaltest` directly, so
  an old binary silently checks headroom against an old runtime.

### 1.4 Where recordings go

Recordings live under `ModalBake/samples/`, which is gitignored. Organised
and split notes go to `out/gen/`, which is also ignored.

**Nothing derived from a recording may be committed.** That includes
`-target.wav`, `-resynth.wav`, `-burst.wav`, A/B renders and every `.kykm`,
because a world carries its bursts, which are recording (see
`SOURCES.md`). The repository has no remote.

---

## 2. Source data

### 2.1 The libraries

| set | licence (SOURCES.md) | on disk | used for |
|---|---|---|---|
| University of Iowa MIS | "used for any projects, without restrictions" | `samples/foss/iowa/{Piano_Other,Strings,Percussion}` | piano (pp/mf/ff), guitar and pizzicato strings string by string, marimba, vibraphone, xylophone, bells, crotales |
| Versilian VCSL | CC0 | `samples/foss/vcsl` (git clone of `https://github.com/sgossner/VCSL.git`) | Steinway B (Sus / NoSus), Knight and Yamaha uprights, harpsichords, harps, kalimbas, mbira, glockenspiel, tubular bells, hand chimes, balafon, strumstick |
| VSCO 2 CE | CC0 | `samples/foss/vsco2` (`git clone --depth 1 https://github.com/sgossner/VSCO-2-CE.git`) | harp |
| Salamander Grand V3 | CC-BY 3.0 | `samples/foss/salamander` (freepats.zenvoid.org) | grand, 4 of 16 velocity layers |
| Philharmonia Orchestra | CC BY-NC-SA (**not shippable**) | `samples/philharmonic-all-samples/<instrument>/` | guitar, banjo, mandolin, and the older violin/viola/bass |
| CNCD Wurlitzer | non-commercial, no redistribution (**not shippable**) | `samples/CNCD samples/real/wurlitzr` | `wurli` |
| an EP multisample (MAX/MED folders) | "commercial EP libraries: proprietary" (**not shippable**) | `samples/EP/{MED,MAX}` | `ep`, `ep-vel` |
| Syntec "Wall of Sounds Vol. 1", Stereo Grand | not listed in SOURCES.md | `samples/Syntec - Wall of Sounds Vol. 1/Stereo Grand   X` | `piano` (34 notes) |
| Epi (generated, no recording) | see section 10: the licence statements disagree | `out/gen/tine`, reed set | `tine-vel`, `reed-vel` |

For a reproduction you can share, use only Iowa, VCSL, VSCO 2 and Salamander.
The card in `manifests/card.tsv` also carries worlds from the restricted
sets.

**The fetch scripts are not in git.** They sit in the ignored
`samples/foss/`. Iowa's (`samples/foss/fetch_iowa.sh`) is, in substance:

```sh
B=https://theremin.music.uiowa.edu
for p in MISpiano MISguitar MISviolin MISviola MIScello MISdoublebass Mismarimba Misvibraphone MISxylophone MIScrotales MISbells; do
  curl -s "$B/$p.html" | grep -ioE 'href="sound files[^"]*\.(aif|aiff)"' | sed 's/^href="//; s/"$//' | sort -u > "iowa/$p.list"
  while IFS= read -r rel; do
    out="iowa/$(echo "$rel" | sed 's|sound files/MIS/||; s| |_|g')"
    mkdir -p "$(dirname "$out")"; [ -s "$out" ] && continue
    curl -s --retry 3 -o "$out.part" "$B/$(echo "$rel" | sed 's| |%20|g')" && mv "$out.part" "$out"
  done < "iowa/$p.list"
done
echo "== all done" >> iowa/fetch.log      # foss2.sh waits for this line
```

`fetch_more.sh` downloads:

- `https://freepats.zenvoid.org/Piano/SalamanderGrandPiano/SalamanderGrandPiano-SFZ+FLAC-V3+20200602.tar.gz`, unpacked into `salamander/`
- the Upright Piano KW, which is not used
- VSCO 2, cloned as above

The repo does not record where the Philharmonia, CNCD, EP and Syntec files
were downloaded from.

### 2.2 The layouts the fitter reads

`fitset.py --layout` accepts three layouts:

- **`folders`** (used for almost everything): one subdirectory per dynamic, files named by note.

  ```
  out/gen/<set>/pp/C4.wav, .../mf/C4.wav, .../ff/C4.wav
  ```

  The note regex is `^[a-gA-G][#sb]?-?\d+`, so `c#3 max.wav` and `G#2.ff.wav` both parse.
- **`philharmonia`**: `<instrument>_<note>_<length>_<dynamic>_<articulation>.mp3` in one directory. `--articulation` picks one (e.g. `pizz-normal`), and `phrase` files are skipped.
- **`manifest`**: a `fits.tsv` beside the files (`id family param value dynamic`) for sets whose file names carry no note. The shipped manifests live in `manifests/` because `samples/` is not in git:

  ```sh
  cp manifests/wurli.tsv "samples/CNCD samples/real/wurlitzr/fits.tsv"
  ```

  For `manifests/piano.tsv` (Syntec), copy the files into `out/gen/piano/` as `pianoNNN.flac` beside it. See `manifests/README.md` for the off-by-one in pitchman's numbering.

### 2.3 Preparing notes

**One file per note (Iowa piano, VCSL, Salamander).** Use `organise.py`.
It reads the note and dynamic out of each library's naming and writes the
folders layout, keeping the first round-robin take of each note:

```sh
PY=~/fmexplorer/bin/python
$PY tools/organise.py out/gen/piano-iowa samples/foss/iowa/Piano_Other/piano/*.aif*          # -> pp/ mf/ ff/
$PY tools/organise.py out/gen/upright-knight-o --octave 1 "samples/foss/vcsl/Chordophones/Zithers/Upright Piano, Knight/Sustains"/*.wav
```

- **VCSL octave naming.** Most VCSL instruments call middle C "C3", so
  they need `--octave 1`. The Steinway (`piano-vcsl`) and the two harps
  were named correctly without it.
- **Salamander.** `organise.py` parses Salamander's `F#3v2` names into
  `v01`...`v16` folders. `foss3.sh` then symlinks four of them into one
  set:

  ```sh
  mkdir -p out/gen/piano-sal4
  for v in v04 v08 v12 v16; do ln -sfn ../piano-salamander/$v out/gen/piano-sal4/$v; done
  ```
- **Not recorded.** The exact `organise.py` lines that made
  `out/gen/piano-salamander`, `piano-vcsl` and `piano-vcsl-nosus` are not in
  any script. They follow the same pattern, over
  `salamander/SalamanderGrandPiano-SFZ+FLAC-V3+20200602/samples/*.flac` and
  `vcsl/Chordophones/Zithers/Grand Piano, Steinway B/{Sus,NoSus}`.

**Chromatic runs, several notes per file (Iowa strings, guitar,
mallets).** Use `splitnotes.py`. The file name gives only the range (for
example `Violin.pizz.ff.sulG.G3B3.aiff`). Each segment is labelled by its
own detected pitch and kept only if it lies within the range, within 0.35
semitone of a note, and above the previous note kept. Each cut is then
walked back to the silence before the attack.

```sh
I=samples/foss/iowa
$PY tools/splitnotes.py out/gen/bass-pizz3-ff $I/Strings/doublebass/*pizz*.aif* --per-string --dynamic ff --articulation pizz
$PY tools/splitnotes.py out/gen/marimba-m-ff  $I/Percussion/Marimba/*.yarn.ff.*.aif* --dynamic ff
```

- `--per-string` writes one directory per string (`sula`, `suld`, ...).
- `--dynamic` and `--articulation` keep exactly one kind of take per set.
- Each directory gets a `splits.tsv` with one row per note: output file,
  source file, start sample, how far the cut moved back, articulation.
  Check it for notes that start far into their file (see section 9).

The campaign scripts then assemble dynamics into a folders set with
symlinks, for example from `foss6.sh`:

```sh
mkdir -p out/gen/bass-pizz3-sula
for dyn in pp mf ff; do ln -sfn ../bass-pizz3-$dyn/sula out/gen/bass-pizz3-sula/$dyn; done
```

**Check every label before fitting.** The fitter seeds each note's
fundamental from its label and keeps it, so a wrong label turns into a
false mode that no later relabel removes:

```sh
$PY tools/labelcheck.py out/gen/upright-knight-o          # moves notes >0.7 st off their name to <dir>-rejected/
```

**Sets with no note names.** `pitchman.py` pitches every file (lowest line
with a harmonic series) and writes a `manifest`-layout `fits.tsv`. Its
printed table is meant to be read by eye:

```sh
$PY tools/pitchman.py piano "samples/Syntec - Wall of Sounds Vol. 1/Stereo Grand   X" --copy out/gen/piano
```

---

## 3. The resonator: per-note fits

### 3.1 How one note is fitted (`modalfit.py`)

For each file, `fitset.py` imports `modalfit` (torch starts once) and does
the following:

1. **Load.**
   - The onset is the envelope peak, walked back to the last 5 ms that sat 20 dB below it, less 10 ms. The file must reach `--onset` dBFS at all (fitset default -40).
   - The window runs from the onset until the level falls 40 dB, or `--max-seconds` if that comes first.
   - The audio is high-passed at 40 Hz (8th-order Butterworth, both ways) and peak-normalised.
2. **Initialise.**
   - Candidate modes are the peaks of the mean STFT magnitude (8192 points, hop 512).
   - The frequency of each comes from the phase-vocoder advance of its bin, and the decay from a line through its log-magnitude track.
   - A candidate must pass three gates: prominence (8 dB over the two-octave median, `MB_PROMINENCE_DB`), phase coherence, and decay.
   - Near-coincident pairs, such as a mandolin course, are split on a 0.7 Hz fine spectrum.
   - The labelled fundamental (f0 from the note) is always the first candidate and is never gated.
3. **Fit.**
   - A differentiable renderer: a sum of decaying sines under a 3 ms raised-cosine attack.
   - The loss is a three-scale log/linear STFT loss (4096/1024, 1024/256, 256/64), made of three terms:
     - spectral convergence;
     - log-magnitude L1 at 0.1 weight;
     - an asymmetric "excess" term that penalises energy where the recording sits at its own floor.
   - The first `MB_BURST_MS` (60 ms) of frames are weighted down to `MB_BURST_FLOOR` (0.3), because the burst will carry the attack.
   - Adam with a cosine schedule; learning rate 0.02 for decay, amplitude and phase, and 0.0005 for log-frequency (the loss is a comb in frequency).
   - A decay prior (`MB_DECAY_PRIOR` 2.0) keeps log decay within `MB_DECAY_BAND` (ln 1.5) of the measured track.
   - A cluster penalty (`MB_CLUSTER` 1.0) stops pairs of antiphase sines within 1% from building the attack.
   - Nothing may die faster than 5 ms.
4. **Validate and polish.**
   - Drop modes more than 60 dB under the loudest at 10 ms (`MB_AUDIBLE_DB`), and modes that fail validation; always keep the fundamental.
   - Refit the survivors for `--polish` steps (default half of `--steps`) from their own phases.
   - T60 is capped at `--t60-cap` (8) times the window.

### 3.2 Running a set (`fitset.py`)

```sh
nice -n 10 $PY -u tools/fitset.py <family> <indir> <outdir> --layout folders --resume --steps 1200 --max-seconds <s>
```

The flags the campaigns use:

- `--steps 1200`: the default is 800, and the 2026-09-21 refit used 2000.
- `--max-seconds`: 3-6 by instrument. Plucked strings get 3-5, mallets 4-6, pianos 5.
- `--resume`: skip records already written. A 250-record set takes hours, and the machine has gone down mid-set.
- `--octave 1`: for sets that call middle C "C3". The EP needs it.
- `--onset -65`: for very quiet takes. The Iowa grand's pp layer peaks at -51 to -30 dBFS.
- `--only id,id`: refit chosen records in place and keep the other manifest rows.
- `--keep-ids`: name records after their files (the manifest sets).
- `--body N`: fit N broad body modes to the set's mean residual. Not used by the current campaigns.

The records all go into the output directory:

```
out/fit/<set>/fits.tsv             id family param value dynamic source modes loss excess_db decay_ratio
out/fit/<set>/<id>.mmr             the record
out/fit/<set>/<id>-target.wav      the analysed excerpt at half scale (bursts.py later trims it)
out/fit/<set>/<id>-resynth.wav     the fitted model
```

- `loss` is the fitter's final loss.
- `excess_db` is the "whistle" metric: the model's energy where the recording has none.
- `decay_ratio` is the energy-weighted ratio of fitted T60 to measured T60.

**The `.mmr` record** is plain text:

```
# modalfit record via fitset: ff/G#2.ff.wav, 2.39 s analysed, loss 1.0920, excess 0.01 dB, decay ratio 1.16
source out/gen/marimba-mset/ff/G#2.ff.wav
fitted 1
loss 1.09198
excess_db 0.011
decay_ratio 1.163
positions 12
modes 20
body 0
mode 0 hz 104.513902 zeta 0.00274278535 phase 5.90513 gains 0.4335 0.6886 ... (12 gains)
...
signed 1                         <- added by bursts.py: the sign has been decided
trimmed 1080                     <- added by bursts.py: samples cut from the front of the window
burst marimba050-burst.wav 2880  <- added by bursts.py: the attack file, and the fade length in samples
```

- `zeta` is the damping ratio (rate / 2πf).
- The 12 gains exist because the FEM records have 12 positions. A recording
  has one position, so the fit writes the same amplitude 12 times; after
  `bursts.py` the first gain is the trimmed and anchored one (see section 10).
- Shaped records (section 3.3) add `shaped 1`, a
  `shaper bell|gap H W K FC Q` line and one `take <dir> swing G level L` line
  per take.
- `noise.py` adds `noise L0 T0 ... L7 T7`.

### 3.3 Velocity

There are two different mechanisms.

**(a) Acoustic sets: layers.** Fit every dynamic of a note as its own record
with `fitset.py` (the `folders` layout, one subfolder per dynamic). At export,
`export.layer()` groups the records that share a note into one point:

- The loudest take supplies the modes.
- Every take supplies its own burst, at a swing equal to its source RMS over the loudest take's.
- The runtime crossfades the two bursts that bracket a strike's velocity over the shared modes.

This needs the source files on disk, since the levels are read back from
them. Every `-vel` world on the card is built this way:
`piano-iowa3`, `*-pizz3-*`, `guitar3-*`, `marimba-vel`, and so on.

**(b) Electric pianos: the pickup stage (`fitvel.py`).** A tine or reed
moves almost as a pure sine; the harmonics at the jack come from the pickup.
One velocity cannot separate the two, but several takes of the same note
can. `fitvel.py` fits all takes of a note jointly (`modalfit.fit_shaped`):

- **The metal.** The modes are shared across takes. With `--bar`, only the inharmonic partials are kept, plus the fundamental.
- **The swings.** One swing gain per take, softest first. `MB_MONO` penalises a harder take that swings less.
- **The field.** `--form bell` (magnetic, 1/(1+((u-h)/w)²)) or `--form gap` (electrostatic plate).
- **The coil.** Faraday (a first difference), then a resonant second-order low-pass at (fc, Q).
- **Level.** A loudness term per take, `MB_LEVEL`, unless `--normalised`.

The recorded EP, from `tools/refit.sh`:

```sh
$PY -u tools/fitvel.py epv samples/EP out/fit/ep-vel --order MED,MAX --octave 1 --normalised --bar --coil-from-set --steps 800
```

- `--coil-from-set` holds every note's coil to the median fc and Q of the
  set's *previous* fit, read from `out/fit/ep-vel/fits.tsv`. The first pass
  on a new set must therefore run without it.
- `--normalised` gives each take its own output gain, because the library
  levelled its samples.

The synthetic sets from Epi, from `docs/findings.md`:

```sh
make build/epigen && build/epigen out/gen/tine --instrument 0      # tine piano, 6 velocities a note
$PY tools/fitvel.py tine out/gen/tine out/fit/tine-vel --bar
```

- `reed-vel` was fitted with `--form gap`. Its epigen command line is not
  recorded; `--instrument 1` is a guess based on the order of Epi's
  instruments.
- `fitvel.py` defaults: `--modes 24 --steps 1200 --seconds 3`. `--only g1,g#1`
  refits notes in place.

---

## 4. The exciter as it ships: the recorded attack (`bursts.py`)

A modal bank cannot reproduce a hammer's thump, a pluck's scrape or the dense
partials above its 48 modes. The shipping answer is commuted synthesis: store
the recording's own attack and bring the modes in underneath it.

```sh
$PY tools/bursts.py out/fit/<set> --ms auto            # plain records
$PY tools/bursts.py out/fit/ep-vel --shaped --ms auto  # a fitvel set: a burst per take
```

The code does the following for each plain record:

1. **Trim.** Cut the window so the strike starts 1 ms in: 1 ms before the first sample within 34 dB of the peak. Carry every mode's phase and amplitude to the new start, and rewrite `-target.wav`. The record gets `trimmed <n>`, so the trim is done only once.
2. **Drop the thump.** Remove modes with T60 under `--thump` (0.06 s). They only existed to imitate the hammer, which the burst now carries.
3. **Set the sign.**
   - The STFT loss cannot see a sign, so half the fits came out as the negative of their recording.
   - Whichever sign leaves less residual over the burst is taken, and the phases are turned by π if needed.
   - The record gets `signed 1`, and the choice is never revisited.
4. **Choose the length (`--ms auto`).**
   - High-pass the recording at 1.05 times the highest fitted mode.
   - The burst runs until that band falls 40 dB under the note's 10 ms peak: at least 60 ms, at most 400 ms.
   - The fade is max(30 ms, length/3).
5. **Cut the burst.** The burst is the recording itself, not the recording minus the model:
   - The band the modes can take over (below 1.05 times the top mode) fades out on a raised cosine over the fade.
   - The band above it runs to the end, with a 10 ms taper.
   - The first 1 ms fades in.
   - It is written as `<id>-burst.wav` (float), with a `burst <file> <fade>` line.
6. **Anchor at the seam.**
   - At the middle of the fade, each isolated mode (no other mode within 1%) that still sustains there (under 15 dB of decay) gets its phase from the recording.
   - Its amplitude is also taken from the recording, carried back along its decay, but only when that is within 2x of the fitted amplitude.
   - Pairs are left as fitted. Without this step the crossfade dipped 6-9 dB.

For shaped (`--shaped`) records:

- The target of each take is scaled onto the model's level over the first 100 ms.
- The sign is decided once across all takes.
- Each take's burst is its recording with the last `--fade` ms (30) faded, giving a `burst <take> <file> <fade>` line.

What the fitter knows about this: its loss down-weights the first 60 ms
(`MB_BURST_MS`, `MB_BURST_FLOOR`), so the modes fit the sustain rather than
imitating the hammer.

In the runtime (`Kyklophoria/core/kyk_resonate.h`):

- Bursts are stored as 16-bit at 48 kHz in the world, each with its swing, scale, length and fade.
- **Timing.** The modes' strike bank stays silent through the burst's lead (its length minus its fade), then ramps in under 1 - fade. Since the runtime change mirrored in ModalBake b51d614 ("the modes timed from the attack that plays"), the lead and fade come from the take that actually plays. Before, a point's first take set them, and the VCSL grand's C5 went silent at 70-80 ms.
- **Scaling and crossfade.** A burst is scaled by the strike's swing over the burst's own swing. A layered point crossfades the two takes that bracket that swing.
- **Single-take worlds.** Velocity low-passes the burst, and the pitch is read at the played note.

Bursts make up 96-100% of a world's bytes (`proto/README.md`, Size).

**When to rerun `bursts.py`.** Rerun it after every fit or refit.
`refitn.py` rewrites a record with plain modes and drops its burst, sign and
trim lines. `export.py` reads bursts from the record, so a world exported
from a refitted set without `bursts.py` has no attack.

---

## 5. What is prototyped for the exciter, and not trained

- **The three-way bench (ModalBake `proto/`, `bench/threeway/`).** A desktop
  comparison of three exciters, measured against each note's recording:
  - (a) today's recorded burst;
  - (b) a synthesised exciter: a Hertzian felt hammer or a pluck, the fitted modes ramped in from t = 0, and 12 noise bands bounded by the recording's floor between partials;
  - (c) (b) plus a waveguide above the bank's top mode.

  What is fitted in (b) and (c):
  - The contact (hammer or pluck) is **not fitted**. It follows a keyboard law: 4 ms at A0 to 0.8 ms at C8, with Hertz scaling for velocity.
  - The noise bands (48 numbers per point) and the modes' ramp length **are fitted**, by least squares on the CPU.
  - The waveguide's parameters (f0, B, loss, dispersion, input level and tilt) come from the recording's own partials above the bank.

  It runs from ModalBake's root:

  ```sh
  make -C proto
  ~/fmexplorer/bin/python proto/threeway.py --refit      # CPU, 6 workers, ~10 min
  ~/fmexplorer/bin/python proto/velocity.py
  ~/fmexplorer/bin/python proto/report.py --md
  ```

  The WAVs land in `bench/threeway/` (gitignored). Result: (a) wins the
  first 150 ms, while (b) and (c) remove the cliff above the bank at the
  seam. None of it is in the firmware.
- **Coupled exciters (Kyklophoria `core/kyk_exciter.h`, `docs/exciters.md`).**
  - Bow, Hammer (Hunt-Crossley felt), Pluck, Reed and Lips, each coupled
    through the modes' state every sample (read the contact velocity, write
    the force back). `ContactNoise` ties the noise to the contact.
  - All of it is physics with hand-set parameters, checked by
    `tests/exciter_check` against synthetic resonators (Schelleng's bow
    band, the reed's pressure threshold, lip locking).
  - **None of it is fitted to recordings, and none is wired into the
    engine.** Stage 4 (an Exciter page on the module) is a proposal for the
    owner to decide.
  - The waveguide as a separate path was dropped.

So on the module today, "the exciter" means the recorded burst; the only
exciter parameters trained from data are the resonator's own
(modes, phases, pickup and swings) that the burst hands over to.

---

## 6. Grading, and refitting what fails

### 6.1 Checks on a fitted set

| tool | what it says | run |
|---|---|---|
| `gate.py` | a point fails at loss > 1.5 or decay_ratio > 3; it is "thin" (said, not failed) under 8 modes; a set fails with more than 10% bad; exits 1 on failure; shaped sets are not graded | `python3 tools/gate.py --brief out/fit/*/` |
| `fitcheck.py` | modes resynthesised with phases, loudness in windows from 0.1 s on against the recording, mean abs dB ("envelope error"). By default only the records a world plays modes from (the loudest dynamic of each note); `--list` writes `set ids` lines for refitn | `$PY tools/fitcheck.py out/fit/<set> --over 3 --list targets.txt` |
| `pitchcheck.py` | each record's source audio pitched against its label; past 0.5 st is mislabelled; exits 1 | `$PY tools/pitchcheck.py out/fit/<set>` |
| `fundcheck.py` | every note record has a mode within 4% of its note, at least -40 dB re the loudest | `python3 tools/fundcheck.py <set> ...` (names under out/fit) |
| `ringers.py` | modes ringing more than 3x longer than the recording does at their frequency; `--fix` caps quiet isolated ones | `$PY tools/ringers.py out/fit/<set>` |
| `specaudit.py` | a world's points through kykdesk against their recordings: stray spectra (>10 dB over) and static between partials | `$PY tools/specaudit.py out/worlds/<w>.kykm --fit out/fit/<set>` |
| `build/worldaudit` | per point of a world, through the runtime: tuning, harmonic-band shape, level, ring, each against what its neighbours predict (the refit list, worst first); `--walk` every semitone; `--render NOTE out.raw` | `build/worldaudit out/worlds/<w>.kykm --tsv a.tsv` |

Some reference numbers from the records:

- Over the card's plain sets, 1112 records had a median envelope error of 0.9 dB and a p90 of 4.0.
- After three refit campaigns, 85 records were still more than 3 dB off.
- `docs/rough-remainder-2026-09-27.md` goes through the worst twelve. Four
  are bad recordings, three are bad cuts, and four are genuine fit failures.

### 6.2 Refitting from neighbours (`refitn.py`)

A note fitted on its own can settle in a bad optimum. `refitn.py` fits a
flagged record again, starting from the modes of the nearest good records of
the same dynamic, transposed to this note. It keeps a candidate only when
its score, `loss × (1 + envelope_dB/3)`, beats the current record by 5%.
The old record goes to `<set>/prev/` (one version only), and `fits.tsv` is
rewritten after every record.

```sh
$PY -u tools/refitn.py out/fit/<set> --auto                    # decay ratio outside 0.4..2.5, loss > 1.3, or < 4 modes
$PY -u tools/refitn.py out/fit/<set> --only id,id --redo --neighbours 2 --tries 2
$PY tools/refitn.py out/fit/<set> --sync                        # repair fits.tsv after a crash
$PY tools/bursts.py out/fit/<set> --ms auto                     # ALWAYS afterwards
```

- It skips shaped records and records with a body.
- `--dry` fits and reports without writing anything.
- It falls back to onsets of -65 and -80 dBFS for quiet takes.

The three refit campaigns:

- `refit1.sh`: 511 records, `--auto`. It backs every set up to `out/refit1-backup/` first.
- `refit2.sh`: 161 records, from `out/overnight/refit2-targets.txt`.
- `refit3.sh`: 166 records, the fitcheck list, from `refit3-targets.txt`.

---

## 7. Export and the card

### 7.1 `export.py records`: a set into a world

```sh
$PY tools/export.py records out/fit/<set> out/worlds/<name>.kykm [--gate] [--clean[=DB]] [--voice[=S]] [--level-of=REF.kykm]
```

In order, the export:

1. Reads each record's modes, stage, takes, bursts (resampled to 48 kHz) and wash.
   - `--gate` leaves out the points gate.py would fail; the neighbouring point, transposed, then plays those notes.
   - A record with no modes is left out, with a warning.
2. **`layer()`**: records at the same note become one point with velocity layers (section 3.3a).
3. **`intune()`**: each point is pulled onto its nominal note.
   - The modes are scaled by the frequency of the most heard mode near the fundamental, weighted by energy 50-300 ms in.
   - The burst is resampled by the recording's own settled pitch.
   - Corrections over 120 cents are refused and reported as a likely octave error.
4. **`align()`**: every point is padded to the widest mode count (at most 48) with silent slots, and the loudest modes by ring energy are kept.
5. **`--voice`**: a note straying more than 3 dB from the median of its three neighbours on each side is scaled back by S of the excess, at most 9 dB.
6. **The body curve** (`body.py`): the instrument's 8-band radiation envelope. It makes the world version 7.
7. **Headroom**: the loudest note at full velocity must peak at 3.0 through the Python model, re-checked through `build/modaltest`. The world is rescaled if the runtime disagrees by more than 5%.
8. **`thin_points()`**: warns about points with under 8 live modes.

`--clean` plays each record as the runtime would, against its recording. A
mode more than 10 dB over the recording where it is heard gets its T60, then
its level, cut; past a 30 dB cut it is dropped. Clusters within 1% move
together. Without the flag the output is byte-identical to a plain export.
`--level-of=` matches a reference world's attack level, for A/Bs.

The flags the card uses:

- Layered and pizzicato worlds: `--gate --clean`.
- The refit re-exports in `export-r.sh`: `--voice --level-of=<card copy>`.

**Families.** One file holding several complete worlds; the module's
position axis chooses between them.

```sh
$PY tools/export.py family out/worlds/cello-pizz-vel.kykm C2=out/worlds/cello-pizzv-sulc.kykm G2=... D3=... A3=...
$PY tools/export.py family --by-pitch out/worlds/guitar-strings.kykm out/worlds/guitar-sul*.kykm
```

`--by-pitch` orders the members by their lowest note and names them after
it. The double bass is ordered explicitly instead, because its D string's
lowest fitted note is C3.

**The format.** From `export.py` and `core/kyk_resonate.h`.

- **Header.** `'KYKM' u16 version, u16 N, u16 P, u8 form (0 none, 1 bell, 2 gap), u8 kind (0 note, 1 index/body row, 2 family), f32 lo, f32 hi`.
  - Version 7 adds 8 f32 of body curve.
  - A family's header is followed by a table of (offset, size, name[16]) and then the members.
- **Per point.**
  - f32 param
  - 8 f32: h w K fc Q swing_soft swing_hard loudest
  - N modes of 5 bytes: u16 fifths of a cent from 20 Hz, u8 decay (-10 ln zeta), u8 level (quarter-dB under loudest; 255 is silence), u8 phase
  - u8 wash bands, then (f32 level, f32 t60) per band
  - u16 bursts, then per burst: f32 swing, f32 scale, u16 len, u16 fade, i16[len]
- **Version 8** (worldfill only): a burst count of 0xFFFF followed by a u16 index refers to another point's attack.
- **Accepted versions.** The runtime accepts 4-8 for a plain world and 6-8 for a family.

### 7.2 The card

`manifests/card.tsv` decides the card. Each row is `world`, then the fit sets
it came from (globs), then a note, then `gated` if it was exported with
`--gate`. `manifests/card-extra.tsv` holds the worlds that did not fit.

```sh
python3 tools/card.py                                   # -> out/card/kyklophoria/*.kykm + out/card/CONTENTS.txt
python3 tools/card.py --manifest manifests/card-extra.tsv --out out/card-extra
python3 tools/card.py --prefer out/worlds-r --out out/card-r          # an A/B card from another folder
```

`card.py` rebuilds the folder fresh each time. A world goes onto the card
only if:

- it exists;
- its name is 16 characters or fewer;
- it fits a 4 MB region;
- every one of its sets passes the gate, or, for a `gated` world, keeps at least 3 good points;
- the card has fewer than 24 worlds so far (`--max`).

Worlds that fail are printed with the reason; `--force` overrides the checks.

**Fill every semitone (format 8).** Between two recorded points the runtime
blends both at every strike, which overran the module on the pianos.
`worldfill` bakes each missing semitone once, using the runtime's own
`At()`, and verifies the result: each semitone of the new world must be
within 1.5 dB of the old one in 50 ms windows. How `out/card8` was assembled
is not scripted. The obvious loop is:

```sh
mkdir -p out/card8/kyklophoria
for f in out/card/kyklophoria/*.kykm; do build/worldfill "$f" out/card8/kyklophoria/$(basename "$f"); done
```

Copy the folder's `.kykm` files into `/kyklophoria/` on the SD card (the
module reads `0:/kyklophoria`). The module lists only the **first 32**
world files in directory order (`kMaxCardWorlds`), which is why the card is
capped at 24 worlds.

### 7.3 A complete open-licence example: the Iowa marimba (stage eight)

```sh
cd ModalBake; PY=~/fmexplorer/bin/python; P=samples/foss/iowa/Percussion
for dyn in pp mf ff; do
  $PY tools/splitnotes.py out/gen/marimba-m-$dyn $P/Marimba/*.yarn.$dyn.*.aif* --dynamic $dyn
  mkdir -p out/gen/marimba-mset && ln -sfn ../marimba-m-$dyn out/gen/marimba-mset/$dyn
done
nice -n 10 $PY -u tools/fitset.py marimba out/gen/marimba-mset out/fit/marimba-vel --layout folders --resume --steps 1200 --max-seconds 6.0
$PY tools/bursts.py out/fit/marimba-vel --ms auto
$PY tools/export.py records out/fit/marimba-vel out/worlds/marimba-vel.kykm --gate --clean
$PY tools/pitchcheck.py out/fit/marimba-vel
python3 tools/gate.py --brief out/fit/marimba-vel
$PY tools/notecheck.py out/worlds/marimba-vel.kykm 46 96 --vel 0.9
```

This produced 155 records in about an hour on the 4090, in one of two lanes.

---

## 8. Knobs, run times, and running campaigns safely

### 8.1 Environment variables (all in `tools/modalfit.py`)

They are read when `modalfit` is imported, so set them in the environment of
the `fitset`, `fitvel` or `refitn` process. No campaign script sets any of
them; the defaults are what shipped.

| variable | default | what it does |
|---|---|---|
| `MB_CLUSTER` | 1.0 | weight of the penalty on cancelling clusters (modes within 1%): stops antiphase pairs from building the attack |
| `MB_MONO` | 2.0 | shaped fit: penalty on a harder take swinging less than a softer one (takes in folder order, softest first) |
| `MB_COIL_PRIOR` | 1.0 | shaped fit with `--coil-from-set`: holds each note's coil fc and Q to the set's medians |
| `MB_DECAY_PRIOR` | 2.0 | weight holding log decay to the measured bin track |
| `MB_DECAY_BAND` | 0.405 (ln 1.5) | the free band around the measured decay before the prior bites |
| `MB_LEVEL` | 2.0 | shaped fit: loudness per take (log RMS ratio squared); off with `--normalised` |
| `MB_BURST_MS` | 60 | the window a burst will carry; frames inside it are weighted down |
| `MB_BURST_FLOOR` | 0.3 | the weight at the strike (1 means no down-weighting); proto/README suggests 1.0 for a refit without bursts |
| `MB_AUDIBLE_DB` | -60 | modes further than this under the loudest (judged at 10 ms) leave the record |
| `MB_PROMINENCE_DB` | 8 | a candidate peak must stand this far over its two-octave median |

### 8.2 Run times measured on the RTX 4090

Times come from `out/overnight/*.log` timestamps, with two lanes running at
once:

| work | time |
|---|---|
| fitset at `--steps 1200` (+600 polish) | about 25-60 s per record per lane: marimba-vel 155 records in ~62 min, vibraphone-vel 69 in ~35 min, bass-pizz3-sula 43 in ~42 min |
| stage six (Iowa piano 248 records, and every pizzicato string and the guitar at pp/mf/ff) | 00:04 to 06:23, about 6.3 h |
| the 2026-09-21 refit (574 records at 2000 steps) | 21:57 to 01:59, about 4 h for the fits |
| refitn per set | a few minutes to about 20 min |
| export | seconds to minutes per world; `--clean` and `--voice` render every point |
| proto three-way `--refit` | ~10 min on the CPU |

### 8.3 Campaign discipline

The campaign scripts are `tools/overnight.sh`, `refit.sh`, `foss.sh`
through `foss9.sh`, `refit1.sh` through `refit3.sh` and `export-r.sh`. They
share one pattern:

- `cd` to the repo root.
- Lanes run as background subshells, and the script `wait`s for them.
- Each lane writes a `.done` marker when it finishes and is skipped next time, so a script can simply be run again after an interruption.
- Each lane logs to `out/overnight/<script>-<lane>.log`, and the script itself to `<script>.log` (lines `== <step> HH:MM:SS`).

Rules of operation, from the scripts and the owner's practice:

- **Two fitting processes at most.** Three lanes and an interactive session
  pushed WSL past its 12 GB and it closed, killing everything, including the
  session's `/tmp` (where copied piano sources had lived). The scripts that
  follow another wait for it to finish:
  - `foss4.sh` and `foss5.sh` wait for `== done` in `foss3.log`;
  - `foss7.sh` and `refit1.sh` take a PID and poll it with `kill -0`.
- **Launch detached, and log.** For example:

  ```sh
  nohup tools/foss8.sh > out/overnight/foss8-driver.log 2>&1 &
  ```

  The `*-driver.log` files in `out/overnight/` come from launches like this.
  Python runs with `-u` so the logs update live; follow them with
  `tail -f out/overnight/<lane>.log`.
- **Never edit a campaign script while it runs.** `sh` reads a script as it
  goes, so an edit shifts what the running shell reads next. Copy it to a
  new name instead, as stages 5-9 did.
- **Stop a run by its PIDs.** Use `ps -ef | grep -E 'foss|fitset|refitn'`,
  then `kill <pid>` for the shell and its Python children. Do not use
  `pkill -f python`, which also kills other people's jobs and your session.
  `--resume` and the `.done` markers pick the run up again afterwards.
- **Keep sources under `out/gen/`, never in `/tmp`.** A crash or a WSL
  restart empties `/tmp`.
- **Keep new work beside the old.** New worlds get new names (`-vel`,
  `-pizz`, `worlds-r/`) beside the ones on the card, and ears decide which
  is better. Back up a set before refitting it, because `prev/` holds only
  the last version.

---

## 9. Verifying a result

1. **Numbers per set.** Run all four: `gate.py --brief`, `fitcheck.py` (median envelope error around 1 dB is typical), `pitchcheck.py` and `fundcheck.py`.
2. **Every semitone through the real engine.** `notecheck.py` strikes each
   semitone alone through `kykdesk --resonate` and reads the pitch back. It
   fails a note only when the recording reads correctly and the render does
   not.

   ```sh
   $PY tools/notecheck.py out/worlds/<w>.kykm 36 96 --vel 0.9
   ```

   Run it from the ModalBake root: it finds each world's recordings through
   `manifests/card*.tsv` and `out/fit/`. Use `--vel 0.3` as well on pickup
   worlds; the hard-hit "bark" can read as an octave.
3. **Listening renders.**
   - A keyboard through the runtime copy:

     ```sh
     build/modaltest out/worlds/<w>.kykm out/wav/<w>-keyboard.wav --velocity 0.9
     make fitted-renders
     ```

     `make fitted-renders` renders every fitted set and runs `earcheck.py`, which looks for rails, clicks, silence, level and NaN.
   - A shaped record soft to hard:

     ```sh
     $PY tools/playvel.py out/fit/ep-vel out/wav/ep-vel-bark.wav --note 60 --velocities 8
     ```
   - A/B against the recording: `<id>-target.wav` against `<id>-resynth.wav`.
   - One note through Kyklophoria's engine:

     ```sh
     printf '0.8 dur\n0.0 f0 261.6256\n0.05 strike 0.9\n' > /tmp/n.txt
     ../Kyklophoria/build/host/kykdesk --gen --seed 1 --resonate out/worlds/<w>.kykm --script /tmp/n.txt --out n.wav
     ```

   All of these WAVs contain recording. Keep them out of git.
4. **Consistency across the keyboard.** Run
   `build/worldaudit out/worlds/<w>.kykm --walk`. The points it scores worst
   against their neighbours are the list to hear first, and then to feed
   `refitn --only`.
5. **On the module.**
   - Copy the card folder to `/kyklophoria/`, rescan, and load a world into a slot from the Worlds tab.
   - Strike it from the MOTION row, or with a trigger. The jack map has changed since modal-mode.md was written (`overnight-2026-09-27.md` item 3), so take the current one from Kyklophoria's README.
   - The desktop and the module run the same core, so if the module sounds different from `kykdesk --resonate`, the fault is the shell's, not the fit's. See `Kyklophoria/docs/modal-mode.md`, "Playing it: the bench steps".

---

## 10. Pitfalls recorded in the docs and history

**Labels and pitch**

- **VCSL octave naming.** Most VCSL instruments call middle C "C3".
  Fourteen sets were fitted an octave low, and each record carried a real
  mode an octave under its note (the Knight upright's A4 had one at 216 Hz,
  -12 dB). Organise them with `--octave 1` and run `labelcheck.py` *before*
  fitting. The tubular glockenspiel's names are not parsed at all, so it is
  left out.
- **The EP set also names middle C "c3"**, so `--octave 1` is needed there too.
- **The Wurlitzer manifest had a C2 labelled C3**, and two bass fits had lost
  their fundamentals, so notes played an octave off. The fix: the fitter now
  seeds and keeps the labelled f0, and the checks `pitchcheck`, `fundcheck`
  and `notecheck` exist.
- **An old organise.py bug.** It read every natural a semitone sharp (Python's
  `'' in '#s'` is True) and silently dropped 36 of 88 piano files. Fixed; the
  lesson is to check the note count `organise.py` prints.
- **Unreliable pitch detection.** The detector is unreliable in the top
  piano octave, on pizzicato (sympathetic open strings) and on notes whose
  second harmonic dominates. `pitchcheck` is advisory there. A
  stretch-tuned piano top is not mislabelled.

**Splits and takes**

- **Labels by position were wrong.** An early splitter labelled segments by
  their order, so one missed onset put every later label a semitone out. It
  now labels each segment by its own pitch.
- **Late splits.** Cuts started where the envelope rose fastest, up to 1.3 s
  into a ring (33 of 55 bass notes). `splitnotes.py` now walks each cut back
  to the silence. Even so, two cello G-string notes still start over a
  second in (`rough-remainder-2026-09-27.md`), so check `splits.tsv`.
- **Mixed articulations.**
  - Arco and pizzicato were globbed together (22 of 63 violin points).
  - Iowa's percussion folders mix rolls, dead strokes, mallets and glissandi,
    and the marimba played a repeated note "like it was trained on a roll".
  - Keep one articulation per set (`--articulation`, one mallet per set).
- **A bowed note cannot be fitted as a strike.** Arco strings failed the
  gate on 162 of 210 points, so fit pizzicato.
- **Folder names.** Iowa's double bass is `Strings/doublebass`, not `bass`,
  so stage two silently fitted no bass.
- **Quiet takes.** Iowa's pp piano never reaches -40 dBFS and was skipped.
  Refit it with `--onset -65 --only ...`.
- **Very short files.** A file shorter than three STFT frames once killed a
  lane; it is now skipped with a message.

**Fitting**

- **Sign.** The STFT loss is blind to sign; `bursts.py` fixes it and records
  `signed 1`.
- **Antiphase clusters.** They built attacks that every byte and blend then
  undid. `MB_CLUSTER` fixed this in the 2026-09-21 refit (banjo's cluster
  excess went from a median of 8.4 dB to 0).
- **Swings and coils (fitvel).** Swings came out inverted on 39 of the EP's
  84 notes, and the coil was used as a free equaliser (Q 0.02-55). The fixes:
  `MB_MONO`, `--coil-from-set`, and the export's `monotonic()` reorder.
- **The EP bass local optimum** (`docs/ep-bass-2026-09-28.md`).
  - ep-vel's G2-C#4 fits sit almost dead centre on the pickup (h/w 0.014-0.03), which makes the 2nd harmonic and starves the 3rd and 4th by 20-40 dB, so the notes read an octave up.
  - A grid on the same pickup model shows h/w of about 0.1-0.35 reaches the recordings.
  - The proposed refit (`fitvel --only epv004..epv026` started off-centre) has not been run. `fitvel.py` has no option to seed h; `fit_shaped` always starts at h/w 0.3 and converges to the centre, so this needs a code change or a prior on h/w.
- **A single-fit bad optimum.** Piano A#5 ff rang 12x too short because of a
  sharp junk mode. `refitn.py`, seeded from neighbours, fixes cases like
  this. The fitter also lands differently from run to run, so use `--tries`.
- **An interrupted rewrite** left a record with no modes while `fits.tsv`
  still listed 44. Export now leaves such a point out with a warning; refit
  it.
- **The VCSL Steinway with the pedal down** holds sympathetic strings, so
  some notes' sustain sits on a detuned mode. `piano-vcsl-nosus` (the NoSus
  takes) avoids it.

**Export, runtime and card**

- **Stale runtime copy.** `runtime/kyk_resonate.h` goes stale whenever
  Kyklophoria moves on; run `make check-runtime` and rebuild
  `build/modaltest`, `worldfill` and `worldaudit`.
- **worldfill and pre-v7 worlds.** worldfill once stamped a version 6 world
  as 8 without the body curve, and the runtime walked off the end. Fixed:
  such a world gets a flat curve, and the runtime refuses a world whose
  bytes run out.
- **More than 32 world files** on the card and the module silently drops the
  newest. `card.py` builds a fresh folder of at most 24.
- **Licensing.** A `.kykm` is a recording. The card carries worlds from
  non-commercial and proprietary sets (wurli, ep, ep-vel, and the
  Philharmonia guitar, banjo and mandolin); they are fine on your own module
  but must not be redistributed.

---

## 11. Gaps, contradictions and doc drift found while writing this

- **`make check-runtime` fails today.** `runtime/kyk_resonate.h` (27 Sep) is
  older than `Kyklophoria/core/kyk_resonate.h` (28 Sep, with `TopScores`, the
  staged strike and the load governor). `build/modaltest` dates from 22 Sep,
  so `export.py`'s runtime headroom check runs an older runtime than either
  header.
- **No build rule for `worldfill` or `worldaudit`**, and no script for the
  filled cards (`out/card8`, `card-B8`, `card-r38`, `card-abc8`).
- **Unscripted steps.** The sample fetch scripts live only in the ignored
  `samples/foss/`. The organise steps for `piano-salamander`, `piano-vcsl` and
  `piano-vcsl-nosus`, the epigen command for `reed-vel`, and the download
  sources of Philharmonia, CNCD, the EP and Syntec are recorded nowhere in
  git.
- **Stale descriptions of the burst.**
  - README.md ("first 60 ms minus the model's"), the `export.py` header
    ("first 40 ms minus the model's") and the `BurstPlayer` comment in
    `kyk_resonate.h` all describe it as recording minus model. The code has
    stored the recording itself, faded, since the holistic pass.
  - `bursts.py`'s `--ms` help says "until the residual is 24 dB under"; the
    code uses 40 dB under the note's peak on the band above the top mode.
  - The `export.py` header gives version 6 and omits the u16 fade in each
    burst's header.
- **Stale scoring docstring.** `refitn.py`'s docstring gives the score as
  `loss × (1 + |ln decay_ratio|)`. The code has used
  `loss × (1 + envelope_dB/3)` since 17012ed.
- **Probable bug: refitted records are misaligned with their bursts.**
  - `refitn.py` fits against the *source* file, loaded untrimmed, and writes a record with no `trimmed` line. It leaves the `-target.wav` that an earlier `bursts.py` run had already trimmed.
  - The next `bursts.py` finds nothing left to trim, so it writes `trimmed 0`. Every refitted record checked says `trimmed 0`, where its backup in `out/refit1-backup/` says 259-550 samples.
  - So a refitted record's modes are timed from a start 5-12 ms earlier than the burst they sit under.
  - `anchor()` resets the phase and level of isolated modes that sustain to the seam, which hides most of it. Pairs and fast modes keep the offset.
  - To fix: have refitn re-trim (or rewrite `-target.wav` from the source, as fitset does). Then rerun bursts and re-export the refitted sets.
- **Gain columns disagree after `bursts.py`.** `trim()` and `anchor()`
  rewrite only the first of a record's 12 gains. `export.py` reads the first
  gain, while `refitn.py` and `fitcheck.py` read the last (the untouched
  fitted amplitude). This may be deliberate, since the last gain matches the
  untrimmed window they load, but nothing says so.
- **The stdlib-only rule is not what the code does.** Most non-fitter tools
  import numpy, scipy or soundfile (see 1.2).
- **Epi's licence is stated three ways.**
  - README and commit 1f98747: Epi is DatanoiseTV's, GPL-3.0.
  - ModalBake's SOURCES.md lists "`epi`, our own generator | ours".
  - Kyklophoria's `tests/data/SOURCES.md` calls it "ModalBake's own `epi` ... ours, AGPL-3.0".

  The worlds contain no Epi code, but the provenance line should be made
  consistent.
- **Memory region sizes disagree.** `Kyklophoria/docs/modal-mode.md` still
  speaks of 2 MB regions ("every world is under a 2 MB region"). The
  firmware has `kResRegionBytes = 4 MB` (and a mix of 4 MB and 2 MB regions
  per `overnight-2026-09-27.md`), and `card.py` checks 4 MB.
- **Uncommitted working-tree state.** Some `out/fit/*/*.mmr` files are
  tracked despite the `out/fit/*/*.mmr` ignore rule (older sets such as
  banjo, ep, guitar, piano and wurli). The working tree currently holds
  uncommitted modifications to many of them (out/fit/banjo and others),
  apparently from the refit campaigns.
