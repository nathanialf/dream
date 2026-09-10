; bank $DE  (file $1E0000)

org $DE0000
    incbin "../data/3C.bin":$0000..$8000      ; 32768 bytes
    incbin "../data/3D.bin":$0000..$8000      ; 32768 bytes
