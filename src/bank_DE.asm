; bank $DE  (file $1E0000)

org $DE0000
    incbin "../data/sprites/sprite_frames_alt_dc.bin":$13956..$1B956        ; 32768 bytes
    incbin "../data/sprites/sprite_frames_alt_dc.bin":$1B956..$23956        ; 32768 bytes
