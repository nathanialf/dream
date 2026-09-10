; bank $CE  (file $0E0000)

org $CE0000
    incbin "../data/1C.bin":$0000..$8000      ; 32768 bytes
    incbin "../data/1D.bin":$0000..$8000      ; 32768 bytes
