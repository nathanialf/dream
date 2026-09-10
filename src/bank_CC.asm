; bank $CC  (file $0C0000)

org $CC0000
    incbin "../data/18.bin":$0000..$8000      ; 32768 bytes
    incbin "../data/19.bin":$0000..$8000      ; 32768 bytes
