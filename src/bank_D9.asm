; bank $D9  (file $190000)

org $D90000
    incbin "../data/32.bin":$0000..$8000      ; 32768 bytes
    incbin "../data/33.bin":$0000..$8000      ; 32768 bytes
