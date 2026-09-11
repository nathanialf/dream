# Provenance of Dream's unreferenced content

Where the seven classes of unreferenced or unreached data in `DREAM.sfc` came from, checked
by comparing bytes against the six other Rare SNES ROMs available for this task:
`Donkey Kong Country (USA) (Rev 2)`, `Donkey Kong Country 2 - Diddy's Kong Quest (USA) (En,Fr)
(Rev 1)`, `Donkey Kong Country 3 - Dixie Kong's Double Trouble! (USA) (En,Fr)`, `Killer Instinct
(USA) (Rev 1)`, `Battletoads in Battlemaniacs (USA)`, `Battletoads-Double Dragon (USA)`, called
DKC1/DKC2/DKC3/KI/BTBM/BTDD below. None of the six carries a copier header (`size % 1024`
is 0 for all six, checked before comparison, so no byte was stripped). *Ken Griffey Jr.'s
Winning Run* is not in the available library. Where NOTES.md's baseball-caption inference
touches it (region 3), that inference stays unverified: no bytes from that ROM were available
to check it against.

## Method

Two searches ran for every region against all six ROMs.

**Exact-run search.** Every 16-byte window of the Dream region is indexed, at every byte
offset, into a hash table (near-constant or short-period windows, such as runs of `00`, are
dropped from the index: they recur by chance in any binary blob and only cost time to keep).
Each ROM is then scanned at every byte offset; a hit against the index is extended forward and
backward, byte by byte, to the longest exact match. Matches are reported at >= 32 bytes, except
for the four small unused BRR samples (>= 16 bytes there, since two of the four are only 32-40
bytes long in total and would leave little room for a run above the default threshold).

**Tile-set search.** For regions with a known tile size (font glyphs, 16 bytes 2bpp; picture-
strip and alt-frame tiles, 32 bytes 4bpp), the Dream region's tiles are collected into a set and
every byte offset of each ROM is checked for a tile-sized exact match, so a tile counts as found
even where it is not tile-aligned on the other side. For the picture-strip and alt-frame
regions the tile size (32 bytes) equals the exact-run threshold already in use, so a full-tile
match is already reported as its own run by the exact-run search; running the tile-set search
again there would only repeat those same offsets, so results are given once, under the
exact-run search. The font's tile size (16 bytes) is smaller than 32, so its tile-set search is
run and reported separately.

**Noise filter.** Most raw hits, at every region and every threshold, turned out to be sparse,
near-empty 4bpp tile edges: a handful of non-zero bytes (a soft anti-aliased corner or edge)
followed by a long run of `00`. That shape recurs by chance in almost any SNES tile corpus,
Rare's or not, because empty tile margins are common everywhere. Every match below is checked
for the fraction of `0x00` bytes it contains; matches over roughly 70% zero are called out as
this kind of coincidence rather than evidence, with the actual fraction quoted. Matches over
audio or code data run 1-9% zero, which is the clean side of the same test.

## 1. Alternate-format sprite frames (1CC6AA-1F0000, 1F2E14-1FFEE5, tiles only)

The pure 4bpp tile bytes (header and `{x,y,attr}` records stripped per `docs/data_formats.md`
1c, using the same 0x1C-0x22 attribute-band rule `tools/assetcodec.py` uses) were pooled from
all 113 frames plus the truncated tail frame: 190,752 bytes, 5,961 tiles. Both the pooled tiles
and the raw region bytes (header+records+tiles together, to also test for the record format
itself turning up elsewhere) were searched.

| ROM | offset | length | what it is there |
|---|---|---:|---|
| DKC1 | 0x0FFF53 | 69 | sparse 4bpp sprite-tile edge, 96% zero |
| DKC2 | 0x00093A | 65 | sparse 4bpp sprite-tile edge, 95% zero |
| DKC3 | 0x3C4D09 | 55 | sparse 4bpp sprite-tile edge, 91% zero |
| KI | 0x01031C | 53 | sparse 4bpp sprite-tile edge, 81% zero |
| BTBM | 0x03BD4C | 35 | sparse 4bpp sprite-tile edge, 91% zero |
| BTDD | 0x045A73 | 53 | sparse 4bpp sprite-tile edge, 87% zero |

Every one of the raw-region hits (13 DKC1, 18 DKC2, 13 DKC3, 14 KI, 1 BTBM, 5 BTDD) and every
one of the pooled-tile hits (14 DKC1, 15 DKC2, 12 DKC3, 13 KI, 3 BTBM, 5 BTDD) sits at 70-96%
zero bytes: the anti-aliased-edge pattern above, not distinctive content. None of the hits fall
inside a frame's 8-byte header or its `{x,y,attr}` records either (a real x/y/attr triplet run
would not be 70%+ zero: `attr` alone is never `0x00`, per section 1c of
`docs/data_formats.md`), so the header/record shape itself was not found anywhere in the six
ROMs, tile-aligned or not.

**Conclusion: unknown.** No exact tile, no exact byte run, and no header/record shape from
this format turns up anywhere in DKC1/2/3, KI, BTBM or BTDD. Section 2 below (animation-script
and entity-init format comparison) also finds no structural match to DKC2's sprite-init or
animation containers. This format is not traceably DKC lineage from what is checkable here; it
remains this project's best label of "unknown, structurally similar to Dream's own live sprite
frames" from `docs/data_formats.md`.

### Animation script / entity init / alt-frame container vs. DKC2

`ref/DKC2-disassembly/notes.txt` documents DKC2's two relevant formats directly:

- DKC2 **animation scripts** are an opcode stream, commands `0x80`-`0x94`, each 1-10 bytes,
  e.g. `85 <frames> <sprite_gfx_id> <sprite_gfx_id>` (6 bytes, show two graphics for N frames)
  or `88 <x> <y>` (5 bytes, move to position). Dream's animation scripts
  (`docs/data_formats.md` 1, `0419B4-046588`) are fixed 8-byte `{callback, mode, duration,
  frame}` records in a flat table, indexed by a separate 174-word index. These are two
  different encodings: variable-length opcode stream vs. fixed-size record table.
- DKC2 **entity/sprite init** is likewise an opcode stream, commands `0x80`-`0x8E`, 0-0x1C
  bytes each (`81 <id>` sets the animation, `8A <13 words>` is a 0x1C-byte bulk-set, etc.).
  Dream's entity init table (`docs/data_formats.md` 1, `00B4B4-00B66A`) is a flat table of
  fixed 18-byte records read by `entity_init_from_table`. Same mismatch: opcode stream vs.
  fixed record table.
- The alt-format frame's own record, 3 bytes `{x, y, attr}`, has no DKC2 counterpart either:
  DKC2's nearest analogues (commands `0x85`-`0x8D`) are 6-10 bytes and carry a 16-bit
  `sprite_gfx_id` reference, not a raw per-tile x/y/attr triplet.

So although bank `$C0`'s library routines and the SPC700 driver are verbatim or near-verbatim
DKC2/DKC3 code (`docs/toolchain_evidence.md`, `docs/dkc_crossref.md`, confirmed again in
section 7 below), the mid-level content formats -- animation scripts, entity init records, and
both sprite-frame container shapes -- are Dream's own encoding, not a copy of DKC2's.

## 2. 2bpp ASCII font (014FE0-0155E0)

Exact-run search of the whole 1536-byte block: **zero matches** in all six ROMs at any length
>= 32 bytes. Glyph-by-glyph (16-byte tile) search against all six:

| ROM | glyph | offset | what it is there |
|---|---|---|---|
| DKC3 | `H` (glyph 40) | 0x3FEAF0 | identical 16-byte 2bpp glyph bitmap |
| DKC3 | `I` (glyph 41) | 0x3FEB10 | identical 16-byte 2bpp glyph bitmap |
| DKC3 | `L` (glyph 44) | 0x3FEB70 | identical 16-byte 2bpp glyph bitmap |

No other ROM matches any glyph. These three are not contiguous or in the same relative order
in DKC3 as they are in Dream's font (checked directly against the surrounding glyph indices,
which do not match), so this is not a shared font block: it is three isolated letters. `H`,
`I` and `L` are also the plainest possible shapes at 8x8 with a simple two-bar-and-crossbar
serif style: two vertical strokes and a full-width bar is close to the only reasonable way to
draw those three letters at this resolution, so two unrelated fonts landing on the same bitmap
for exactly these three is a believable coincidence, not evidence of one shared asset.

**Conclusion: Dream-original.** No shared font block, no run of shared glyphs, in any of the
six ROMs. The three single-glyph coincidences in DKC3 do not change that.

## 3. Picture-strip tilesets and maps (010000-014FE0)

Exact-run search (>= 32 bytes) and 32-byte tile-set search, combined (the tile size equals the
run threshold here, see Method):

| ROM | offset | length | what it is there |
|---|---|---:|---|
| DKC2 | 0x1B5894 | 41 | sparse 4bpp tile edge, 93% zero |
| DKC3 | 0x046057 | 37 | sparse 4bpp tile edge, 92% zero |
| DKC1 | 0x26E508, 0x28364E, 0x3383E4 | 32 each | sparse 4bpp tile edges, 91% zero each |

All five hits, and all 7/8/1/4/1/0 tile-set hits (DKC1/DKC2/DKC3/KI/BTBM/BTDD respectively) of
the 561 distinct tiles in the region, sit at 84-94% zero: the same anti-aliased-edge
coincidence as region 1. None is a meaningful match.

**Conclusion: unknown, not traceable to these six ROMs.** NOTES.md's reading of the three
strips as baseball captions (`STRIKE`, `TIME`/`OUT`, `HIT BY`/`PITCH`) stands on the rendered
text alone, and nothing here confirms or refutes the *Ken Griffey Jr.* connection: that ROM
was not available to check.

## 4. Four unused BRR samples (sample_00, _27, _28, _40)

| Dream sample | offset | length | match |
|---|---|---:|---|
| sample_00 | 023095-0230B5 | 32 | **exact, full length, byte-for-byte, in DKC2 at 0x2FB15A** |
| sample_27 | 02C5EA-02CCC0 | 1750 | no match anywhere, down to a single 9-byte BRR block |
| sample_28 | 02CCC0-02D345 | 1669 | no match anywhere, down to a single 9-byte BRR block |
| sample_40 | 033B06-033B2E | 40 | no match anywhere, down to a single 9-byte BRR block |

sample_00's 32 bytes are 9% zero (real ADPCM content, not silence), and the DKC2 match covers
every byte of the sample, not a fragment. sample_27/_28/_40 were also checked at single-BRR-block
granularity (9 bytes) rather than only the 16-byte default, in case only a fragment matched, and
still turned up nothing in any of the six ROMs.

**Conclusion: mixed.** sample_00 is DKC lineage, specifically shared with DKC2. sample_27,
sample_28 and sample_40 are unknown: not traceable to any of the six ROMs at any granularity
checked.

## 5. Used BRR samples and songs vs. the DKC titles

All 51 BRR sample records (023095-035163, 47 of them used by Dream's three real songs) and the
song/sfx data (song blocks 02119F-022E5C, sfx banks 022E5C-023081) were searched.

**Samples: extensive, exact sharing.** 26 of Dream's 51 BRR sample records match a DKC ROM at
100% of their own length, byte-for-byte (not a fragment, not a fuzzy resemblance: the entire
`{loop_offset, length, BRR blocks}` record), at 1-9% zero density (real audio, not padding
coincidence):

| Dream sample | length | matches (100% of length) |
|---|---:|---|
| sample_18 | 4558 | DKC2 @ 0x308A2D; DKC3 @ 0x2F8617 |
| sample_32 | 3514 | DKC2 @ 0x31946B |
| sample_33 | 3065 | DKC2 @ 0x30BD45 |
| sample_08 | 2480 | DKC2 |
| sample_13 | 2111 | DKC1 @ 0x07DA8A; DKC2 |
| sample_20 | 2092 | DKC2 |
| sample_25 | 1841 | DKC2; DKC3 |
| sample_02 | 1778 | DKC2; DKC3 |
| sample_38 | 1579 | DKC2 |
| sample_26 | 1516 | DKC2 |
| sample_23 | 1283 | DKC2; DKC3 |
| sample_06 | 1274 | DKC1; DKC2 |
| sample_12 | 1274 | DKC2 |
| sample_34 | 1067 | DKC2 |
| sample_41 | 751 | DKC2; DKC3 |
| sample_07 | 734 | DKC2 |
| sample_17 | 725 | DKC2 |
| sample_16 | 661 | DKC2 |
| sample_10 | 644 | DKC2 |
| sample_22 | 527 | DKC2; DKC3 |
| sample_09 | 526 | DKC1; DKC2; DKC3 |
| sample_31 | 500 | DKC2; DKC3 |
| sample_19 | 410 | DKC2; DKC3 |
| sample_21 | 212 | DKC1; DKC2 |
| sample_15 | 212 | DKC2 |
| sample_00 | 32 | DKC2 (see region 4) |

The full row list is reproducible from `data/brr/sample_NN.bin` against DKC1/2/3; the table
above gives every sample that hit 100% of its own length in at least one title. DKC2 alone
accounts for the great majority; DKC3 repeats a large subset of the same samples at the same
lengths (DKC2 and DKC3 sharing a sample library with each other is expected; what matters here
is that Dream's copy is byte-identical to both). DKC1 shares a smaller subset (sample_06, _09,
_13, _21), consistent with `docs/toolchain_evidence.md`'s "DKC1 an earlier revision"
finding for the SPC driver: an earlier, smaller sample library. KI, BTBM and BTDD share none of
Dream's BRR samples.

**Songs and sfx banks: no match.** The song blocks (02119F-022E5C: the actual note/sequence
data for Dream's three real songs) and the sfx bank data (022E5C-023081) returned **zero**
matches against any of the six ROMs at any length >= 32 bytes.

**Conclusion: split, and informative.** Roughly half of Dream's BRR sample bank is Rare's
shared DKC-era instrument library, reused wholesale and byte-for-byte, strongest against DKC2
and DKC3 and weaker against DKC1. That is DKC lineage, established by bytes, not resemblance.
The songs built from those samples, and the sound-effect sequence data, are not shared with any
DKC title: Dream's compositions and sfx list are its own, layered on top of a borrowed sample
library.

## 6. Unknown slivers

| Dream region | length | result |
|---|---:|---|
| 05F0E1-060000 (`unknown_counter_table`) | 3871 | 39 raw hits (37 DKC1, 1 DKC2, 1 DKC3); every one is a small sequential-`u16` counter fragment (`00 01 00 02 00 03...`), the same shape Dream's own region is built from -- coincidental, not a shared table (see below) |
| 07FFF8-080000 | 8 | no match |
| 08FFEC-090000 | 20 | no match |
| 0AE384-0AE38E | 10 | no match |
| 0AFFEC-0B0000 | 20 | no match |

The `unknown_counter_table` hits were checked byte-for-byte: e.g. the Dream bytes at file
offset 0x05F4DB (region-relative 1018) are `00 C2 00 C3 00 C4 00 C5 00 C6 00 C7 00 C8` followed
by zeros, and that same seven-value run (0x00C2 through 0x00C8, a fragment any table counting
through that range would contain) is what DKC1 has at its matching offset too: not a copy of
Dream's table, just the same short numeric run that any two unrelated counter/index tables wide
enough to pass through 0xC2-0xC8 will coincidentally share. Any two small sequential-index
tables padded with zero look alike; this is the same class of coincidence flagged for this
region in `config/assets.txt` already ("not enough to confirm identity"), now confirmed absent
even against these six specific ROMs.

**Conclusion: unknown.** No sliver in this list matches any of the six ROMs in any way that
survives the zero-density/coincidence check.

## 7. SPC700 driver stale/unreachable handlers, and the driver image generally

The full SPC700 image Dream uploads (IPL loader + driver + the start of the sample pointer
table, file 020000-0210B9, 4281 bytes) was searched as one block, independently of
`docs/toolchain_evidence.md`'s and `docs/dkc_crossref.md`'s existing address-level analysis, to
confirm those findings from raw bytes:

| ROM | bytes matched (of 4281) | share | longest run |
|---|---:|---:|---|
| DKC3 | 3417 | 79.8% | 423 bytes @ file 0x0204A8 == DKC3 0x2D04A8 |
| DKC2 | 3281 | 76.6% | 423 bytes @ file 0x0204A8 == DKC2 0x2E04A8 |
| KI | 915 | 21.4% | 203 bytes @ file 0x020CED == KI 0x01FEEF |
| DKC1 | 420 | 9.8% | 122 bytes @ file 0x020D3E == DKC1 0x0AB072 |
| BTDD | 245 | 5.7% | 122 bytes @ file 0x020D3E == BTDD 0x0A2BFB |
| BTBM | 122 | 2.8% | 122 bytes @ file 0x020D3E == BTBM 0x0E8C94 |

This reconfirms `docs/toolchain_evidence.md`'s "96.3% byte-identical to DKC2/3" claim from
independent bytes: the true shared fraction is higher than the 76-80% above states, because a
16-byte-window exact-run search fragments at every one of the many `+0x33`-shifted pointer
bytes documented in `docs/dkc_crossref.md` section 2.2 (each shift breaks the run, even though
the code on both sides either side of it is identical); 76-80% is therefore a lower bound
consistent with, not contradicting, the 96.3% figure obtained there by direct address-aligned
comparison. DKC1's much lower share matches `docs/toolchain_evidence.md`'s "DKC1 an earlier
revision," and KI's matches "Killer Instinct carries the same code at a different layout."

The 122-byte fragment at file 0x020D3E (`docs/dkc_crossref.md` section 4.2 identifies this
address range as the `voice_bits` table, SPC `$0FC8`, DKC2's `DATA_0F95` shifted -0x33) is the
one run shared identically across **all six** ROMs, not just the DKC trilogy: it is present,
byte-for-byte, in DKC1, DKC2 (inside a larger 204-byte run), DKC3 (inside a larger run), KI (at
a shifted offset, inside a 203-byte run), BTBM and BTDD. `docs/dkc_crossref.md` had confirmed
this table only against DKC2; this extends it to Battletoads and Killer Instinct as well, from
bytes.

Checking the five specific stale/orphan opcode handlers `docs/dkc_crossref.md` section 2.2
already names (`orphan_slide_up2` $0F77, `orphan_slide_down2` $0F81, `orphan_gate_on` $0FAF,
`orphan_gate_off` $0FB9, `orphan_volume_preset2` $0C18) against this independent scan's match
spans: `orphan_slide_up2`, `orphan_slide_down2` and `orphan_volume_preset2` fall inside matched
runs against both DKC2 and DKC3 (and, for the first two, a shorter run against KI);
`orphan_gate_on` matches DKC2 and DKC3 only; `orphan_gate_off`'s bytes were not picked up as
part of any matched run here. None of the five fall inside a DKC1 run in this scan. This mostly
corroborates `docs/dkc_crossref.md`'s address-level finding that these are "stale in both"
Dream and DKC2; `orphan_gate_off` is the one handler this byte scan does not independently
confirm.

**Conclusion: DKC/Rare lineage, confirmed.** The driver and IPL loader are the shared Rare
SPC700 sound engine used across the whole catalog checked here, strongest with DKC2 and DKC3,
present in weaker form in DKC1 and KI, and with at least one shared table reaching all the way
to both Battletoads titles. This matches and, for the `voice_bits` table and the Battletoads
titles, extends the existing findings in `docs/toolchain_evidence.md` and
`docs/dkc_crossref.md`.

## Summary

| region | conclusion |
|---|---|
| 1. Alt-format sprite frames (tiles) | unknown; no format match to DKC2's sprite-init/animation containers either |
| 2. 2bpp ASCII font | Dream-original |
| 3. Picture-strip tilesets and maps | unknown, not traceable to these six ROMs |
| 4. Four unused BRR samples | split: sample_00 is DKC2 lineage; sample_27/_28/_40 are unknown |
| 5. Used BRR samples and songs | split: ~half the sample bank is shared DKC-era Rare library (byte-identical); songs and sfx sequences are Dream-original |
| 6. Unknown slivers | unknown; apparent matches are coincidental counter-table noise |
| 7. SPC700 driver | confirmed DKC/Rare lineage, shared across the whole six-ROM set to varying degrees |

Scripts used for this pass are not part of the repository (per `docs/LEGAL.md`, no ROM bytes or
extraction tooling tied to a specific ROM copy is committed); they lived under this session's
scratchpad and are not needed to reproduce the byte-level claims above, which quote offsets and
lengths directly.

## Maintainer identification (2026-09-11)

With the alternate frames assembled through their decoded headers, the maintainer identified them
visually as baseball player sprites, consistent with Ken Griffey Jr.'s Winning Run (Rare, 1996) and
with the STRIKE 1 / TIME OUT / HIT BY PITCH caption strips in bank $C1. The Winning Run ROM is not in
the library, so this stays a visual identification, not a byte match.
