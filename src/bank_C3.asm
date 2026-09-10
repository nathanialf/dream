; bank $C3  (file $030000)

org $C30000
    incbin "../data/06.bin":$0000..$8000      ; 32768 bytes
    incbin "../data/07.bin":$0000..$8000      ; 32768 bytes
