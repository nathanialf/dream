; bank $D2  (file $120000)

org $D20000
    incbin "../data/24.bin":$0000..$8000      ; 32768 bytes
    incbin "../data/25.bin":$0000..$8000      ; 32768 bytes
