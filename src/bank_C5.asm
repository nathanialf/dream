; bank $C5  (file $050000)

org $C50000
    incbin "../data/gfx/tiles_bg3_mode1.bin"                        ; 512 bytes (whole asset)

data_C50200:
    incbin "../data/maps/mode1_param_tables.bin":$0000..$0060        ; 96 bytes

data_C50260:
    incbin "../data/maps/mode1_param_tables.bin":$0060..$00C0        ; 96 bytes
    incbin "../data/gfx/obj_tiles_1600.bin"                        ; 3072 bytes (whole asset)
    incbin "../data/stale/stale_dup_c8c9_multi.bin":$0000..$7140        ; 28992 bytes
    incbin "../data/stale/stale_dup_c8c9_multi.bin":$7140..$ABFF        ; 15039 bytes
    incbin "../data/stale/stale_dup_bg2_mode1.bin"                        ; 13858 bytes (whole asset)
    incbin "../data/unknown/unknown_counter_table.bin"                        ; 3871 bytes (whole asset)
