; bank $DB  (file $1B0000)

org $DB0000
    incbin "../data/36.bin":$0000..$8000      ; 32768 bytes
    incbin "../data/37.bin":$0000..$8000      ; 32768 bytes
