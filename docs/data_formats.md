# Project Dream (DREAM.sfc) data-format map

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
| 01EAC0-01EFC0 | 1280 | unknown (tile-like, 1280 bytes) | low | entropy 4.9, no duplicate found | none |
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
| 06FC26-070000 | 986 | unreferenced tail | low | entropy 5.5 | none |
| 070000-070340 | 832 | sprite frames, live format: 1 frames {8-byte header: n1, n2, tile-offset2, ?, ?, ntiles1, vram-offset2, ntiles2/flags; 2-byte OAM records (x,y); 4bpp tiles = (ntiles1+ntiles2)*32 bytes}; 2-40 byte trailers between frames | high | frame table 040000 entries point here; header/record/tile sizes chain contiguously | sub_C0A538 / sub_C0AE7E DMA |
| 070340-070342 | 2 | unlabelled gap | low | entropy 1.00 | none |
| 070342-077302 | 28608 | 4bpp BG1 tiles, 894 tiles -> VRAM $2000 (game_mode 2) | high | sub_C0A46A at $8813 ($C7:0342, $6FC0) | $8813-$8822 |
| 077302-07DF82 | 27776 | unreferenced: mixed 4bpp tiles and tilemap/metatile word chunks (79402-7A302, 7A602-7A802, 7B602-7B802, 7D102-7D402, 7D702-7D902); duplicated at 047463 (bank C4) | medium | fine classifier; duplicate at delta -0x30160 | none |
| 07DF82-07F962 | 6624 | 4bpp BG2 tiles, 207 tiles -> VRAM $6000 (game_mode 2) | high | sub_C0A46A at $883D ($C7:DF82, $19E0) | $883D-$884C |
| 07F962-07FFF8 | 1686 | sprite frames, live format: 1 frames {8-byte header: n1, n2, tile-offset2, ?, ?, ntiles1, vram-offset2, ntiles2/flags; 2-byte OAM records (x,y); 4bpp tiles = (ntiles1+ntiles2)*32 bytes}; 2-40 byte trailers between frames | high | frame table 040000 entries point here; header/record/tile sizes chain contiguously | sub_C0A538 / sub_C0AE7E DMA |
| 07FFF8-080000 | 8 | unlabelled gap | low | entropy 2.41 | none |
| 080000-086AC0 | 27328 | 4bpp BG1 tiles, 854 tiles -> VRAM $2000 (game_mode 3); metatile max tile index 853 | high | sub_C0A46A at $8941 ($C8:0000, $6AC0) | $8941-$8950 |
| 086AC0-08C980 | 24256 | 4bpp BG1 tiles -> VRAM $2000 (game_mode 1); DMA size $6000 over-reads 0x140 bytes into the next set | high | sub_C0A46A at $855B ($C8:6AC0, $6000); metatile max tile index 622 | $855B-$856A |
| 08C980-08FFA0 | 13856 | 4bpp BG2 tiles, 433 tiles -> VRAM $5000 (game_mode 1) | high | sub_C0A46A at $856D ($C8:C980, $3620) | $856D-$857C |
| 08FFA0-08FFEC | 76 | sprite frames, live format: 1 frames {8-byte header: n1, n2, tile-offset2, ?, ?, ntiles1, vram-offset2, ntiles2/flags; 2-byte OAM records (x,y); 4bpp tiles = (ntiles1+ntiles2)*32 bytes}; 2-40 byte trailers between frames | high | frame table 040000 entries point here; header/record/tile sizes chain contiguously | sub_C0A538 / sub_C0AE7E DMA |
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
| 0A6360-0AE384 | 32804 | sprite frames, live format: 15 frames {8-byte header: n1, n2, tile-offset2, ?, ?, ntiles1, vram-offset2, ntiles2/flags; 2-byte OAM records (x,y); 4bpp tiles = (ntiles1+ntiles2)*32 bytes}; 2-40 byte trailers between frames | high | frame table 040000 entries point here; header/record/tile sizes chain contiguously | sub_C0A538 / sub_C0AE7E DMA |
| 0AE384-0AE38E | 10 | 10-byte gap | low |  | none |
| 0AE38E-0AEB8E | 2048 | 32x32 tilemap -> VRAM $1C00 (game_mode 0 BG3) | high | sub_C0A46A at $8317 ($CA:E38E, $800) | $8317-$8326 |
| 0AEB8E-0AF38E | 2048 | 32x32 tilemap -> VRAM $7000 (game_mode 0) | high | sub_C0A46A at $8365; B208 record | $8365-$8374 |
| 0AF38E-0AFB8E | 2048 | 32x32 tilemap -> VRAM $6800 (game_mode 0) | high | sub_C0A46A at $834D; B208 record | $834D-$835C |
| 0AFB8E-0AFFEC | 1118 | sprite frames, live format: 1 frames {8-byte header: n1, n2, tile-offset2, ?, ?, ntiles1, vram-offset2, ntiles2/flags; 2-byte OAM records (x,y); 4bpp tiles = (ntiles1+ntiles2)*32 bytes}; 2-40 byte trailers between frames | high | frame table 040000 entries point here; header/record/tile sizes chain contiguously | sub_C0A538 / sub_C0AE7E DMA |
| 0AFFEC-0B0000 | 20 | unlabelled gap | low | entropy 2.70 | none |
| 0B0000-0B4000 | 16384 | eight 32x32 tilemaps (2 KB each): B0000 -> $6C40 (mode 1), B0800 -> $7400 (mode 2), B1000 -> $5800 (mode 2), B1800 -> $7000 and B2000 -> $6800 (streamed, B208), B2800 -> $5C00, B3000 -> $1C00, B3800 -> $5800 (mode 3) | high | sub_C0A46A sites $8585,$8825,$884F,$892F,$8953,$8965; B208 records | listed sites |
| 0B4000-0BA4CA | 25802 | sprite frames, live format: 13 frames {8-byte header: n1, n2, tile-offset2, ?, ?, ntiles1, vram-offset2, ntiles2/flags; 2-byte OAM records (x,y); 4bpp tiles = (ntiles1+ntiles2)*32 bytes}; 2-40 byte trailers between frames | high | frame table 040000 entries point here; header/record/tile sizes chain contiguously | sub_C0A538 / sub_C0AE7E DMA |
| 0BA4CA-0BAC4C | 1922 | tilemap-like words ($1800,$1801,$1802,... increasing tile index, palette 6), 961 words, unreferenced, sits between two sprite frames | medium | word decode; no frame-table entry points here | none |
| 0BAC4C-0CA402 | 63414 | sprite frames, live format: 35 frames {8-byte header: n1, n2, tile-offset2, ?, ?, ntiles1, vram-offset2, ntiles2/flags; 2-byte OAM records (x,y); 4bpp tiles = (ntiles1+ntiles2)*32 bytes}; 2-40 byte trailers between frames | high | frame table 040000 entries point here; header/record/tile sizes chain contiguously | sub_C0A538 / sub_C0AE7E DMA |
| 0CA402-0CAB02 | 1792 | tilemap, 896 words (28 rows) -> VRAM $7400/$7420 (game_mode 1) | high | sub_C0A46A at $85BB/$85CD ($CC:A402, $700) | $85BB-$85DC |
| 0CAB02-0CB202 | 1792 | tilemap, 896 words -> VRAM $7000/$7020 (game_mode 1) | high | sub_C0A46A at $8597/$85A9 ($CC:AB02, $700) | $8597-$85B8 |
| 0CB202-0E8714 | 120082 | sprite frames, live format: 73 frames {8-byte header: n1, n2, tile-offset2, ?, ?, ntiles1, vram-offset2, ntiles2/flags; 2-byte OAM records (x,y); 4bpp tiles = (ntiles1+ntiles2)*32 bytes}; 2-40 byte trailers between frames | high | frame table 040000 entries point here; header/record/tile sizes chain contiguously | sub_C0A538 / sub_C0AE7E DMA |
| 0E8714-0E8D14 | 1536 | level map, game_mode 3: 32 columns x 24 rows (48 bytes/column) | high | $7A/$7C = $8714/$CE at $88EF, $82=-1, $86=$2FF | sub_C09FB7 |
| 0E8D14-1CC066 | 930642 | sprite frames, live format: 1415 frames {8-byte header: n1, n2, tile-offset2, ?, ?, ntiles1, vram-offset2, ntiles2/flags; 2-byte OAM records (x,y); 4bpp tiles = (ntiles1+ntiles2)*32 bytes}; 2-40 byte trailers between frames | high | frame table 040000 entries point here; header/record/tile sizes chain contiguously | sub_C0A538 / sub_C0AE7E DMA |
| 1CC066-1CC6AA | 1604 | tile-like data between the last table-referenced frame and the alternate-format frames | low | no header parses here | none |
| 1CC6AA-1F0000 | 145750 | sprite frames in an ALTERNATE format not read by the live code: 8-byte header {8A-8C, xx, 2*n?, 00, xx, xx, 30, 0N} + n x 3-byte OAM records {x, y, attr $1C-$22} + 4bpp tiles (~52 tiles/frame, 1729-byte spans); 82 frames, many identical headers; no duplicates elsewhere | high | header scan (68 strict / 113 relaxed hits at regular 0x6C1/0x6A7/0x684 spacing); render below | none (unreferenced) |
| 1F0000-1F10C0 | 4288 | stale duplicate of 0A37A0-0A4860 (all 134 game_mode 3 metatiles) | high | 4288-byte exact match at delta -0x14C860 | none |
| 1F10C0-1F1100 | 64 | 64-byte gap | low |  | none |
| 1F1100-1F2780 | 5760 | stale duplicate of 09C660-09DCE0 (game_mode 2 metatiles 136-315) | high | 5760-byte exact match at delta -0x154AA0 | none |
| 1F2780-1F2E14 | 1684 | unreferenced (tile-like, entropy 5.2) | low |  | none |
| 1F2E14-1FFEE5 | 53457 | sprite frames, alternate format (as 1CC6AA): 31 frames at 0x684 spacing | high | header scan | none |
| 1FFEE5-200000 | 283 | tail (partial last frame / padding) | low |  | none |

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

`sub_C0A538`: frame id (`$07C8,X`, even) indexes `040000` -> `{ptr16, bank, y-bias}`. Frame at `bank:ptr`: header `n1, n2, tileoff2, ?, ?, ntiles1, vramoff2, ntiles2|flags` (8 bytes; frame ids < 4 use a 5-byte header), then `n1+n2` 2-byte `{x, y}` OAM records (tile numbers are sequential: +2 per 16x16 sprite, +$10 at each 16-tile row; size bits from `00A6D3`/`00A6D7`), then `(ntiles1 + ntiles2) * 32` bytes of 4bpp tiles which `sub_C0AE7E` DMAs to VRAM `($0788,X & $1FF) << 4`. 1556 frames parse; 970 abut exactly and the rest are separated by 2-40 byte trailers (e.g. `03 01 01 03 0E 0F 15 1F ...`, look like 8x1-bit masks) that no traced code reads. Frames fill the tails of banks `$C7`, `$C8` and all of `$CA:6360`-`$DC:C066` around the level data.

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

## 4. Summary by content type

| type | bytes | % of ROM |
|---|---:|---:|
| sprite frames, live format (4bpp tiles + OAM records) | 1176456 | 56.1% |
| sprite frames, alternate format (unreferenced) | 199207 | 9.5% |
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
