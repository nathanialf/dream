; bank $D7  (file $170000)

org $D70000
    incbin "../data/2E.bin":$0000..$8000      ; 32768 bytes
    incbin "../data/2F.bin":$0000..$8000      ; 32768 bytes
