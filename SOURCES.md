# What this repository may carry, and what it may not

ModalBake fits models to recordings. It therefore handles audio it does not
own, and the rule is simple:

**No recording, and nothing derived from a recording, is ever committed.**

`samples/` is where recordings live and it is ignored. So is `out/gen/`,
which is where the tools copy and split them. That much was always true.
Two kinds of derived audio were tracked anyway until 2026-09-22, because the
ignore rules named patterns rather than a principle:

- `out/fit/*-AB.wav` — 27 files. An A/B render is the target followed by the
  resynthesis, so the **first half of each one is the original recording**,
  in full, up to six seconds of it.
- `out/wav/*.wav` — 55 files. Renders of the fitted worlds through the
  runtime: keyboards, sweeps, glides, walks. A fitted world carries bursts,
  and a burst is a window of the recording, so these carry it too.

- `out/worlds/*.kykm` — 17 worlds. A fitted world carries a burst per point,
  so these ARE recordings, however much they look like numbers. Among them
  `wurli`, `ep`, `ep-vel`, `perc` and the families built from them
  (`electric`, `pianos`, `strings`) — the whole restricted set. Missed on the
  first pass, which swept `.wav` and forgot that the rule written directly
  below applies to `.kykm` first of all. Found by re-reading it.

104 MB of it, across the whole history. This repository has no remote and
never had one, so none of it was published — but a push would have published
all of it, and one of those worlds came from a Wurlitzer library that is
licensed for non-commercial use with no redistribution at all.

Both patterns are now ignored, and the history was rewritten to remove them.

## What may be committed

Numbers and text: the tools, the manifests under `manifests/`, the fit
reports, the `.tsv` and `.txt` under `out/`. A `.kykm` is a judgement call
and the answer is **no by default** — it carries burst audio, so it is a
recording in the same way an A/B render is. The ones that ship in
Kyklophoria's `tests/data/` are there because their sources allow it, and
that directory has its own `SOURCES.md` saying which.

## The sources, and what each allows

| source | terms | may we ship what we fit from it? |
|---|---|---|
| University of Iowa Musical Instrument Samples | "may be downloaded and used for any projects, without restrictions" | yes |
| Versilian VCSL / VSCO 2 Community Edition | CC0 | yes |
| Salamander Grand | CC-BY | yes, with attribution |
| Philharmonia Orchestra samples | CC BY-NC-SA | no — non-commercial, share-alike |
| the CNCD Wurlitzer set | non-commercial, no redistribution | no |
| commercial EP libraries | proprietary | no |
| `epi`, our own generator | ours | yes — no recording in it at all |

Iowa's wording, from `https://theremin.music.uiowa.edu/MIS.html` as checked
on 2026-09-22:

> these recordings have been freely available on this website and may be
> downloaded and used for any projects, without restrictions.

It is a grant in plain words rather than a named licence, so it is quoted
whole rather than reduced to an identifier.
