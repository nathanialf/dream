; bank $DC  (file $1C0000)

org $DC0000
    incbin "../data/38.bin":$0000..$8000      ; 32768 bytes
    incbin "../data/39.bin":$0000..$8000      ; 32768 bytes
