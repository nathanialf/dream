; bank $DF  (file $1F0000)

org $DF0000
    incbin "../data/stale/stale_dup_metatiles_mode3.bin"                        ; 4288 bytes (whole asset)
    incbin "../data/unknown/unknown_gap_1f10c0.bin"                        ; 64 bytes (whole asset)
    incbin "../data/stale/stale_dup_metatiles_mode2b.bin"                        ; 5760 bytes (whole asset)
    incbin "../data/gfx/tiles_unref_1f2780.bin"                        ; 1684 bytes (whole asset)
    incbin "../data/sprites/sprite_frames_alt_ef.bin":$0000..$51EC        ; 20972 bytes
    incbin "../data/sprites/sprite_frames_alt_ef.bin":$51EC..$D0D1        ; 32485 bytes
    incbin "../data/sprites/sprite_frame_tail.bin"                        ; 283 bytes (whole asset)
