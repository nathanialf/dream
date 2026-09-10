; bank $C2  (file $020000)

org $C20000

spc_loader_image:
    incbin "../data/04.bin":$0000..$0088      ; 136 bytes

spc_driver_blocks:
    incbin "../data/04.bin":$0088..$0DB9      ; 3377 bytes

data_C20DB9:
    incbin "../data/04.bin":$0DB9..$0DBB      ; 2 bytes

data_C20DBB:
    incbin "../data/04.bin":$0DBB..$10B9      ; 766 bytes

data_C210B9:
    incbin "../data/04.bin":$10B9..$10BB      ; 2 bytes

data_C210BB:
    incbin "../data/04.bin":$10BB..$10BC      ; 1 bytes

data_C210BC:
    incbin "../data/04.bin":$10BC..$10BE      ; 2 bytes

data_C210BE:
    incbin "../data/04.bin":$10BE..$10EE      ; 48 bytes

data_C210EE:
    incbin "../data/04.bin":$10EE..$10F0      ; 2 bytes

data_C210F0:
    incbin "../data/04.bin":$10F0..$8000      ; 28432 bytes
    incbin "../data/05.bin":$0000..$8000      ; 32768 bytes
