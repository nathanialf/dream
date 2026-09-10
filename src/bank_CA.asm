; bank $CA  (file $0A0000)

org $CA0000
    incbin "../data/14.bin":$0000..$8000      ; 32768 bytes
    incbin "../data/15.bin":$0000..$8000      ; 32768 bytes
