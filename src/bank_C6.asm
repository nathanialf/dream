; bank $C6  (file $060000)

org $C60000
    incbin "../data/filler/tool_export_header.bin"                        ; 43 bytes (whole asset)

data_C6002B:
    incbin "../data/gfx/title_tiles_8bpp.bin":$0000..$7FD5        ; 32725 bytes
    incbin "../data/gfx/title_tiles_8bpp.bin":$7FD5..$9CC0        ; 7403 bytes

data_C69CEB:
    incbin "../data/maps/title_tilemap_6500.bin"                        ; 832 bytes (whole asset)

data_C6A02B:
    incbin "../data/maps/title_tilemap_6960.bin"                        ; 448 bytes (whole asset)

data_C6A1EB:
    incbin "../data/maps/title_tilemap_6da0.bin"                        ; 256 bytes (whole asset)

data_C6A2EB:
    incbin "../data/maps/title_tilemap_61c0.bin"                        ; 128 bytes (whole asset)

data_C6A36B:
    incbin "../data/palettes/title_palette_256.bin":$0000..$0001        ; 1 bytes

data_C6A36C:
    incbin "../data/palettes/title_palette_256.bin":$0001..$0200        ; 511 bytes
    incbin "../data/maps/tilemap_unref_6a56b.bin"                        ; 246 bytes (whole asset)
    incbin "../data/stale/stale_dup_sprite_frames_ca.bin"                        ; 21957 bytes (whole asset)
    incbin "../data/unknown/unknown_tail_6fc26.bin"                        ; 986 bytes (whole asset)
