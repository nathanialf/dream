; bank $D4  (file $140000)

org $D40000
    incbin "../data/28.bin":$0000..$8000      ; 32768 bytes
    incbin "../data/29.bin":$0000..$8000      ; 32768 bytes
