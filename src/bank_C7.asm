; bank $C7  (file $070000)

org $C70000
    incbin "../data/sprites/frame_0000.bin"                        ; 832 bytes (whole asset)
    incbin "../data/unknown/unknown_gap_70340.bin"                        ; 2 bytes (whole asset)
    incbin "../data/gfx/bg1_tiles_mode2.bin"                        ; 28608 bytes (whole asset)
    incbin "../data/gfx/mixed_tiles_c7_mid.bin":$0000..$0CFE        ; 3326 bytes
    incbin "../data/gfx/mixed_tiles_c7_mid.bin":$0CFE..$6C80        ; 24450 bytes
    incbin "../data/gfx/bg2_tiles_mode2.bin"                        ; 6624 bytes (whole asset)
    incbin "../data/sprites/frame_0001.bin"                        ; 1686 bytes (whole asset)
    incbin "../data/unknown/unknown_gap_7fff8.bin"                        ; 8 bytes (whole asset)
