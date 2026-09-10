; bank $C8  (file $080000)

org $C80000
    incbin "../data/gfx/bg1_tiles_mode3.bin"                        ; 27328 bytes (whole asset)
    incbin "../data/gfx/bg1_tiles_mode1.bin":$0000..$1540        ; 5440 bytes
    incbin "../data/gfx/bg1_tiles_mode1.bin":$1540..$5EC0        ; 18816 bytes
    incbin "../data/gfx/bg2_tiles_mode1.bin"                        ; 13856 bytes (whole asset)
    incbin "../data/sprites/frame_0002.bin"                        ; 76 bytes (whole asset)
    incbin "../data/unknown/unknown_gap_8ffec.bin"                        ; 20 bytes (whole asset)
