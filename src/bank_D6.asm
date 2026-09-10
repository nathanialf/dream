; bank $D6  (file $160000)

org $D60000
    incbin "../data/2C.bin":$0000..$8000      ; 32768 bytes
    incbin "../data/2D.bin":$0000..$8000      ; 32768 bytes
