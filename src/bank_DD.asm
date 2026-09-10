; bank $DD  (file $1D0000)

org $DD0000
    incbin "../data/sprites/sprite_frames_alt_dc.bin":$3956..$B956        ; 32768 bytes
    incbin "../data/sprites/sprite_frames_alt_dc.bin":$B956..$13956        ; 32768 bytes
