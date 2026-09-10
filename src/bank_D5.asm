; bank $D5  (file $150000)

org $D50000
    incbin "../data/2A.bin":$0000..$8000      ; 32768 bytes
    incbin "../data/2B.bin":$0000..$8000      ; 32768 bytes
