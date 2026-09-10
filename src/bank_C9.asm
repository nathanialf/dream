; bank $C9  (file $090000)

org $C90000
    incbin "../data/gfx/bg1_tiles_mode0.bin"                        ; 23232 bytes (whole asset)
    incbin "../data/gfx/bg2_tiles_mode3.bin":$0000..$2540        ; 9536 bytes
    incbin "../data/gfx/bg2_tiles_mode3.bin":$2540..$3000        ; 2752 bytes
    incbin "../data/gfx/bg2_tiles_mode0.bin"                        ; 10912 bytes (whole asset)
    incbin "../data/maps/metatiles_mode2.bin"                        ; 10112 bytes (whole asset)
    incbin "../data/maps/metatiles_mode0.bin"                        ; 8352 bytes (whole asset)
    incbin "../data/maps/level_map_mode1.bin"                        ; 640 bytes (whole asset)
