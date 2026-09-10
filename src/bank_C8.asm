; bank $C8  (file $080000)

org $C80000
    incbin "../data/10.bin":$0000..$8000      ; 32768 bytes
    incbin "../data/11.bin":$0000..$8000      ; 32768 bytes
