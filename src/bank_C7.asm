; bank $C7  (file $070000)

org $C70000
    incbin "../data/0E.bin":$0000..$8000      ; 32768 bytes
    incbin "../data/0F.bin":$0000..$8000      ; 32768 bytes
