; bank $DD  (file $1D0000)

org $DD0000
    incbin "../data/3A.bin":$0000..$8000      ; 32768 bytes
    incbin "../data/3B.bin":$0000..$8000      ; 32768 bytes
