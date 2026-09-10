; bank $C5  (file $050000)

org $C50000
    incbin "../data/0A.bin":$0000..$0200      ; 512 bytes

data_C50200:
    incbin "../data/0A.bin":$0200..$0260      ; 96 bytes

data_C50260:
    incbin "../data/0A.bin":$0260..$8000      ; 32160 bytes
    incbin "../data/0B.bin":$0000..$8000      ; 32768 bytes
