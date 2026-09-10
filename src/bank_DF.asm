; bank $DF  (file $1F0000)

org $DF0000
    incbin "../data/3E.bin":$0000..$8000      ; 32768 bytes
    incbin "../data/3F.bin":$0000..$8000      ; 32768 bytes
