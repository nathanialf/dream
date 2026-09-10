; bank $CF  (file $0F0000)

org $CF0000
    incbin "../data/1E.bin":$0000..$8000      ; 32768 bytes
    incbin "../data/1F.bin":$0000..$8000      ; 32768 bytes
