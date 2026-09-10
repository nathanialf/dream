; bank $C9  (file $090000)

org $C90000
    incbin "../data/12.bin":$0000..$8000      ; 32768 bytes
    incbin "../data/13.bin":$0000..$8000      ; 32768 bytes
