; bank $C6  (file $060000)

org $C60000
    incbin "../data/0C.bin":$0000..$002B      ; 43 bytes

data_C6002B:
    incbin "../data/0C.bin":$002B..$8000      ; 32725 bytes
    incbin "../data/0D.bin":$0000..$1CEB      ; 7403 bytes

data_C69CEB:
    incbin "../data/0D.bin":$1CEB..$202B      ; 832 bytes

data_C6A02B:
    incbin "../data/0D.bin":$202B..$21EB      ; 448 bytes

data_C6A1EB:
    incbin "../data/0D.bin":$21EB..$22EB      ; 256 bytes

data_C6A2EB:
    incbin "../data/0D.bin":$22EB..$236B      ; 128 bytes

data_C6A36B:
    incbin "../data/0D.bin":$236B..$236C      ; 1 bytes

data_C6A36C:
    incbin "../data/0D.bin":$236C..$8000      ; 23700 bytes
