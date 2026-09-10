; bank $C3  (file $030000)

org $C30000
    incbin "../data/brr/sample_35.bin":$08B2..$0D96        ; 1252 bytes
    incbin "../data/brr/sample_36.bin"                        ; 8608 bytes (whole asset)
    incbin "../data/brr/sample_37.bin"                        ; 2911 bytes (whole asset)
    incbin "../data/brr/sample_38.bin"                        ; 1579 bytes (whole asset)
    incbin "../data/brr/sample_39.bin"                        ; 760 bytes (whole asset)
    incbin "../data/brr/sample_40.bin"                        ; 40 bytes (whole asset)
    incbin "../data/brr/sample_41.bin"                        ; 751 bytes (whole asset)
    incbin "../data/brr/sample_42.bin"                        ; 1075 bytes (whole asset)
    incbin "../data/brr/sample_43.bin"                        ; 256 bytes (whole asset)
    incbin "../data/brr/sample_44.bin"                        ; 499 bytes (whole asset)
    incbin "../data/brr/sample_45.bin"                        ; 184 bytes (whole asset)
    incbin "../data/brr/sample_46.bin"                        ; 564 bytes (whole asset)
    incbin "../data/brr/sample_47.bin"                        ; 364 bytes (whole asset)
    incbin "../data/brr/sample_48.bin"                        ; 445 bytes (whole asset)
    incbin "../data/brr/sample_49.bin"                        ; 1156 bytes (whole asset)
    incbin "../data/brr/sample_50.bin"                        ; 391 bytes (whole asset)
    incbin "../data/gfx/tiles_c3_tail.bin"                        ; 11551 bytes (whole asset)
    incbin "../data/stale/stale_dup_c8_a.bin":$0000..$017E        ; 382 bytes
    incbin "../data/stale/stale_dup_c8_a.bin":$017E..$5154        ; 20438 bytes
    incbin "../data/gfx/tiles_c3_end.bin"                        ; 12330 bytes (whole asset)
