; bank $DA  (file $1A0000)

org $DA0000
    incbin "../data/34.bin":$0000..$8000      ; 32768 bytes
    incbin "../data/35.bin":$0000..$8000      ; 32768 bytes
