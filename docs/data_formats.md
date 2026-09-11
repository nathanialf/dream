# Dream: Land of Giants (DREAM.sfc) data-format map

Derived statically with python3 from the ROM bytes and `out/dream.asm` (DMA register writes, `lda [dp],y` walkers, the bank-`$C4` frame/animation tables and the bank-`$C2` SPC tables). File offset `f` = CPU `$C0:0000 + f` (HiROM). Every byte of the 2 MiB image is covered; regions are ordered by file offset. Confidence: high = the live code reads the region with a known format, or an exact structural parse/duplicate match; medium = classifier + eyeballed render; low = classifier only.

Conventions: `game_mode` 0-3 are the four scenes dispatched through `game_mode_table` (`sub_C08292`, `sub_C084D7`, `sub_C08798`, `sub_C088AB`). VRAM addresses are word addresses as written to `VMADDL`. `sub_C0A46A` = DMA (A = source lo16, X = bank, Y = byte count) to the VRAM address set just before; `sub_C0A445` = zero-fill `$800` bytes of VRAM; `sub_C0A483` = CGRAM DMA from bank `$C4` (X*8 bytes, Y = CGADD); `sub_C0A538`/`sub_C0AE7E` = sprite OAM builder + VRAM DMA queue.

## 1. Region table

| start-end (file) | size | format | conf. | evidence | referenced by |
|---|---:|---|---|---|---|
| 000000-003000 | 12288 | stale build: 65816 code (older assembly of the same program, modules reordered) | high | reset-style prologue at 0 (18 FB 78 C2 30); NOTES.md toolchain section; not traced | none (dead) |
| 003000-004B00 | 6912 | stale build: older copy of the animation-script table (8-byte records, word[1] steps by 4) | medium | records {02 00, D4 04, 00 00, 00 00}...; NOTES.md toolchain section | none |
| 004B00-007AC8 | 12232 | stale build: 4bpp tiles | medium | row coherence 0.21-0.26, decodes to shaded shapes (render below); no copy elsewhere | none |
| 007AC8-007EC0 | 1016 | stale build: palette block (508 x 15-bit BGR words, bit 15 clear in all) | medium | bit15-clear run 508 words at 7AC8; colours e.g. (30,3,0),(30,3,16) | none |
| 007EC0-008000 | 320 | stale build: tail (mostly FF, a few words) | low | entropy 2.0, 242/320 bytes = FF | none |
| 008000-00C00D | 16397 | 65816 main program; embedded data tables: 84A2 HDMA tables, 84B5 wave (16 s16), 8791/8792 CGRAM bytes, 8793 HDMA (CGADSUB), 88A6 HDMA (TM), 8A5A HDMA tables, A443 zero word (VRAM fill source), A6D3/A6D7 OAM size-bit masks, B208-B24C VRAM/CGRAM streaming descriptors (8 bytes: vram, src, bank/flag, size), B24C-B26C per-mode descriptor index, B26C-B2F6 entity spawn x/y list, B2FA-B4A4 per-state Y table, B4A4-B4B4 per-mode entity offsets, B4B4-B66A entity init records (18 bytes), B66A/B6DC/B7AE two-level state tables | high | out/dream.asm, out/handler_tables.md | vectors |
| 00C00D-00C054 | 71 | HDMA table, indirect mode ($48) for INIDISP: 3-byte entries {lines, ptr16 -> $7F:0Fxx brightness bytes} | high | A1TL1=$C00D at $BCF3, DMAP1=$48, BBAD=$00, A1B=$80, DASB=$7F; entries 58 89 0F / 37 8D 0F ... | $BCED-$BCF6 (title-screen init) |
| 00C054-00FFD4 | 16256 | filler: repeated 06 37 00 37 (as tilemap words: tile $306/$300, palette 5, priority 1, no flip); overwrites the internal header | high | 16256-byte exact repeat; nothing reads it; no other occurrence in the ROM | none |
| 00FFD4-00FFE4 | 16 | internal header remnants (map mode/ROM type/size bytes partly overwritten) | medium | 00 06 03 20 00 07 03 20 00 85 12 20 ... | hardware |
| 00FFE4-010000 | 28 | CPU vectors; "RARE" ASCII in unused slots | high | NOTES.md mapping section | hardware |
| 010000-010100 | 256 | 32x4 tilemap (words $3C44.. increasing = picture strip, palette 7 priority 1) | high | grid print: tile idx 44-89 laid out over 4 rows; hi bits F | none (unreferenced) |
| 010100-012800 | 9984 | 4bpp tiles for the strip above (unreferenced picture/logo) | medium | row coherence 0.15-0.24; internal duplicates at +9C0/+9E0 (repeated frames) | none |
| 012800-012900 | 256 | 32x4 tilemap (tile idx 44-95) | high | grid analysis as above | none |
| 012900-013300 | 2560 | 4bpp tiles (unreferenced) | medium | row coherence ~0.2 | none |
| 013300-013400 | 256 | 32x4 tilemap (tile idx 44-A0) | high | grid analysis as above | none |
| 013400-014FE0 | 7136 | 4bpp tiles (unreferenced); 14D00-14FE0 contains further tilemap-like words | medium | row coherence ~0.2; fine classifier | none |
| 014FE0-0155E0 | 1536 | 2bpp font, 96 glyphs = ASCII $20-$7F (plane 1 empty, so effectively 1bpp); the only font in the ROM | high | glyphs decode as A-Z/a-z/0-9/punctuation (render below); ends at the 0x55 fill | none (unreferenced) |
| 0155E0-018000 | 10784 | fill 0x55 | high | all bytes 0x55 | none |
| 018000-01841A | 1050 | 65816 sound interface (spc_init, spc_command, uploads) | high | out/dream.asm | jsl from reset/play_sound_effect |
| 01841A-01EAC0 | 26278 | stale duplicate of 08041A-086AC0 (game_mode 3 BG1 4bpp tileset, minus its first 0x41A bytes) | high | 26278-byte exact match at delta +0x68000 | none |
| 01EAC0-01EFC0 | 1280 | stale duplicate of 076E02-077302 (last 40 tiles of bg1_tiles_mode2) | high | 1280-byte exact match at delta +0x58342 | none |
| 01EFC0-020000 | 4160 | fill 0x55 | high | all bytes 0x55 | none |
| 020000-020088 | 136 | SPC700 IPL loader image (uploaded to SPC $04D8) | high | spc/spc_map.txt; spc_ipl_upload_loader | $C1807A |
| 020088-020DBA | 3378 | SPC700 driver image (0x699 words -> SPC $0560; DKC2/3 driver per NOTES.md) | high | spc/driver.asm | spc_upload_driver |
| 020DBA-020E52 | 152 | BRR sample pointer table: 51 x 24-bit pointers (index*3) to sample records | high | entries 0-50 point at 023095..034FDC in order; entry 51 has bank $E9 | sub_C1815F (data_C20DB9) |
| 020E52-0210B9 | 615 | unused tail of the sample pointer table area (increasing 16-bit values, stale pointer list?) | low | words $9CE8,$1CE9,$1CEA,... entropy 5.6 | none |
| 0210B9-0210EE | 53 | song table: 16 x 24-bit pointers, pairs {song block, sample list} per song (index*6), + 5 zero bytes | high | sub_C18119 (6n) and sub_C18392 (6n+3); songs 0-2 real, 3-7 empty | spc_command |
| 0210EE-021109 | 27 | sfx bank 2 block pointer per song: 8 x 24-bit (index*3) + 3 zero bytes | high | sub_C1813D | spc_command |
| 021109-02119F | 150 | sample lists: $FFFF-terminated u16 sample numbers (init list at 21109 is empty; song0 2110B: 32 samples; song1 2114D: 12; song2 21167: 22; songs 3-7 empty) | high | parsed via song table | sub_C1815F |
| 02119F-022E5C | 7357 | song blocks {dest $1300, word count, data}: song0 2119F (0x84D words), song1 2223C (0x4F8), song2 22C2F (0x10B), songs 3-7 empty 4-byte blocks at 22E48-22E5C. Data = channel pointer header + sequence streams (spc_map.txt format) | high | spc_upload_block reads dest/count; byte histogram 31% >= $80 (notes) | sub_C18119 |
| 022E5C-023070 | 532 | sfx bank 1 block -> SPC $2410: count 21 + 21 pointers + 21 sfx sequences | high | block header 10 24 0A 01; pointers 2617,2618,... all inside the block | spc_init |
| 023070-023081 | 17 | sfx bank 2 blocks -> SPC $2E94 (23070: 5 words for songs 0,2-7; 23079: 2 words for song 1); overlaps the last 4 bytes of bank 1 | high | data_C210EE table | sub_C1813D |
| 023081-023095 | 20 | 20 bytes between sfx bank 2 and the first sample record | low |  | none |
| 023095-035163 | 73934 | 51 BRR sample records {loop_offset u16, length u16, BRR blocks}; records abut exactly; 47 used by the 3 songs (0,27,28,40 unused). Lengths are byte counts, some = 9k+1/9k+2 (pad bytes); every 9-aligned sample ends with a block whose end flag is set | high | parsed from the pointer table; shifts <= 12 in all headers; entropy 6.9-7.3 | sub_C1815F / loc_C182AF |
| 035163-037E82 | 11551 | tile-like data, unreferenced (bank C3 tail); probably more stale tileset copies | low | row coherence ~0.25; no exact duplicate found | none |
| 037E82-03CFD6 | 20820 | stale duplicate of 050EC0-056014 (itself a duplicate of bank C8 tiles: 087B4E.. via delta +0x4FAFE) | high | 20818-byte exact match at delta +0x1903E | none |
| 03CFD6-040000 | 12330 | tile-like data, unreferenced | low | row coherence ~0.25 | none |
| 040000-041858 | 6232 | sprite frame table: 1558 x 4 bytes {ptr16, bank, y-bias} (frame id = byte index, even) | high | sub_C0A538 reads data_C40000/data_C40002; all 1556 non-null entries parse as frames | sub_C0A538 |
| 041858-0419B4 | 348 | animation script index: 174 words (anim id -> script offset in bank C4) | high | handler_tables.md | anim_update |
| 0419B4-046588 | 19412 | animation scripts: 8-byte records {callback, mode, duration, frame}, 96 scripts | high | handler_tables.md | anim_update |
| 046588-046788 | 512 | wave table: 256 x s16 sine (0,-6,-13,...,-255 at 64, 0 at 128, 255 at 192) | high | sub_C08F47 reads it with sign-extending ror (HDMA wave for BG2/BG3) | $8FA2,$8FDB,$9078 |
| 046788-046988 | 512 | four 64-entry s16 curves (256->0, 79->-1, 190->-1, -32..-64) used by the camera/parallax code | high | adc data_C46788,X etc. at $8C16/$8D4F/$8D7F/$8D86 | $8C16,$8D4F,$8D7F,$8D86 |
| 046988-046B88 | 512 | two 128-entry s16 tables (+-64 ramp; -6..5 jitter) for scroll offsets | high | $8DF0 (data_C46988), $81A6 (data_C46A88, sign-extended) | $8DF0,$81A6 |
| 046B88-046C48 | 192 | colour ramp: 96 x 15-bit BGR ($0000,$0421,$0842,...) for palette cycling of CGRAM entry $12 | high | sub_C08D6B -> $0C0A -> CGDATA at $8EEA; values are grey ramp | $8D6B |
| 046C48-047463 | 2075 | palettes (15-bit BGR): 46C48 128 colours -> CGRAM $80 (sprites, all modes); 46D48/46D68/46D88 16-colour sets -> CGRAM $F0/$E0/$B0; 46DA8 128 colours -> CGRAM $00 (mode 0); 46EA8 -> CGRAM $00 (mode 1); 46FA8-46FE3 HDMA table for CGADD/CGDATA (mode 1, 59 bytes); 46FE3 -> CGRAM $00 (mode 2); 470E3 16 colours (CGRAM $70 stream); 47103-47343 9 x 32-colour rows for CGRAM $40 palette animation; 47343 -> CGRAM $00 (mode 3); 47443 -> CGRAM $A0 (mode 3) | high | sub_C0A483 DMA (DMAP $2200, bank $C4) at $8383-$83AA, $8737-$8776, $8861-$888E, $8989-$89B6; HDMA ch1 at $8669; data_C0B20E records; $91DD | listed sites |
| 047463-04DE22 | 27071 | stale duplicate of 0775C3-07DF82 (bank C7 middle: mixed 4bpp tiles + tilemap/metatile words) | high | 27107-byte exact match at delta +0x30160 | none |
| 04DE22-04FD62 | 8000 | stale duplicate of 09DCE0-09FC20 (game_mode 0 metatiles 0-249) | high | 8000-byte exact match at delta +0x4FEBE | none |
| 04FD62-050000 | 670 | unreferenced tail (tile-like) | low |  | none |
| 050000-050200 | 512 | 2bpp tiles, 32 tiles -> VRAM $1E00 (game_mode 1 BG3) | high | sub_C0A46A at $8758 ($C5:0000, $200 bytes); BG3 is 2bpp in BGMODE 1 | $8758-$8767 |
| 050200-0502C0 | 192 | two 48-entry u16 tables (data_C50200/data_C50260: $7782,$7781,... / $797E,...), paired byte values | high | sub_C0942D indexes both with the same X | $942D,$9433 |
| 0502C0-050EC0 | 3072 | 4bpp OBJ tiles, 96 tiles -> VRAM $1600 (mode 0) and VRAM $0000 (sub_C09234) | high | sub_C0A46A at $8305 and $9234 ($C5:02C0, $C00 bytes) | $8305,$9234 |
| 050EC0-05BABF | 44031 | stale duplicates of bank C8/C9 tilesets: 50EC0-52820 = 087980-0892E0, 5108E-56014 also = C3 tail, 56000-5BAC0 = 090000-095AC0 (whole game_mode 0 BG1 tileset) | high | exact matches at deltas +0x36AC0, +0x3A000, -0x1903E | none |
| 05BABF-05F0E1 | 13858 | stale duplicate of 08C97F-08FFA1 (game_mode 1 BG2 tileset) | high | 13853-byte match at delta +0x30EC0 | none |
| 05F0E1-060000 | 3871 | word table 0000 0001 ... 0009 then mostly zeros | low | entropy 3.4, 66% zero bytes | none |
| 060000-06002B | 43 | 43-byte header with ASCII names "FRAME 1", "FRAME 3" (graphics-tool export directory left in the data) | high | strings at 60013/60024; only ASCII text in the ROM besides "RARE" | none |
| 06002B-069CEB | 40128 | 8bpp tiles, 627 tiles -> VRAM $0600 (title screen, BGMODE 3 BG1; tilemaps add $30 = tile base) | high | VMDATAL loop at $BC47 ($9CC0 bytes); BGMODE 3 at $BBEC; render below | $BC3B-$BC52 |
| 069CEB-06A02B | 832 | tilemap words (+$30 added) -> VRAM $6500, 416 words | high | loop at $BC72 | $BC69-$BC81 |
| 06A02B-06A1EB | 448 | tilemap words -> VRAM $6960, 224 words | high | loop at $BC8F | $BC86-$BC9D |
| 06A1EB-06A2EB | 256 | tilemap words -> VRAM $6DA0, 128 words | high | loop at $BCC7 | $BCBE-$BCD5 |
| 06A2EB-06A36B | 128 | tilemap words -> VRAM $61C0, 64 words | high | loop at $BCAB | $BCA2-$BCB9 |
| 06A36B-06A56B | 512 | 256-colour palette -> CGRAM $00-$FF (title screen), also copied to $7F0F91 | high | loop at $BC2A; colour 0 = $0000, 1 = white | $BC24-$BC37 |
| 06A56B-06A661 | 246 | tilemap-like words ($5240,$5340,... increasing), unreferenced | medium | word analysis | none |
| 06A661-06FC26 | 21957 | stale duplicate of 0A7581-0ACB46 (bank CA sprite frames) | high | 21957-byte exact match at delta +0x3CF20 | none |
| 06FC26-06FFFC | 982 | stale duplicate of 12F8AA-12FC80 inside sprite_frames_ce (not tile/frame aligned) | medium | 982-byte exact match at delta +0xBFC84 | none |
| 06FFFC-070000 | 4 | unreferenced tail, no duplicate | low | diverges from the sprite_frames_ce match just above | none |
| 070000-070340 | 832 | sprite frames, live format: 1 frames {8-byte header: n1, n2, off2, n3, off3, nt1, vo2, nt2; n1+n2+n3 2-byte OAM records (x,y); 4bpp tiles = (nt1+nt2)*32 bytes}; 2-40 byte trailers between frames | high | frame table 040000 entries point here; header/record/tile sizes chain contiguously | sub_C0A538 / sub_C0AE7E DMA |
| 070340-070342 | 2 | unlabelled gap | low | entropy 1.00 | none |
| 070342-077302 | 28608 | 4bpp BG1 tiles, 894 tiles -> VRAM $2000 (game_mode 2) | high | sub_C0A46A at $8813 ($C7:0342, $6FC0) | $8813-$8822 |
| 077302-07DF82 | 27776 | unreferenced: mixed 4bpp tiles and tilemap/metatile word chunks (79402-7A302, 7A602-7A802, 7B602-7B802, 7D102-7D402, 7D702-7D902); duplicated at 047463 (bank C4) | medium | fine classifier; duplicate at delta -0x30160 | none |
| 07DF82-07F962 | 6624 | 4bpp BG2 tiles, 207 tiles -> VRAM $6000 (game_mode 2) | high | sub_C0A46A at $883D ($C7:DF82, $19E0) | $883D-$884C |
| 07F962-07FFF8 | 1686 | sprite frames, live format: 1 frames {8-byte header: n1, n2, off2, n3, off3, nt1, vo2, nt2; n1+n2+n3 2-byte OAM records (x,y); 4bpp tiles = (nt1+nt2)*32 bytes}; 2-40 byte trailers between frames | high | frame table 040000 entries point here; header/record/tile sizes chain contiguously | sub_C0A538 / sub_C0AE7E DMA |
| 07FFF8-080000 | 8 | unlabelled gap | low | entropy 2.41 | none |
| 080000-086AC0 | 27328 | 4bpp BG1 tiles, 854 tiles -> VRAM $2000 (game_mode 3); metatile max tile index 853 | high | sub_C0A46A at $8941 ($C8:0000, $6AC0) | $8941-$8950 |
| 086AC0-08C980 | 24256 | 4bpp BG1 tiles -> VRAM $2000 (game_mode 1); DMA size $6000 over-reads 0x140 bytes into the next set | high | sub_C0A46A at $855B ($C8:6AC0, $6000); metatile max tile index 622 | $855B-$856A |
| 08C980-08FFA0 | 13856 | 4bpp BG2 tiles, 433 tiles -> VRAM $5000 (game_mode 1) | high | sub_C0A46A at $856D ($C8:C980, $3620) | $856D-$857C |
| 08FFA0-08FFEC | 76 | sprite frames, live format: 1 frames {8-byte header: n1, n2, off2, n3, off3, nt1, vo2, nt2; n1+n2+n3 2-byte OAM records (x,y); 4bpp tiles = (nt1+nt2)*32 bytes}; 2-40 byte trailers between frames | high | frame table 040000 entries point here; header/record/tile sizes chain contiguously | sub_C0A538 / sub_C0AE7E DMA |
| 08FFEC-090000 | 20 | unlabelled gap | low | entropy 0.29 | none |
| 090000-095AC0 | 23232 | 4bpp BG1 tiles, 726 tiles -> VRAM $2000 (game_mode 0); metatile max tile index 725; render below | high | sub_C0A46A at $8329 ($C9:0000, $5AC0); B208 descriptor | $8329-$8338 |
| 095AC0-098AC0 | 12288 | 4bpp BG2 tiles, 384 tiles -> VRAM $6000 (game_mode 3) | high | sub_C0A46A at $8977 ($C9:5AC0, $3000) | $8977-$8986 |
| 098AC0-09B560 | 10912 | 4bpp BG2 tiles, 341 tiles -> VRAM $5000 (game_mode 0) | high | sub_C0A46A at $833B ($C9:8AC0, $2AA0); B208 record 00 50 C0 8A C9 | $833B-$834A |
| 09B560-09DCE0 | 10112 | metatile definitions, game_mode 2: 316 x 32 bytes (4x4 tilemap words, palettes 0,2-7) | high | $7E/$80 = $B560/$C9 at $87E6; map max index 315 | sub_C09FB7/sub_C09E83 |
| 09DCE0-09FD80 | 8352 | metatile definitions, game_mode 0: 261 x 32 bytes | high | $7E/$80 = $DCE0/$C9 at $82D7; map max index 260 | sub_C09FB7 |
| 09FD80-0A0000 | 640 | level map, game_mode 1: 40 columns x 8 rows of u16 metatile index (bits 14/15 = H/V flip), column-major 16 bytes/column | high | $7A/$7C = $FD80/$C9, $82=1, $86=$3FF | sub_C09FB7 |
| 0A0000-0A26A0 | 9888 | 4bpp BG2 tiles streamed mid-level -> VRAM $5220 ($2A00 bytes, over-reads 0x360 into the metatiles) | high | B208 descriptor 20 52 00 00 CA FF 00 2A via sub_C09C62 | sub_C09C62 |
| 0A26A0-0A37A0 | 4352 | metatile definitions, game_mode 1: 136 x 32 bytes | high | $7E/$80 = $26A0/$CA at $8532 | sub_C09FB7 |
| 0A37A0-0A4860 | 4288 | metatile definitions, game_mode 3: 134 x 32 bytes | high | $7E/$80 = $37A0/$CA at $88F9 | sub_C09FB7 |
| 0A4860-0A5760 | 3840 | level map, game_mode 0: 120 columns x 16 rows (32 bytes/column) | high | $7A/$7C = $4860/$CA, $82=0, $86=$DFF; ends exactly at the mode 2 map | sub_C09FB7 |
| 0A5760-0A6360 | 3072 | level map, game_mode 2: 64 columns x 24 rows (48 bytes/column) | high | $7A/$7C = $5760/$CA, $82=-1, $86=$6FF | sub_C09FB7 |
| 0A6360-0AE384 | 32804 | sprite frames, live format: 15 frames {8-byte header: n1, n2, off2, n3, off3, nt1, vo2, nt2; n1+n2+n3 2-byte OAM records (x,y); 4bpp tiles = (nt1+nt2)*32 bytes}; 2-40 byte trailers between frames | high | frame table 040000 entries point here; header/record/tile sizes chain contiguously | sub_C0A538 / sub_C0AE7E DMA |
| 0AE384-0AE38E | 10 | 10-byte gap | low |  | none |
| 0AE38E-0AEB8E | 2048 | 32x32 tilemap -> VRAM $1C00 (game_mode 0 BG3) | high | sub_C0A46A at $8317 ($CA:E38E, $800) | $8317-$8326 |
| 0AEB8E-0AF38E | 2048 | 32x32 tilemap -> VRAM $7000 (game_mode 0) | high | sub_C0A46A at $8365; B208 record | $8365-$8374 |
| 0AF38E-0AFB8E | 2048 | 32x32 tilemap -> VRAM $6800 (game_mode 0) | high | sub_C0A46A at $834D; B208 record | $834D-$835C |
| 0AFB8E-0AFFEC | 1118 | sprite frames, live format: 1 frames {8-byte header: n1, n2, off2, n3, off3, nt1, vo2, nt2; n1+n2+n3 2-byte OAM records (x,y); 4bpp tiles = (nt1+nt2)*32 bytes}; 2-40 byte trailers between frames | high | frame table 040000 entries point here; header/record/tile sizes chain contiguously | sub_C0A538 / sub_C0AE7E DMA |
| 0AFFEC-0B0000 | 20 | unlabelled gap | low | entropy 2.70 | none |
| 0B0000-0B4000 | 16384 | eight 32x32 tilemaps (2 KB each): B0000 -> $6C40 (mode 1), B0800 -> $7400 (mode 2), B1000 -> $5800 (mode 2), B1800 -> $7000 and B2000 -> $6800 (streamed, B208), B2800 -> $5C00, B3000 -> $1C00, B3800 -> $5800 (mode 3) | high | sub_C0A46A sites $8585,$8825,$884F,$892F,$8953,$8965; B208 records | listed sites |
| 0B4000-0BA4CA | 25802 | sprite frames, live format: 13 frames {8-byte header: n1, n2, off2, n3, off3, nt1, vo2, nt2; n1+n2+n3 2-byte OAM records (x,y); 4bpp tiles = (nt1+nt2)*32 bytes}; 2-40 byte trailers between frames | high | frame table 040000 entries point here; header/record/tile sizes chain contiguously | sub_C0A538 / sub_C0AE7E DMA |
| 0BA4CA-0BAC4C | 1922 | tilemap-like words ($1800,$1801,$1802,... increasing tile index, palette 6), 961 words, unreferenced, sits between two sprite frames | medium | word decode; no frame-table entry points here | none |
| 0BAC4C-0CA402 | 63414 | sprite frames, live format: 35 frames {8-byte header: n1, n2, off2, n3, off3, nt1, vo2, nt2; n1+n2+n3 2-byte OAM records (x,y); 4bpp tiles = (nt1+nt2)*32 bytes}; 2-40 byte trailers between frames | high | frame table 040000 entries point here; header/record/tile sizes chain contiguously | sub_C0A538 / sub_C0AE7E DMA |
| 0CA402-0CAB02 | 1792 | tilemap, 896 words (28 rows) -> VRAM $7400/$7420 (game_mode 1) | high | sub_C0A46A at $85BB/$85CD ($CC:A402, $700) | $85BB-$85DC |
| 0CAB02-0CB202 | 1792 | tilemap, 896 words -> VRAM $7000/$7020 (game_mode 1) | high | sub_C0A46A at $8597/$85A9 ($CC:AB02, $700) | $8597-$85B8 |
| 0CB202-0E8714 | 120082 | sprite frames, live format: 73 frames {8-byte header: n1, n2, off2, n3, off3, nt1, vo2, nt2; n1+n2+n3 2-byte OAM records (x,y); 4bpp tiles = (nt1+nt2)*32 bytes}; 2-40 byte trailers between frames | high | frame table 040000 entries point here; header/record/tile sizes chain contiguously | sub_C0A538 / sub_C0AE7E DMA |
| 0E8714-0E8D14 | 1536 | level map, game_mode 3: 32 columns x 24 rows (48 bytes/column) | high | $7A/$7C = $8714/$CE at $88EF, $82=-1, $86=$2FF | sub_C09FB7 |
| 0E8D14-1CC066 | 930642 | sprite frames, live format: 1415 frames {8-byte header: n1, n2, off2, n3, off3, nt1, vo2, nt2; n1+n2+n3 2-byte OAM records (x,y); 4bpp tiles = (nt1+nt2)*32 bytes}; 2-40 byte trailers between frames | high | frame table 040000 entries point here; header/record/tile sizes chain contiguously | sub_C0A538 / sub_C0AE7E DMA |
| 1CC066-1CC6AA | 1604 | tile-like data between the last table-referenced frame and the alternate-format frames | low | no header parses here | none |
| 1CC6AA-1F0000 | 145750 | sprite frames in an ALTERNATE format not read by the live code: same 8-byte header as the live format (n1, n2, off2, n3, off3, nt1, vo2, nt2; hdr[0] bit 7 set) + n1+n2+n3 3-byte OAM records {x, y, attr} + 4bpp tiles = (nt1+nt2)*32 bytes; frame boundaries recovered by chain-walking maximal runs of >=8 records with attr in 0x1C-0x22, then splitting each run by its own frames' decoded header lengths (`tools/gen_assets.py:parse_sprite_frame_alt_region`); 87 frames (5 of the 82 detected runs hold two frames back to back), many identical headers; no duplicates elsewhere | high | header decode accounts for every byte of all 87 frames exactly (`nt1+nt2 == 4*n1+n2+n3` in each); each frame is its own `sprite_frame_alt` asset; render below | none (unreferenced) |
| 1F0000-1F10C0 | 4288 | stale duplicate of 0A37A0-0A4860 (all 134 game_mode 3 metatiles) | high | 4288-byte exact match at delta -0x14C860 | none |
| 1F10C0-1F1100 | 64 | stale duplicate of 0A3760-0A37A0 (last 2 metatiles of metatiles_mode1) | high | 64-byte exact match at delta -0x14D960 | none |
| 1F1100-1F2780 | 5760 | stale duplicate of 09C660-09DCE0 (game_mode 2 metatiles 136-315) | high | 5760-byte exact match at delta -0x154AA0 | none |
| 1F2780-1F2E14 | 1684 | unreferenced (tile-like, entropy 5.2) | low |  | none |
| 1F2E14-1FFEE5 | 53457 | sprite frames, alternate format (as 1CC6AA): 32 frames (1 of the 31 detected runs holds two frames back to back), same chain-walk plus header-length split | high | header decode accounts for every byte of all 32 frames exactly; each is its own `sprite_frame_alt` asset | none |
| 1FFEE5-200000 | 283 | a complete alt-format frame, same 8-byte header + 5 records + 8 tiles as any other; the header accounts for 279 of the 283 bytes, a 4-byte trailer and no more | high | decodes cleanly with the same `sprite_frame_alt` codec, header length exact but for the trailer; not a truncated or partial region | none |

### 1a. Level scene data by game_mode (cross-reference)

| game_mode | BGMODE | BG1 tiles | BG2 tiles | tilemaps | metatiles | map | palettes (bank $C4) |
|---|---|---|---|---|---|---|---|
| 0 (`sub_C08292`) | 1 | 090000 ($5AC0) -> $2000 | 098AC0 ($2AA0) -> $5000 | 0AE38E -> $1C00, 0AF38E -> $6800, 0AEB8E -> $7000; OBJ tiles 0502C0 -> $1600 | 09DCE0 (261) | 0A4860 120x16 | 46DA8 -> $00, 46C48 -> $80, 46D68 -> $E0, 46D48 -> $F0 |
| 1 (`sub_C084D7`) | 1 | 086AC0 ($6000) -> $2000 | 08C980 ($3620) -> $5000 | 0B0000 -> $6C40, 0CAB02 -> $7000/$7020, 0CA402 -> $7400/$7420; 2bpp 050000 -> $1E00 | 0A26A0 (136) | 09FD80 40x8 | 46EA8 -> $00, 46C48 -> $80, 46D08 -> $A0, 46D88 -> $B0; HDMA colour 46FA8 |
| 2 (`sub_C08798`) | 1 | 070342 ($6FC0) -> $2000 | 07DF82 ($19E0) -> $6000 | 0B1000 -> $5800, 0B0800 -> $7400 | 09B560 (316) | 0A5760 64x24 | 46FE3 -> $00, 46C48 -> $80/$C0, 46CC8 -> $E0 |
| 3 (`sub_C088AB`) | 9 | 080000 ($6AC0) -> $2000 | 095AC0 ($3000) -> $6000 | 0B3000 -> $1C00, 0B3800 -> $5800, 0B2800 -> $5C00 | 0A37A0 (134) | 0E8714 32x24 | 47343 -> $00, 46C48 -> $80/$C0, 47443 -> $A0 |
| title (`loc_C0BB81`) | 3 | 06002B 8bpp (627 tiles) -> $0600 | - | 069CEB/06A02B/06A1EB/06A2EB (+$30) -> $6500/$6960/$6DA0/$61C0; $6000 filled with $0030 | - | - | 06A36B 256 colours; HDMA INIDISP table 00C00D |

Mid-level streaming: `sub_C09C62` walks the 8-byte descriptors at `00B208` (`{vram, src16, bank|$FF00 for VRAM / $00C4 for CGRAM, size}`, `$FFFF` terminated) selected per scroll page by `00B24C`; they re-upload 098AC0 -> $5000, 0AF38E/0AEB8E, 0A0000 -> $5220, 0B2000/0B1800 and 32-byte palettes 46E88/470E3 -> CGRAM $70.

### 1b. Sprite frame format (live)

`sub_C0A538`: frame id (`$07C8,X`, even) indexes `040000` -> `{ptr16, bank, y-bias}`. Frame at
`bank:ptr`: an 8-byte header (frame ids `< 4` use a 5-byte header), then `n1+n2+n3` 2-byte
`{x, y}` OAM records, then `(nt1 + nt2) * 32` bytes of 4bpp tiles which `sub_C0AE7E` DMAs to
VRAM `($0788,X & $1FF) << 4`. The header decodes to eight named fields, and it is the same
container the alternate format uses (section 1c), confirmed here by `oam_emit_frame_2row`
(`recomp/src/oam_emit.c`) reading it as three OAM "rows", one per sprite group, each with its
own count and tile/attribute base:

    hdr[0]  bit 7   always clear in the live format (records are always 2 bytes)
            bits 0-6  n1, the count of 16x16 sprites
    hdr[1]  n2, 8x8 sprites      hdr[2]  off2, the VRAM tile they start at
    hdr[3]  n3, 8x8 sprites      hdr[4]  off3, the VRAM tile they start at
    hdr[5]  nt1, tiles in the first DMA chunk, which lands at VRAM tile 0
    hdr[6]  vo2, where the second chunk lands   hdr[7]  nt2, its tile count

A 16x16 sprite is four tiles in the PPU's own name-table arrangement (`t, t+1, t+16, t+17`
across a sixteen-tile VRAM row; the emitter walks `tile += 2; if tile & $10: tile += $10`,
i.e. the `i`'th of the `n1` 16x16 sprites sits at `2*(i%8) + 32*(i/8)`), and each of the `n2`
then `n3` 8x8 sprites is one tile, at `off2` then `off3` in file order. `nt1 + nt2 ==
4*n1 + n2 + n3` in 1555 of the 1556 non-null table entries (the exception is entry 0, a dummy
that aliases the table's own start and is not real frame data); the other 1555 abut this
formula's declared length exactly, 970 with none of it left over and the rest separated by
2-40 byte trailers (e.g. `03 01 01 03 0E 0F 15 1F ...`, look like 8x1-bit masks) that no traced
code reads. 5 of the 1555 fall short of the length their own header declares by exactly `2*n3`
bytes (a neighbouring frame's data ends where theirs should still be running); those are kept
as raw, undecoded bytes rather than force-fit to a header that does not actually describe them.
Frames fill the tails of banks `$C7`, `$C8` and all of `$CA:6360`-`$DC:C066` around the level
data.

### 1c. Sprite frame format (alternate)

`1CC6AA-1F0000` and `1F2E14-1FFEE5`, plus the last 283 bytes of the ROM (`1FFEE5-200000`),
hold 120 frames in the *same* container the live format uses (section 1b), with no frame
table and no traced emitter pointing at any of it: an 8-byte header, then `n1+n2+n3` records,
3 bytes `{x, y, attr}` each (`hdr[0]` bit 7 is set in every one of these frames, unlike the
live format), then `(nt1+nt2) * 32` bytes of 4bpp tiles. `nt1+nt2 == 4*n1+n2+n3` holds
exactly, and the header accounts for every byte of every one of the 120 frames but for a
2-40 byte trailer, same convention as 1b.

With no frame table to read frame boundaries off, the boundaries were still recovered from
the bytes, but by decoding the header rather than guessing at it. `attr` only ever takes two
values, `$1E` and `$20` (2543 records total; see below), so a maximal run of `attr` in
`0x1C-0x22` is still a strong, unmistakable signal of "real OAM records here", and
`tools/gen_assets.py`'s `parse_sprite_frame_alt_region` uses that signal to find 82 + 31 = 113
*candidate* frames the way it always has. What changed is what happens inside each candidate:
its own header gives an exact length (the formula above), and for 107 of the 113 that length
is the candidate's whole extent. For the other 6, the header accounts for only part of the
candidate, and what is left over decodes as a further header-exact frame in its own right,
back to back with no gap. A naive one-per-candidate reading had merged these two frames into
one oversized asset. Splitting those 6 gives 87 + 32 = 119 frames, and the 283-byte tail
(formerly kept as a separate "partial/truncated" asset) is a 120th: its header accounts for
279 of its 283 bytes, a 4-byte trailer and no more. There is no truncated or partial frame
anywhere in the corpus; every one of the 120 is complete.

The header fields beyond `n1`/`n2`/`n3`/`nt1`/`nt2` (`off2`, `off3`, `vo2`) place the `n2`
and `n3` groups' 8x8 sprites and the second DMA chunk's tiles exactly as section 1b describes;
recomp/app/gallery.c's `frame_build_alt` implements the same placement for the gallery viewer.
No byte of any of the 120 headers is left unaccounted for or carried through opaque.

`attr` decodes losslessly as a standard SNES OBJ low-attribute byte, `vhppp p N` bit for bit:
bit 7 v-flip, bit 6 h-flip, bits 5-4 priority, bits 3-1 palette, bit 0 tile-index bit 8. Across
all 2543 records in all 120 frames only two values occur, `$1E` (473 records: priority 1,
palette 7) and `$20` (2070 records: priority 2, palette 0); v-flip and h-flip are 0 throughout
(no frame is ever mirrored). There are no other values and no noise records: the handful of
`0x1D`/`0x1F` bytes an earlier, coarser record-run scan (over candidate spans rather than
header-exact ones) had picked up were tile bytes at a candidate's now-corrected boundary, not
real OAM records; decoding the header exactly removes them. There is no dedicated "size" bit
in this byte; SNES OBJ size comes from a separate high-table bit per pair of sprites, which
this format does not carry at all: size instead comes from which of the three header groups a
record falls into, exactly as in the live format.

Because the container and its tile-placement rule are now known exactly rather than guessed,
`tools/assetcodec.py:decode_sprite_frame_alt` places every tile at the one sprite the header
names for it. There is no tile left unclaimed and no per-record layout guess. Two different
sprites' own boxes can still land on overlapping screen pixels (ordinary in this art: roughly
15% of tiles, across both sprite-frame formats, sit under a later-placed neighbour), and a
tile that loses that overlap keeps its own copy in a small, deterministic overflow area below
the canvas so it stays exactly recoverable; see "Alternate sprite frames" below. The tile art
itself, once rendered, is unambiguously more sprite tiles in the same house style as the live
frames (see the render below): mid-size 4bpp character/creature fragments, same tile size and
similar palette density, not tilemap or font data. Whether any given alt frame is an earlier
or later revision of a specific live-format character cannot be established without in-game
character names or labels, neither of which exist in this disassembly; what can be said is
that the two corpora are stylistically and structurally the same kind of asset, in the same
container format.

**External corroboration.** Nothing here was derived from it. The header/record/tile layout
above comes entirely from this ROM's own bytes, cross-checked against `recomp/src/oam_emit.c`'s
traced emitter, but the same three-group layout (one block of double-size sprites, two blocks
of single-size ones, each with its own VRAM tile base) is independently described for the
retail Donkey Kong Country games' sprite format in a public writeup: DKC Atlas forum, "ALL:
Sprite Graphics" (2011). Per `docs/LEGAL.md` rule 4, this is read and cited as corroborating
evidence that Rare's SNES sprite tooling of this era used the same kind of container across
titles; no bytes, symbols, or text are taken from it.

### 1d. Palette assignment

Which palette bytes end up in which CGRAM entries, and which of those entries any given
tileset or sprite frame is actually drawn with. Everything here is read off the mode-init
bodies (`recomp/src/mode_init.c`, `recomp/src/top_level.c`, both transliterations of
`out/dream.asm`) and the tables section 1 already names; the numbers at the end are
measured against the running game with `dream_harness --dump-cgram/--dump-oam`.

#### CGRAM: what each scene uploads

`dma_upload_to_cgram` (`sub_C0A483`) takes `A` = source address in bank `$C4`, `X` =
byte count / 8 and `Y` = `CGADD`, so one call writes `X * 4` colours starting at CGRAM
entry `Y`. Each scene issues four of them, in a fixed order, and a later narrower call
overwrites part of an earlier wide one, so two scenes can give the same sprite two different
palettes.

| scene | order | CGADD | colours | source | call site |
|---|---|---|---|---|---|
| `game_mode` 0 | 1 | `$00` | 128 | `046DA8` | `C08386` |
| | 2 | `$80` | 128 | `046C48` | `C08392` |
| | 3 | `$E0` | 16 | `046D68` | `C0839E` |
| | 4 | `$F0` | 16 | `046D48` | `C083AA` |
| `game_mode` 1 | 1 | `$80` | 128 | `046C48` | `C0873A` |
| | 2 | `$A0` | 16 | `046D08` | `C08746` |
| | 3 | `$00` | 128 | `046EA8` | `C08752` |
| | 4 | `$B0` | 16 | `046D88` | `C08776` |
| | 5 | `$E1` | 1 | `008791` (two `sta CGDATA`, not a DMA) | `C0877D` |
| `game_mode` 2 | 1 | `$80` | 128 | `046C48` | `C0886A` |
| | 2 | `$C0` | 64 | `046C48` | `C08876` |
| | 3 | `$E0` | 16 | `046CC8` | `C08882` |
| | 4 | `$00` | 128 | `046FE3` | `C0888E` |
| `game_mode` 3 | 1 | `$80` | 128 | `046C48` | `C08992` |
| | 2 | `$C0` | 64 | `046C48` | `C0899E` |
| | 3 | `$A0` | 16 | `047443` | `C089AA` |
| | 4 | `$00` | 128 | `047343` | `C089B6` |
| title (`loc_C0BB81`) | 1 | `$00` | 256 | `06A36B` | `C0BC2A` (a `sta CGDATA` loop, 512 bytes) |

Replaying those in order gives the CGRAM each scene runs with:

| scene | CGRAM `$00-$7F` (BG rows 0-7) | CGRAM `$80-$FF` (OBJ rows 0-7) |
|---|---|---|
| 0 | `046DA8` | `$80-$DF` `046C48`; `$E0-$EF` `046D68`; `$F0-$FF` `046D48` |
| 1 | `046EA8` | `$80-$9F` `046C48`; `$A0-$AF` `046D08`; `$B0-$BF` `046D88`; `$C0-$FF` `046C48` + `$40` colours (= `046CC8`), except `$E1` from `008791` |
| 2 | `046FE3` | `$80-$BF` `046C48`; then `$C0-$FF` `046C48` again, so rows 4-7 repeat rows 0-3; then `$E0-$EF` `046CC8` on top |
| 3 | `047343` | `$80-$BF` `046C48`; then `$C0-$FF` `046C48` again (rows 4-7 repeat rows 0-3); then `$A0-$AF` `047443` on top |
| title | `06A36B`, all 256 entries; BGMODE 3's BG1 is 8bpp, so the pixel byte *is* the CGRAM index and the tilemap's palette field does not apply | none (no OBJ on the title's `TM $01`) |

Per OBJ palette that is:

| OBJ palette | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| CGRAM | `$80` | `$90` | `$A0` | `$B0` | `$C0` | `$D0` | `$E0` | `$F0` |
| mode 0 | `046C48` | `046C68` | `046C88` | `046CA8` | `046CC8` | `046CE8` | `046D68` | `046D48` |
| mode 1 | `046C48` | `046C68` | `046D08` | `046D88` | `046CC8` | `046CE8` | `046D08` | `046D28` |
| mode 2 | `046C48` | `046C68` | `046C88` | `046CA8` | `046C48` | `046C68` | `046CC8` | `046CA8` |
| mode 3 | `046C48` | `046C68` | `047443` | `046CA8` | `046C48` | `046C68` | `046C88` | `046CA8` |

Modes 2 and 3 alias palettes 4-7 back onto 0-3, which is why the same character frame
drawn with palette 0 in mode 0 and palette 4 in mode 2 comes out in the same colours.

#### CGRAM after the init

Four sources rewrite CGRAM once the scene is running, so a capture of the live game need
not agree with the table above everywhere:

- mode 1's HDMA channel 1 (`DMAP1 $03`, `BBAD1 $21` = `CGADD`/`CGDATA`, armed at
  `C08669`) runs the 59-byte table at `046FA8`: one 4-byte set held for 96 scanlines,
  then 13 sets one line each, writing entries `$00,$01,$02,$03,$05,$06,$07,$09,$0A,$0B,$0D,$0E,$0F`.
  At the end of a frame eleven of those (`$01 $02 $05 $06 $07 $09 $0A $0B $0D $0E $0F`)
  hold the table's values rather than `046EA8`'s; `$00` and `$03` happen to coincide.
- the colour ramp at `046B88` (96 greys) drives CGRAM entry `$12` through `sub_C08D6B`;
  no script under `recomp/harness/inputs/` reaches it and the captures never show `$12`
  moving.
- `cgram_palette_ramp_step` (`$C091BB`) queues a 64-byte block for CGRAM `$80` that
  `cgram_upload_queue_flush` performs; likewise unreached by the scripts.
- the streaming descriptors at `00B208` re-upload a 32-byte palette to CGRAM `$70`.
  There are two runs, both selected by `sub_C09C62` for the only caller it has,
  `mode0_weather_zone_update`, so both belong to `game_mode` 0: the first ends with
  `046E88`, which is `046DA8` + `$70` colours (the sixteen colours the init already put
  there, so it restores rather than changes) and the second with `0470E3`, sixteen
  colours that are genuinely different. No script under `recomp/harness/inputs/` reaches
  the second zone; in every mode-0 capture CGRAM `$70-$7F` holds the init's values.

Everything else is constant for the life of a scene: over frames 220-255, 340-375,
460-495 and 580-615 of `mode_cycle.txt` (settled mode 0/1/2/3) and 300-499 of
`level_walk_jump.txt`, not one CGRAM entry changes value.

#### BG layers: which tileset is drawn through which palette rows

`BG12NBA`/`BG34NBA` and `BGnSC` are written once per scene as 16-bit stores, so the low
byte is `BG12NBA`/`BG1SC` and the high byte `BG34NBA`/`BG2SC`:

| scene | BGMODE | BG12NBA | BG34NBA | BG1SC | BG2SC | BG3SC |
|---|---|---|---|---|---|---|
| 0 | 1 | `$25` (BG1 chars `$5000`, BG2 `$2000`) | `$05` | `$69` -> map `$6800`, 64x32 | `$79` -> map `$7800` | `$1C` -> map `$1C00` |
| 1 | 1 | `$52` (BG1 `$2000`, BG2 `$5000`) | `$05` | `$79` -> `$7800` | `$70` -> `$7000` | `$74` -> `$7400` |
| 2 | 1 | `$26` (BG1 `$6000`, BG2 `$2000`) | `$06` | `$5A` -> `$5800` | `$79` -> `$7800` | `$74` -> `$7400` |
| 3 | 9 | `$26` (BG1 `$6000`, BG2 `$2000`) | `$06` | `$58` -> `$5800` | `$79` -> `$7800` | `$5C` -> `$5C00` |
| title | 3 | `$00` (BG1 chars `$0000`) | - | `$60` -> `$6000` | - | - |

In every scene the metatile blitter (`build_metatile_column_580`/`_500` and their
`vram_upload_column_*` partners) writes its columns and rows to VRAM `$7800`, which the
table above makes BG2's map in modes 0, 2 and 3 and BG1's map in mode 1. The other layer
gets a static tilemap DMA'd in by the init.

That fixes the pairing of tileset to map, and the maps' own contents confirm each pairing
independently: the largest tile index a map uses is exactly one less than the tile count
of the set it is paired with.

| scene | BG | char base | tileset asset | tiles | map that carries its palette bits | max tile index | palette row most of its words use |
|---|---|---|---|---|---|---|---|
| 0 | BG2 | `$2000` | `090000` (`bg1_tiles_mode0.bin`) | 726 | `09DCE0` metatiles_mode0 | 725 | **1** (1370 of 4176 words) |
| 0 | BG1 | `$5000` | `098AC0` (`bg2_tiles_mode0.bin`) | 341 | `0AF38E` (`$6800`) + `0AEB8E` (`$7000`, streamed) | 340 | **0** (1710 of 2048) |
| 1 | BG1 | `$2000` | `086AC0` | 758 | `0A26A0` metatiles_mode1 + `0B0000` | 757 | **1** (856 of 3200) |
| 1 | BG2 | `$5000` | `08C980` | 433 | `0CAB02` (`$7000`) | 432 | **1** (768 of 896) |
| 2 | BG2 | `$2000` | `070342` (`bg1_tiles_mode2.bin`) | 894 | `09B560` metatiles_mode2 | 893 | **2** (2341 of 5056) |
| 2 | BG1 | `$6000` | `07DF82` (`bg2_tiles_mode2.bin`) | 207 | `0B1000` (`$5800`) | 206 | **0** (686 of 1024) |
| 3 | BG2 | `$2000` | `080000` (`bg1_tiles_mode3.bin`) | 854 | `0A37A0` metatiles_mode3 | 853 | **3** (761 of 2144) |
| 3 | BG1 | `$6000` | `095AC0` (`bg2_tiles_mode3.bin`) | 384 | `0B3800` (`$5800`) | 383 | **0** (717 of 1024) |
| title | BG1 | `$0000` (`stz BG12NBA`) | `06002B` 8bpp, DMA'd to VRAM `$0600` | 627 | none (8bpp: no palette field) | - | - |

The title's tiles land at VRAM `$0600` with a character base of `$0000`; 8bpp tiles are
32 words each, so the first of them is tile index `$0600 / $20` = 48, exactly
the `+$0030` the init adds to every word of its four tilemaps at `$BC66` onward.

Two notes on the names. The manifest's `bg1_`/`bg2_` file names were assigned from the
upload order, not from the registers, so in modes 0, 2 and 3 they are the other way round
from the PPU's BG numbers; the table above uses the registers. And mode 1's `050000`
(32 2bpp tiles, DMA'd to VRAM `$1E00`) cannot be BG3 chars under `BG34NBA $05` (`$1E00`
is below that base and inside the OBJ tile area the sprite-frame DMAs use), so the
manifest's "game_mode 1 BG3" note on it is not supported by the registers; left open.

A bare tileset page has no tilemap word to take a row from, so "the row most of its tiles
are referenced with" (last column) is the best available default, and the count is quoted
with it so the reader can see how strong it is. Where a tilemap page *is* shown, the
word's own bits 12-10 pick the row and nothing is guessed.

#### Sprites: which OBJ palette a frame is drawn with

`entity_build_oam_frame` loads `entity_flags` (`$0788,Y`) into `$18` and `$1A` at
`$C0A5BE` as a 16-bit store. The six emitters then work on `$1A` in 8-bit mode only
(`lda $1E ; adc $18 ; sta $1A` builds the tile number) and write the pair back to OAM
with a 16-bit `sta $02,X`. So the high half, `$1B`, is untouched from `$C0A5BE` onward
and **every sprite of the frame carries `entity_flags >> 8` as its OAM attribute byte**.
That byte is the standard OBJ low-attribute layout section 1c decodes (bit 7 v-flip,
bit 6 h-flip, bits 5-4 priority, bits 3-1 palette, bit 0 tile-index bit 8), so the OBJ
palette is `(entity_flags >> 9) & 7`.

`entity_flags` is written by `entity_init_from_table` (`$C09D6A`) from byte +12 of the
18-byte init record, and after that by exactly six sites, none of which can change bits
9-11:

| site | operation | bits it can change |
|---|---|---|
| `$C0996A` | `and #$BFFF ; ora facing_flag_table,Y` | 14 (h-flip): the table holds only `$0000`/`$4000` |
| `$C0B1CD` | the same pair, from `sub_C0B1B1` | 14 |
| `$C09AD5` | `eor`/`and #$7000`/`eor` (copy from parent) | 12-14 |
| `$C09BA6` | the same, `#$7000` | 12-14 |
| `$C09B2D` | the same, `#$4000`, then `ora #$8000` | 14, 15 |
| `$C09B6E` | the same, `#$C000` | 14-15 |

`entity_type` is likewise written only by `entity_init_from_table`. So an entity's OBJ
palette is a constant of the scene's init table:

| scene | slot | `entity_type` | `entity_flags` | OAM attr | OBJ palette | priority |
|---|---|---|---|---|---|---|
| 0 | 0 | `$02` | `6000` | `60` | 0 | 2 |
| 0 | 1 | `$04` | `2240` | `22` | 1 | 2 |
| 0 | 2 | `$06` | `0480` | `04` | 2 | 0 |
| 0 | 3 | `$0A` | `22C0` | `22` | 1 | 2 |
| 0 | 4 | `$0E` | `6900` | `69` | 4 | 2 |
| 1 | 0-5 | `$02 $04 $06 $0A $0E $06` | `6000 2240 0480 22C0 2900 0480` | | 0, 1, 2, 1, 4, 2 | |
| 1 | 6, 7 | `$00`, `$00` | `1D40`, `5D40` | `1D`, `5D` | 6, 6 | 1 |
| 2 | 0-5 | `$02 $04 $06 $0A $0C $0E` | `6800 2A40 0480 2AC0 2F00 2D40` | | 4, 5, 2, 5, 7, 6 | |
| 3 | 0-3 | `$02 $04 $06 $0A` | `2800 2A40 0C80 2AC0` | | 4, 5, 6, 5 | |

(The `entity_flags` column is the record's; a running entity's h-flip bit will differ.)

From entity to frames: `entity_anim_id` comes from the two-level table at `data_C0B7AE`,
indexed `B7AE + entity_state + $0BAC + word[B7AE + entity_type]`, with `$0BAC` = `$0018`
in `game_mode` 1 and 0 elsewhere (`$C084D7` against `$C08292`/`$C08798`/`$C088AE`). The
state machine can put an entity in any of the twelve even states `$00`-`$16`
(`sub_C0B171` sets `$10`-`$16`, `anim_cb_reset_state` sets 0, and the facing update only
ever ORs `$0002`/`$0012` into the low bits), so a type's reachable animations are that
whole row. Three types take theirs from another entity instead:

- `$00`: `entity_init_from_table`'s own special case: `entity_anim_id` is the record's
  second word verbatim, not a table lookup;
- `$04` (`entity_spawn_transform_a`): parent's animation + 2 (`$C09AE4`);
- `$06` (`entity_spawn_transform_b`): parent's + 4 (`$C09B3F`), except that `game_mode` 1
  forces `$0158` (`$C09B77`) and `game_mode` 2 returns before touching the animation at
  all (`$C09B0B`), which is why mode 2's slot 2 never shows a frame;
- `$0A` (`entity_spawn_transform_c`): parent's + 4 + `$0BB6` (`$C09BC0`/`$C09BC3`), and
  `$0BB6` cycles 2 -> 4 -> 0 at `$C081DB`; at 0 the entity is not drawn, so the reachable
  set is parent + 6 and parent + 8.

From animation to frames: each id indexes `data_C41858` (174 words, 96 distinct scripts);
a script is 8-byte `{callback, mode, duration, frame}` records running to whichever comes
first: a `duration` of `$FFFE` (loop), a `duration` of `$FFFF` (switch to the animation
id in `frame`, followed here as a link), or the next script's offset. Every `frame` field
is a byte index into the frame table at `040000`, whose entry gives the frame's bank and
pointer.

Doing that for all four scenes gives, for each of the 1555 live frame assets, the set of
`{scene, OBJ palette}` pairs the game can draw it with:

| distinct `{scene, palette}` pairs | frames |
|---|---:|
| 0 (no entity in modes 0-3 plays an animation that names it) | 492 |
| 1 | 159 |
| 2 | 218 |
| 3 | 686 |
| 4 | 0 |

and per palette (a frame can count in more than one row):

| OBJ palette | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| frames | 238 | 496 | 38 | 0 | 644 | 928 | 219 | 90 |

The 492 with none are not unreferenced data: 1539 of the 1555 are named by *some* script
in the index, but only 50 of the 174 animation ids are reachable from the entity roster
the four scenes actually spawn. For those frames the honest answer is "no evidence from
the live game", and the gallery says so rather than picking a palette.

The 120 alternate-format rows (section 1c) have no frame-table entry at all, so no
animation can name them; their nearest evidence is internal. Their 3-byte `{x, y, attr}`
records carry an OBJ attribute byte of their own, and across all 2543 of them only `$1E`
(priority 1, palette 7) and `$20` (priority 2, palette 0) occur, so each frame's own
records give it a palette index, read off the frame, not inferred from anything else.

The one OBJ tileset the game uploads as tiles rather than as a frame, `0502C0` (96 tiles
to VRAM `$1600` in mode 0), is the particle set: `mode1_reset_particles_and_oam` builds
its attribute word as `(rng & $3000) | $0E00` at `$C0931E`, i.e. OBJ palette 7 with a
random priority.

#### Checked against the running game

`dream_harness --dump-cgram --dump-oam` writes the reference machine's CGRAM and OAM
after every frame, out of the same snapshot the frame line hashes.

CGRAM. The table built from the uploads above equals the capture on **all 256 entries the
init writes** for the title (`mode_cycle.txt` frame 100, the fade-in finishes at frame 76
and it matches from there to the end of the title), `game_mode` 0 (`level_walk_jump.txt`
frame 400 and `mode_cycle.txt` frame 250), `game_mode` 2 (frame 490) and `game_mode` 3
(frame 610). `game_mode` 1 (frame 370) differs on **11 of 256**, and they are exactly the
eleven entries the `046FA8` HDMA table leaves changed at the end of a frame:
`$01 $02 $05 $06 $07 $09 $0A $0B $0D $0E $0F`. Over all six captures that is 11 of 1536
entries compared, all of them accounted for. Within a settled scene nothing moves at all:
over frames 220-255, 340-375, 460-495 and 580-615 of `mode_cycle.txt` and 300-499 of
`level_walk_jump.txt`, not one CGRAM entry changes value.

OAM. The palette bits of every drawn entity are checked against the set this derivation
gives the frame that entity is showing, over `level_walk_jump.txt` frames 190-499
(`game_mode` 0) and `mode_cycle.txt` frames 190-255, 305-375, 424-495 and 544-615
(`game_mode` 0, 1, 2, 3): **3096 entity-frame observations, 22 distinct entities, 3096
matches and no conflicts**, with no observation landing on a frame the derivation does not
cover. In 2421 of the 3096 the entity's `entity_flags >> 8` is one of the attribute bytes
actually present in the captured OAM that frame (the rest were culled off-screen by
`entity_build_oam_frame`, which never reaches the emitter). A single frame's attribute
bytes read, for example: mode 0 frame 400 `$20` x14, `$22` x11, `$84` x10, `$3F` x6
(particles); mode 1 frame 370 `$1D` x17, `$62` x11, `$57` x8, `$2F` x7, `$60` x6, `$44` x5,
`$17` x2; mode 2 frame 490 `$6A` x14, `$2F` x12, `$68` x10, `$29` x2; mode 3 frame 610
`$2A` x14, `$28` x13, `$8C` x9.

Observed, at run time. `recomp/app/scene.c` does the same comparison inside the app,
on a machine of its own, and keeps the result: over `mode_cycle.txt` (700 frames),
`level_walk_jump.txt` (620) and `level_attack_enemy.txt` (620) it records, for every
frame id an entity is showing while that entity's `entity_flags >> 8` is one of the
attribute bytes in the OAM the PPU just drew from, the palette bits that byte carries.
**4583 entity-frame observations** over those 1940 frames name **194 of the 1555 live
frames**, each in exactly one scene; 869 more have a derived palette and no observation,
and 492 have neither. Every observation agrees with the derivation. Adding
`p2_enemy_attack.txt` as a fourth script (game_mode 2, with player 2 attacking) raises
the observation count to 5887 and the frame count not at all: the scripts loop over a
small set of animations, and that is the honest ceiling of what watching the game can
say.

One point the sampling has to respect: `game_mode` (`$A4`) changes about 46 frames before
`entity_init_from_table` repopulates the entity arrays, because the Select mode-advance
sets the mode and then fades out before the re-init runs. In `mode_cycle.txt` the mode
changes at frames 257, 377, 497 and 617 and the roster matches the new mode's init records
from frames 304, 423, 543 and 684. Sampling inside that gap compares the *old* scene's
entities against the *new* scene's number and looks like a contradiction; it is not one.

### 1e. The bank `$C1` picture strips: tile base and what they say

`010000`, `012800` and `013300` are 32x4 tilemaps (128 words each) with a 4bpp tileset
behind each of them, and nothing in the ROM uploads any of it, so the base a tile number
counts from has to come out of the bytes rather than off a `VMADDL` write.

Each map pads with one index over and over (all three pad with `$044`) and its content
runs upward from just above it. If the set holds an all-zero tile, that is the tile the
pad means, so the base is `pad - (that tile's index)`; if the set holds none, the pad is
below the set and the base is the lowest content index, which puts the first
content tile at tile 0. That gives a **different base per strip**:

| strip | map | tileset | tiles | pad | content | all-zero tiles | base | words outside the set |
|---|---|---|---:|---|---|---|---|---:|
| 1 | `010000` | `010100-012800` | 312 | `$044` x56 | `$045`-`$089` | 0, 78, 158, 218 | `$44` | 0 |
| 2 | `012800` | `012900-013300` | 80 | `$044` x42 | `$050`-`$095` | none | `$50` | 0 |
| 3 | `013300` | `013400-014FE0` | 223 | `$044` x19 | `$045`-`$0A0` | 1, 102, 145 | `$43` | 0 |

With those bases **no word in any of the three maps lands outside its own set**: nothing
spills into the next region, which the single `$44` the earlier reading used could not
say (it left strip 2's two highest words out of range and drew strip 2's and strip 3's
padding as real tiles: the dotted band and the striped band in the old render).

Rendered at those bases the three read as baseball captions: strip 1 `STRIKE 1`, strip 2
`TIME` … `OUT` (two blocks of eleven columns with a gap between them), strip 3 `HIT BY`
… `PITCH`, the caption starting in the right half of row 0 and continuing on rows 2-3.

The sets are larger than one caption needs (strip 1 is 312 tiles for a 70-tile caption),
and every all-zero tile in a set starts another block of the same shape, four in strip
1 and three in strip 3. Re-basing the *same* map onto strip 1's second block (base
`$44 - 78`) reads as `STRIKE 2`; the later blocks want maps of their own, which are not
in the ROM, so they come out mis-tiled. Strip 1's first 256 bytes repeat at `+$9C0`,
exactly 78 tiles on, which is the same block spacing.

Palette: every word of all three maps carries palette 7 and priority 1, and nothing
uploads a palette 7 for them, so there is no answer, only a best guess. Ranking the
ROM's own 16-colour rows by what the art needs (colour 0 dark and the other fifteen a
monotone ramp, since the tiles use all sixteen values with 52% of pixels at 0) puts row
7 of the main palette block (`046C48 + 7*32` = `046D28`) first: it is the row the words
name, and it is a clean fifteen-step gold ramp. The runners-up are `046EA8` row 2, the
previous build's `007AC8` row 30, and `046FE3` row 7. The 2bpp font at `014FE0` is a
separate case: only pixel values 0 and 1 ever occur in it, so only entries 0 and 1
matter, and the title palette's row 0 (`06A36B`: black, then white) is the best of them.

## 2. Sample tile renders (ASCII, 4bpp/8bpp pixel value -> ` .:-=+*#%@ABCDEF`, 2bpp -> ` .:#`)

Sprite tiles of frame `0A6360` (header `0e 04 2c 0b 3c 47 00 00`: 14+4 sprites, 71 tiles), first 8 tiles at `0A638C`:

```
.*E@CF*  ---FF--- %:         ..::%- -----@AA =A%----- ----=-     :=%-:=
.*CADB = ::-EFE-- -::        ...::: ----FAAF :AEFFFFE ---=FFF=   :=:%-%
=-@ EB+. %:--EE-- :::        :::::: %%-E@FF@  AB--EEF FFEA-::-   -%%E%F
D:=:B-%%  ::: :    %%  CCC ACC::%   -C%EEC== =:% %-.. .. : %%  CCCC CCE
.A - - % . :::::  %=%%%%CC F%% %%    = E      .E=+... ..   %%% CCCCC%%C
 F%D-FD@ .   .     =%% %%C CC %:%    %:C:C    .E%@-.. .. = %%% CCCCCCCC
 FDCCFD@ @          %%%%%C CCCC %    C %A     -:FB... ...%-@%C CCC%DCCC
DCE-EFCC    .     : =%%%CC CCC %% C %% E%CDB -:: B% - ...@-%CC C%C%D%C 
```

game_mode 0 BG1 4bpp tiles at `090200` (tiles 16-23):

```
ABBA%### @@%#*++* =+%@B@AB #A@#+... -=-.===. ....::==    =CA%# -++=.=..
BBA%#+** #%%#**#% +==+#+++ *++..... ...=*=-. .....-++    -BC#= =**+..#+
BB@##+++ ###***%# #=#*=... ........ ..-#%=-. .....=+*    +%AA* -=*+=.*=
AA%*+=== +*++==*# #=#@A*=. ........ ..+*=... ....-+*+    %A=%* =-+=-.=#
A%#*=++= ++====+* @+%@B@*- .-...... -.*=.... ....=*+-    AC#== %#A@@..-
A@%*+**+ +===+=-* @*ABB@*- .--.--== --*=.... ..:=++--    %CCA# *===-...
A@%**#*+ ----.-.+ #%ACBA%= -+#+++** .*@*=... .::==::=   -#ABCB =.......
A@%#*##* ......-- *%BEDB@+ ++##*+=- =@%+-... ..:...==   .%@@AA ++..--..
```

Title-screen 8bpp tiles at `0629AB` (tiles 100-107 of `06002B`):

```
=:*:*:-- --:::*=: =:###### #+#.      ...+++# ###:**** =--::::: ::::::::
*:::---- --::::*: :####### ###.     . ..+..# ###:.*** ::**==** **= .***
::-:::-- --:=::*: =####### ##...     ....... ###:..*- . .*  ** *     **
-:::::-- --::*::: :####### ##.+.      . .... .##:---- -------- --------
::::::-- --:**::* =####### ###+..      . ... ..#::::: ::**:::: ::::**::
::::::-- --::*=*: =####### ##....     ...... .####### ######## ######::
::::::-- --::*=== =####### ###..        .+.. ..#.#### ######## #####=:#
::::::-- --::*=== =####### ##...        .... ..###### ######## :####==#
```

Font at `014FE0`, glyphs `A`-`P` (`0151F0`) and `0`-`?` (`0150E0`):

```
  ...    ......     ....   .....    .......  .......    ....   ..   ..    ....      ....  ..   ..  ..       ..   ..  ..   ..   .....   ......  
 .. ..   ..   ..   ..  ..  ..  ..   ..       ..        ..      ..   ..     ..        ..   ..  ..   ..       ... ...  ...  ..  ..   ..  ..   .. 
..   ..  ..   ..  ..       ..   ..  ..       ..       ..       ..   ..     ..        ..   .. ..    ..       .......  .... ..  ..   ..  ..   .. 
..   ..  ......   ..       ..   ..  ......   ......   ..  ...  .......     ..        ..   ....     ..       .......  .......  ..   ..  ..   .. 
.......  ..   ..  ..       ..   ..  ..       ..       ..   ..  ..   ..     ..        ..   .....    ..       .. . ..  .. ....  ..   ..  ......  
..   ..  ..   ..   ..  ..  ..  ..   ..       ..        ..  ..  ..   ..     ..    ..  ..   .. ...   ..       ..   ..  ..  ...  ..   ..  ..      
..   ..  ......     ....   .....    .......  ..         .....  ..   ..    ....    ....    ..  ...  .......  ..   ..  ..   ..   .....   ..      
                                                                                                                                               

  ...       ..     .....    ......     ...   ......     ....   .......   .....    .....     ..                                          .....  
 .  ..     ...    ..   ..      ..     ....   ..        ..      ..   ..  ..   ..  ..   ..    ..       ..                                ..   .. 
..   ..     ..        ...     ..     .. ..   ......   ..           ..   ..   ..  ..   ..             ..        ..               ..         ... 
..   ..     ..      ....     ....   ..  ..        ..  ......      ..     .....    ......                      ..       ....      ..      ...   
..   ..     ..     ....         ..  .......       ..  ..   ..    ..     ..   ..       ..    ..       ..      ..                   ..           
 ..  .      ..    ...      ..   ..      ..        ..  ..   ..    ..     ..   ..      ..     ..       ..       ..       ....      ..      ..    
  ...     ......  .......   .....       ..    .....    .....     ..      .....    ....              ..         ..               ..       ..    
                                                                                                                                               
```

Alternate-format frame at `1D0000` (19 OAM triples then tiles at `1D0041`), first 8 tiles:

```
         BBBB         ==+* +-       :---:-:: =.-=-     ++*+==+ --+%%*##
      BB AA@@A       :.-+* =-       ..---=-: -=--.=    +++--== -=%%+*##
     BBA AAA@@B     +-.=== :+       :..-.:=- -:.-.-    +=+--=: :+###%+=
     BBA AAAA@@B    +===== :+       =..-..-. --.-..=   *++=++= =+++++++
    BBBA AA@@@@B   EF+==+= ==-    B =..-..+- ++.-:.=   ***++*= =+==+==+
    BBBA AA.@@AB   +F=-=++ +=F    B =..-:.#= #%-=-.-   +=*+=+= =+=++++=
    BBBA A@@@AAB     +=F== FFF    B =.:+-.## =#-#=.-= ++=++=+- -+=+=-=-
    BBBB BAAAAABD    BBFFF FFB    B =###-:## =+-#===+ +=++=-== +==-:::-
```

Stale-image tiles at `004C00` (unreferenced 4bpp):

```
   %%%%     %%%%   DDEDCD   EEDEEDD    FFEDD DDCDEFFD  DC@%@CD EEDCBBBB
   %%%%     %%%%   DDDDDE   EDEEEDC   FFFEDD DDDEEFEB   B@%%@B DDDCABCB
   %%%     %%%%     BCCDDD  EEEEDBC  EFFFEED DEEEDDB@    B@ABA @@A@ACCC
    :*::   %CEEAA =CFFFFFB -----###   C=#A:: FFEE:-   :::*B#-- FB#F--- 
    ::::   CCEAA* =CFFFFF# ----BBBB %==%FF-- FFFFFFFC EEE:*::: EE*F--- 
    ::::  CCCEA*E  FFFFFF* ----BBBB =  =BB#- BFFFFBB% A*AAE:E* AE*A:-::
    :::   CCCE*E% -FFFFFA*  ---FBB# == =#BF- -FBBB##= -:AAE:E* AA:::-::
   ::::   %%AA*E  -BBBBF*%  --#FBB# == ##BF- BBB##-F= -BEEEFEA A*E:::::
```

2bpp BG3 tiles at `050000` (game_mode 1, sparse particle-like shapes):

```
..                                  ..                ..                ..                  .               :.                       .         
 .:#.    ...      :.    .: .         :  :.   ..::      ..  ..:          .#.:##.  ..::.      .....:          : :#:..  .::::.      .  .#         
 : .#:#  ..::::   ::.  :.:  ...:..  .: :..:. ..::##:   .::# ##      .   .#. ::.: ..:####.   ...::         . :#. :# : ..:####.    ..  .      ...
.: : # .   ..:::    ......  ..      .: .# :    ...:.   .   ...          .:  ..:    ....        ...          .#  . .    ....        ...         
......             .   ...  .       ....:.                 ...          ...::.                 ...          ....:                  ...         
: ..      .                         .:.       .                         .:.       .                         .##       .                        
 #       :                          .         :                          .        :                          .        :                        
.                                            .                          ..                                   .                                 
```

## 3. Compression evidence

- No decompressor exists in the live program. Every graphics upload is a raw DMA from ROM (`DMAP=$1801`, fixed source increment, sizes equal to whole tile/tilemap counts) or a `VMDATAL` copy loop (title screen). The only indirect-long walkers are the metatile blitter `sub_C09FB7`/`sub_C09E83` (`lda [$18]` 16-bit index -> 32-byte metatile, `eor #$4000/$8000` for flips), the OAM builder (`lda [$26],Y` / `[$2A],Y` byte pairs), the animation-script reader (`lda [$A0],Y`, 8-byte records) and the SPC uploader (`lda [$04]`/`[$10]`/`[$42]` word streams). The single `mvn` (`$8263`, `mvn $00,$00`) is a RAM-to-RAM copy in `reset`. There is no loop that shifts a flag byte (`lsr/ror` on a control byte followed by literal/back-reference copies); the `lsr/asl` runs after `lda [..]` are address arithmetic (`asl x5` = index*32, `lsr` = velocity scaling).
- The stale image (`000000-007FFF`) contains 13 `$54`/20 `$44` opcode bytes, but NOTES.md shows it is an older assembly of the same sources (modules shifted), so no decoder is expected there either.
- High-entropy 1 KB windows (>= 7.0 bits/byte) outside the BRR sample area: 8 (058000 7.02, 058C00 7.00, 059000 7.01, 05B400 7.02, 092000 7.02, 092C00 7.01, 093000 7.01, 095400 7.02). The BRR block `023095-035163` averages 7.0-7.3 bits/byte, as expected for 4-bit ADPCM nibbles. Every other region sits at 2-6.6 bits/byte, consistent with raw tiles (row-to-row bit coherence 0.15-0.35 in all tile regions versus 0.5 for BRR/code).
- Conclusion: the ROM is entirely uncompressed. Roughly 27% of it is stale duplicates of other regions plus an alternate sprite-frame format the code never reads, which is what an uncompressed prototype image with fixed-origin bank layouts looks like (see NOTES.md, toolchain evidence).

## 3a. Asset manifest

`config/assets.txt` (built by `tools/gen_assets.py generate baserom/DREAM.sfc config/regions.txt
config/assets.txt`, checked with `tools/gen_assets.py verify config/assets.txt`) subdivides the
region table above into 1926 named, per-asset byte ranges, the same granularity as the
DKC2/DKC3 disassemblies' `Graphics/GFX_Sprite_<Name>.bin` / `Music_<Name>` extraction. Format:

    <start6hex> <end6hex> <kind> <path>    ; note

`start`/`end` are file offsets (end exclusive), `kind` is one of `sprite_frame`,
`sprite_frame_alt`, `sprite_table`, `tileset_4bpp`, `tileset_8bpp`, `tileset_2bpp`, `tilemap`,
`metatiles`, `map`, `palette`, `hdma`, `brr`, `song`, `sfx_bank`, `spc_table`, `anim_script`,
`anim_table`, `entity_table`, `code`, `stale`, `filler`, `unknown`, and `path` is the file under
`data/` that `tools/extract.py` writes the asset's bytes to (gitignored, same as the half-bank
files; nothing under `data/` is ever committed). Coverage is total and non-overlapping: every
byte of `000000-200000` belongs to exactly one asset. Most regions.txt rows map 1:1 to a single
asset; five are subdivided by parsing the ROM's own code-driven structures (or, for the two
alternate-format sprite regions, a header/record-run chain walk) instead of guessing at
sub-boundaries:

- **Sprite frames** (`sprite_frame_table` at `0x040000`, 1558 x 4-byte `{ptr16, bank, y_bias}`
  records): the table itself becomes one `sprite_table` asset
  (`data/sprites/frame_table.bin`); its 1556 non-null entries resolve to 1556 distinct file
  offsets, of which entry 0 (`bank=$C4, ptr=0`) aliases the table's own last two (null) bytes
  and is not real frame data, leaving 1555 offsets inside the nine live-format
  `sprite_frames_*` regions. Each is decoded with the `{n1, n2, off2, n3, off3, nt1, vo2, nt2}`
  8-byte header from section 1b (`total_tiles = nt1 + nt2`) to get its length; the next frame's
  start (or the region end) closes the file, folding in the 2-40 byte undecoded trailer bytes
  documented in 1b. Result: 1555 `data/sprites/frame_NNNN.bin` assets, globally numbered in
  file order, with zero overlaps and gap sizes matching the documented trailer range exactly.
  The two alternate-format regions (`1CC6AA-1F0000`, `1F2E14-1FFEE5`) are not code-referenced
  by any table (there is no table at all), so their boundaries instead come from
  `parse_sprite_frame_alt_region`'s header/record-run chain walk (section 1c) *plus* the
  header's own decoded length: the chain walk finds 82 + 31 = 113 candidate frames, and for
  6 of those 113 the header only accounts for part of the candidate's bytes, with the
  remainder decoding as a further, complete frame of its own (also header-exact); splitting
  those 6 gives 87 + 32 = 119 `data/sprites/frame_alt_NNNN.bin` assets of kind
  `sprite_frame_alt`, keeping the 113 original indices/paths stable and appending the 6 newly
  split frames at the next free indices (113-118). The last 283 bytes of the ROM
  (`1FFEE5-200000`), formerly kept as a separate `sprite_frame_tail.bin` "partial" asset, also
  decode header-exact (279 bytes, a 4-byte trailer) and are simply a 120th complete
  `sprite_frame_alt` frame at that same path. 120 frames total, no truncated or partial one
  among them. Splitting these regions changes the file each byte lands in, so `make regen`
  re-slices the handful of `bank_DC`-`bank_DF.asm` incbins that used to reference the old
  whole-region blobs onto the new per-frame files; `make check` still reassembles a
  byte-identical ROM.
- **Animation scripts** (`anim_script_index` at `0x041858`, 174 words): kept as one `anim_table`
  asset (`data/anim/script_index.bin`); its 174 entries resolve to 96 distinct script offsets,
  one of which (`$1850`, "the empty script" in `docs/handler_tables.md`) aliases the same two
  null frame-table bytes as sprite entry 0 above and is left inside `frame_table.bin` rather
  than double-covered. The remaining 95 offsets partition `anim_scripts`
  (`0x0419B4-0x046588`) into `data/anim/script_NNN.bin` assets (each running to the next
  script's start, since 37 of the 96 scripts have no `$FFFE`/`$FFFF` terminator record and are
  ended by a callback instead, per `docs/handler_tables.md`).
- **BRR samples** (`brr_samples`, `0x023095-0x035163`): walked directly as a chain of
  `{loop_offset u16, length u16, length bytes of BRR blocks}` records (the same fields
  `sub_C1815F` reads) rather than via the pointer table's ambiguous byte order; this lands on
  exactly 51 records that tile the region with no remainder, matching
  `docs/data_formats.md` 1's count. Each record is one `data/brr/sample_NN.bin` asset (kind
  `brr`).
- **Song blocks** (`song_blocks`, `0x02119F-0x022E5C`): the song table at `0x0210B9` (16 x
  `{song_block_ptr24, sample_list_ptr24}`, only the first 8 used) gives the 8 song starts
  directly; each runs to the next song's start (or the region end for song 7), giving
  `data/music/song_00.bin` .. `song_07.bin` (kind `song`; songs 3-7 are the documented empty
  4-byte blocks).

Every other regions.txt row becomes a single asset; its `kind` follows the regions.txt `class`
plus a note-keyword check for the finer distinctions the manifest makes that regions.txt does
not (tile bit depth from `8bpp`/`2bpp` in the note; `maps` rows split into `hdma`, `tilemap`,
`metatiles`, or the `map` fallback for the handful of level-scene tables (wave/parallax/scroll
curves, per-mode parameter tables) that are code-referenced but not literally a tilemap,
metatile set, level map, or HDMA table; `music` rows split into `sfx_bank`, the `filler` sfx
bank 2 placeholder run, and `spc_table` for the rest). `code` covers `main_program.bin`
(`0x008000-0x00C00D`) and `sound_iface.bin` (`0x018000-0x01841A`) under `data/misc/`, plus the
SPC700 `data/spc/ipl_loader.bin` and `data/spc/driver.bin` images.

`tools/emit_asar.py` clips every data run it emits to the named asset covering its start
address (in addition to the existing 32 KB bank-half and instruction/table/label boundaries),
so a run can never straddle two assets: it is emitted as a whole-file `incbin "../data/<path>"`
when the run is exactly one asset, or `incbin "../data/<path>":$lo..$hi` (offsets into that
asset file) when it is a sub-range of one. Runs inside `main_program.bin`/`sound_iface.bin`
(65816 code interleaved with embedded data tables) keep the original half-bank
`incbin "../data/NN.bin":$lo..$hi` form. `tools/asset_sprite.py` and `tools/asset_brr.py`
decode a sprite frame / BRR sample asset to a PPM+ASCII preview / 16-bit PCM WAV respectively,
as a check that the derived boundaries line up with the documented formats.

## 3b. Round-trip codecs

`tools/assetcodec.py` (python3 stdlib only: `zlib` for PNG, `struct`, `json`, `wave`) converts
the raw asset bytes `tools/extract.py` writes under `data/` into human-editable files and back:

    python3 tools/assetcodec.py decode <kind> <asset.bin> <outdir>
    python3 tools/assetcodec.py encode <kind> <editable> <asset.bin>

`decode` writes the editable form(s) into `<outdir>`, named after the input stem, and marks the
*primary* file with `*`; `encode` is handed that primary file (it picks up the `.json` sidecar
next to it by name) and writes the asset bytes back. The requirement on every codec is
byte-exactness: `encode(decode(bytes)) == bytes` for every asset of the kind, with no exceptions
and no "close enough". Bits with no recovered meaning are carried through verbatim (an unused
palette word's bit 15, a sprite frame's trailer, a BRR record's pad bytes) rather than dropped.

`tools/roundtrip_check.py` enforces that over the whole manifest:

    python3 tools/roundtrip_check.py               # per-kind pass/total
    python3 tools/roundtrip_check.py --update      # ... and rewrite config/roundtrip.txt
    python3 tools/roundtrip_check.py --kind brr -v # one kind, list every failure

It decodes each asset of a codec-carrying kind into `build/assets/<same subpath as data/>`,
re-encodes, and compares with `data/`. `--update` rewrites `config/roundtrip.txt` with exactly
the kinds at **100%** pass; `tools/progress.py` reads that file to decide which data bytes count
as matched. Nothing here is committed: `build/` is gitignored like `data/` (docs/LEGAL.md 1).

| kind | primary editable form | sidecar | notes |
|---|---|---|---|
| `palette` | `.json`: rows of 16 `[r,g,b]` triples, 5 bits each | `.png` swatch (viewing only, not read back) | word indices whose bit 15 is set are listed in `bit15_set`; an odd trailing byte goes to `trailer` |
| `tileset_2bpp` / `tileset_4bpp` / `tileset_8bpp` | `.png`, 8-bit indexed, 16 tiles per row | `.json` when the tile count is not a multiple of 16 or bytes remain | pixel bytes hold the raw palette index (0-3 / 0-15 / 0-255); the PLTE is only a grey viewing ramp. All five PNG row filters are handled on read, so an editor re-save is fine |
| `tilemap` | `.json` list of `{tile, pal, pri, h, v}` | - | `tile\|pal<<10\|pri<<13\|h<<14\|v<<15` covers all 16 bits, so nothing is lost |
| `metatiles` | `.json` list of 4x4 word groups (32 bytes each) | - | `tail_words` holds a partial trailing group |
| `map` | `.json` column-major `columns x rows` grid of `{index, h, v}` (bits 14/15) | - | dimensions from section 1, keyed by asset name. The `map` rows that are really code-referenced curves (camera/parallax/scroll, per-mode parameter pairs) decode as an `s16_table` instead |
| `hdma` | `.json` entries `{lines, pointer}` + terminator | - | the `wave_table_sine` row is an HDMA *source*, not a table, and decodes as an `s16_table` |
| `anim_script` | `.json` records `{callback, mode, duration, frame}` | - | `mode_name` / `duration_name` (`loop_to_frame_0`, `switch_to_anim`) are derived and not read back |
| `anim_table` | `.json` list of script offsets | - | |
| `sprite_table` | `.json` records `{ptr, bank, y_bias}` | - | frame id = 2 x record index |
| `sprite_frame` | `.png` of the assembled frame | `.json` header + OAM list + tile rects | see below |
| `sprite_frame_alt` | `.png` of the assembled frame | `.json` header + OAM list (with decoded attr bits) + tile rects | see below |
| `brr` | `.wav`, 16-bit mono, 32000 Hz | `.json` `{loop_offset, length, per-block shift/filter/loop/end, pad, trailing}` | see below |
| `song` / `sfx_bank` / `spc_table` | `.json` | - | see below |

### Sprite frames

The live-format decoder follows `sub_C0A538` / `oam_emit_frame_2row` (`$C0A772`) and the shared
container section 1b decodes: `n1` 16x16 sprites (four tiles each, in the PPU's own name-table
arrangement, the `i`'th at grid slot `2*(i%8) + 32*(i/8)`), then `n2` 8x8 sprites starting at
tile `off2`, then `n3` more starting at `off3`. Every tile the header declares (`nt1+nt2` of
them) is used by exactly one sprite and none is left over. There is no unclaimed-tile spill,
unlike the reconstruction this replaces.

The PNG is that assembly: sprites drawn at their OAM `(x, y)`, canvas cropped to their bounding
box (`canvas.origin_x`/`origin_y` record the crop). Sprite positions are not 8-pixel aligned, and
distinct sprites' own boxes can still land on overlapping screen pixels (ordinary in this art,
not a decoding error): ownership of the canvas is tracked per *pixel*, ascending tile index
first, and a tile that loses a pixel to an earlier one keeps its own copy in a small overflow
strip below the canvas, 16 tiles per row, instead of being drawn where it would overwrite
another tile's only copy. The `tiles` array in the sidecar gives the authoritative 8x8 source
rect of every VRAM tile (in the main canvas or the overflow strip), and `encode` reads tiles from
those rects, so the round trip is exact regardless of how the frame assembles. Across the 1550
live frames this decodes (5 more fall short of their own header's declared length and are kept
raw, section 1b), 86% of 35010 VRAM tiles sit in the main canvas with no overlap and 266 frames
need no overflow strip at all.

Caveats worth knowing: frame ids `< 4` are emitted by `oam_emit_frame_1row` with a 5-byte header,
so for those one or two frames the OAM list in the sidecar is shifted (the bytes still round-trip,
since the extra header bytes parse as OAM records).

### Alternate sprite frames

`sprite_frame_alt` (section 1c) covers the two alternate-format regions and the ROM's trailing
280-odd bytes, one frame per asset, 120 in total. `decode_sprite_frame_alt` decodes the header
exactly (the same fields as the live format, `off2`/`n3`/`off3` included, plus the 3-byte
`{x, y, attr}` records the alternate format always uses) rather than re-deriving the record
count from the `attr` band, giving named header fields, `n1+n2+n3` OAM records (each decoded
into `vflip`/`hflip`/`priority`/`palette`/`name_bit`, which partition all 8 bits of `attr`
losslessly, so `encode` reconstructs it exactly), and the `nt1+nt2` tiles the header declares.

Placement uses the same rule as the live format: `n1` 16x16 sprites in the name-table
arrangement, then `n2` and `n3` 8x8 sprites at `off2`/`off3`, every one of the 5960 tiles across
all 120 frames claimed by exactly one sprite. As with the live format, overlapping sprite boxes
on screen (about 25% of tiles here) go to the same per-pixel-claim overflow strip rather than
being drawn over; only 1 of the 120 frames needs none of it. None of that affects round-trip
exactness (the sidecar's `tiles` array is authoritative regardless of how the canvas assembles,
exactly as for `sprite_frame`), and the assembled PNG is now a real reconstruction of the frame,
not a layout guess: "these are more 4bpp sprite tiles, laid out somewhere in this frame" is no
longer the caveat it was.

The 283-byte frame at `1FFEE5-200000` decodes the same way as any other (5 records, 8 tiles, a
4-byte trailer); nothing in the manifest is `format: "raw"` for this kind, and none of the 120
is partial or truncated.

### BRR samples

The `.wav` holds the PCM the standard SNES BRR decode produces from the whole record (all blocks,
end flag included), and the sidecar keeps the per-block `shift`/`filter`/`loop`/`end` plus the
`{loop_offset, length}` header, any pad bytes at the tail of the declared length, and any bytes
past it. `encode` re-quantises the PCM with the stored filter and shift: because the PCM came from
those exact nibbles, the residual is exactly reproducible whenever the mapping nibble -> sample is
injective. It is not injective when `shift == 0` (the `>> 1` drops the low bit), when
`shift >= 13`, or when a sample was clamped; the decoder detects those blocks by re-running the
quantiser and falls back to storing that block's nibbles in `raw_nibbles`. Over the 51 samples,
**6 of 8190 blocks** (0.07%) need that fallback; the other 8184 come back from the PCM alone.

### SPC sequences

`song` and the `sfx_bank1` block decode as `{dest, word_count, segments}`, where the segments
cover the body **exactly once, in order**: a `song_header` (8 channel pointers, tempo, tempo2) or
`sfx_index` (count + pointers), then `events` runs and `raw` runs. Streams are walked linearly
from each channel/sfx pointer and from every jump/call target, using the opcode table in
`spc/spc_map.txt` with the event lengths read off the `mov $00,#$nn` (`tmp0` = total event length)
in each handler of `spc/driver.asm`; note events are 1 byte when a note length is latched
(seq cmd `$06`) and 2 otherwise (3 with the stale gate mode). Each event re-encodes to its own
bytes: notes as `{note, operands}`, commands as `{cmd, code, operands}`, control transfers as
`{cmd, code, target}` (plus `count` for `$04`), so anything the walk does not reach stays a
`raw` hex run and exactness never depends on the parse being semantically right. In practice the
three real songs parse to 1829 / 1116 / 207 events with 32 / 31 / 27 bytes left raw, and
`sfx_bank1`'s 21 sequences parse to 222 events with none. The small pointer/count assets
(`sample_pointer_table`, `song_table`, `sfx_bank2_ptrs`, `sample_pointer_tail`,
`sfx_bank2_blocks`, `sample_lists`) decode as `pointer_table_24`, `u16_table` or `sample_lists`
JSON, chosen by asset name.

### Kinds without a codec

`code` (`main_program.bin`, `sound_iface.bin`, the SPC700 loader/driver images) belongs to `src/`
and `spc/`, not here. `stale`, `filler` and `unknown` have no recovered structure to decode; they
already count as matched in `tools/progress.py` for exactly that reason. `entity_table` is a kind
`config/assets.txt` can emit but currently does not use.

### Current status

`python3 tools/roundtrip_check.py --update` passes **1891 / 1891** assets across all **17**
codec-carrying kinds, so all 17 are in `config/roundtrip.txt`: `anim_script`, `anim_table`, `brr`,
`hdma`, `map`, `metatiles`, `palette`, `sfx_bank`, `song`, `spc_table`, `sprite_frame`,
`sprite_frame_alt`, `sprite_table`, `tilemap`, `tileset_2bpp`, `tileset_4bpp`, `tileset_8bpp`.
That is 1833792 bytes, 87.4% of the 2 MiB image (unchanged from before: splitting the
alternate-format regions into 6 more frames does not change how many bytes those two regions
cover), now reachable as editable files (3637 files, 10411530 bytes under `build/assets/`;
fewer bytes than before despite more files, since the corrected container needs a far smaller
overflow area than the old spill strip did). 5 of the 1555 live sprite-frame assets decode to
`format: "raw"` (section 1b): once the header is read correctly (including `n3`/`off3`, not
just `n1`/`n2`), it claims more OAM-record bytes than the asset's own boundary holds. That is
a real inconsistency in these 5 frames' data, not a decoder gap, and one the old two-group
header reading was not looking closely enough to notice. Every other codec-carrying kind, and every
other asset within `sprite_frame`, has real recovered structure with no `raw` escape hatch.

## 4. Summary by content type

| type | bytes | % of ROM |
|---|---:|---:|
| sprite frames, live format (4bpp tiles + OAM records) | 1176456 | 56.1% |
| sprite frames, alternate format (unreferenced) | 199490 | 9.5% |
| 4bpp tiles (BG/OBJ, code-referenced) | 179744 | 8.6% |
| stale duplicate copies of other regions | 172063 | 8.2% |
| BRR samples | 73934 | 3.5% |
| unknown | 62794 | 3.0% |
| 8bpp tiles (title) | 40128 | 1.9% |
| stale build image (old code, old anim table, old tiles/palette) | 32768 | 1.6% |
| filler (06 37 00 37, 0x55) | 31200 | 1.5% |
| tilemaps | 30712 | 1.5% |
| metatile definitions | 27104 | 1.3% |
| animation scripts + frame table | 25992 | 1.2% |
| 65816 code (live) | 17447 | 0.8% |
| level maps | 9088 | 0.4% |
| SPC sequences (songs, sfx) | 7906 | 0.4% |
| SPC700 loader/driver images | 3514 | 0.2% |
| palettes / colour tables | 2779 | 0.1% |
| 2bpp tiles + font | 2048 | 0.1% |
| other tables (HDMA, curves, vectors) | 1843 | 0.1% |
| SPC pointer tables / sample lists | 382 | 0.0% |
| ASCII text | 43 | 0.0% |

Aggregated: tiles (all bpp, incl. sprite frames of both formats) 76.2%, tilemaps+metatiles+maps 3.2%, palettes 0.1%, BRR 3.5%, SPC sequences+tables 0.4%, animation scripts/frame table 1.2%, code (65816+SPC) 1.0%, stale image + duplicates 9.8%, filler 1.5%, unknown 3.0%.
