; bank $CD  (file $0D0000)

org $CD0000
    incbin "../data/1A.bin":$0000..$8000      ; 32768 bytes
    incbin "../data/1B.bin":$0000..$8000      ; 32768 bytes
