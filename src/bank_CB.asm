; bank $CB  (file $0B0000)

org $CB0000
    incbin "../data/16.bin":$0000..$8000      ; 32768 bytes
    incbin "../data/17.bin":$0000..$8000      ; 32768 bytes
