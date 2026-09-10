; bank $C4  (file $040000)

org $C40000

data_C40000:
    incbin "../data/08.bin":$0000..$0002      ; 2 bytes

data_C40002:
    incbin "../data/08.bin":$0002..$1858      ; 6230 bytes

anim_script_table:
    incbin "../data/08.bin":$1858..$6588      ; 19760 bytes

data_C46588:
    incbin "../data/08.bin":$6588..$6788      ; 512 bytes

data_C46788:
    incbin "../data/08.bin":$6788..$6808      ; 128 bytes

data_C46808:
    incbin "../data/08.bin":$6808..$6888      ; 128 bytes

data_C46888:
    incbin "../data/08.bin":$6888..$6908      ; 128 bytes

data_C46908:
    incbin "../data/08.bin":$6908..$6988      ; 128 bytes

data_C46988:
    incbin "../data/08.bin":$6988..$6A88      ; 256 bytes

data_C46A88:
    incbin "../data/08.bin":$6A88..$6B88      ; 256 bytes

data_C46B88:
    incbin "../data/08.bin":$6B88..$8000      ; 5240 bytes
    incbin "../data/09.bin":$0000..$8000      ; 32768 bytes
