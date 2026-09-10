; bank $D0  (file $100000)

org $D00000
    incbin "../data/20.bin":$0000..$8000      ; 32768 bytes
    incbin "../data/21.bin":$0000..$8000      ; 32768 bytes
