; bank $D3  (file $130000)

org $D30000
    incbin "../data/26.bin":$0000..$8000      ; 32768 bytes
    incbin "../data/27.bin":$0000..$8000      ; 32768 bytes
