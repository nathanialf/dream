; bank $D8  (file $180000)

org $D80000
    incbin "../data/30.bin":$0000..$8000      ; 32768 bytes
    incbin "../data/31.bin":$0000..$8000      ; 32768 bytes
