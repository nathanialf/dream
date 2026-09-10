; bank $D1  (file $110000)

org $D10000
    incbin "../data/22.bin":$0000..$8000      ; 32768 bytes
    incbin "../data/23.bin":$0000..$8000      ; 32768 bytes
