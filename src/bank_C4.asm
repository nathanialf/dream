; bank $C4  (file $040000)

org $C40000

data_C40000:
    incbin "../data/sprites/frame_table.bin":$0000..$0002        ; 2 bytes

data_C40002:
    incbin "../data/sprites/frame_table.bin":$0002..$1858        ; 6230 bytes

anim_script_table:
    incbin "../data/anim/script_index.bin"                        ; 348 bytes (whole asset)
    incbin "../data/anim/script_000.bin"                        ; 638 bytes (whole asset)
    incbin "../data/anim/script_001.bin"                        ; 638 bytes (whole asset)
    incbin "../data/anim/script_002.bin"                        ; 638 bytes (whole asset)
    incbin "../data/anim/script_003.bin"                        ; 638 bytes (whole asset)
    incbin "../data/anim/script_004.bin"                        ; 638 bytes (whole asset)
    incbin "../data/anim/script_005.bin"                        ; 638 bytes (whole asset)
    incbin "../data/anim/script_006.bin"                        ; 638 bytes (whole asset)
    incbin "../data/anim/script_007.bin"                        ; 118 bytes (whole asset)
    incbin "../data/anim/script_008.bin"                        ; 118 bytes (whole asset)
    incbin "../data/anim/script_009.bin"                        ; 118 bytes (whole asset)
    incbin "../data/anim/script_010.bin"                        ; 118 bytes (whole asset)
    incbin "../data/anim/script_011.bin"                        ; 118 bytes (whole asset)
    incbin "../data/anim/script_012.bin"                        ; 118 bytes (whole asset)
    incbin "../data/anim/script_013.bin"                        ; 134 bytes (whole asset)
    incbin "../data/anim/script_014.bin"                        ; 134 bytes (whole asset)
    incbin "../data/anim/script_015.bin"                        ; 134 bytes (whole asset)
    incbin "../data/anim/script_016.bin"                        ; 134 bytes (whole asset)
    incbin "../data/anim/script_017.bin"                        ; 134 bytes (whole asset)
    incbin "../data/anim/script_018.bin"                        ; 134 bytes (whole asset)
    incbin "../data/anim/script_019.bin"                        ; 120 bytes (whole asset)
    incbin "../data/anim/script_020.bin"                        ; 112 bytes (whole asset)
    incbin "../data/anim/script_021.bin"                        ; 112 bytes (whole asset)
    incbin "../data/anim/script_022.bin"                        ; 112 bytes (whole asset)
    incbin "../data/anim/script_023.bin"                        ; 112 bytes (whole asset)
    incbin "../data/anim/script_024.bin"                        ; 112 bytes (whole asset)
    incbin "../data/anim/script_025.bin"                        ; 128 bytes (whole asset)
    incbin "../data/anim/script_026.bin"                        ; 120 bytes (whole asset)
    incbin "../data/anim/script_027.bin"                        ; 120 bytes (whole asset)
    incbin "../data/anim/script_028.bin"                        ; 128 bytes (whole asset)
    incbin "../data/anim/script_029.bin"                        ; 120 bytes (whole asset)
    incbin "../data/anim/script_030.bin"                        ; 120 bytes (whole asset)
    incbin "../data/anim/script_031.bin"                        ; 200 bytes (whole asset)
    incbin "../data/anim/script_032.bin"                        ; 200 bytes (whole asset)
    incbin "../data/anim/script_033.bin"                        ; 200 bytes (whole asset)
    incbin "../data/anim/script_034.bin"                        ; 200 bytes (whole asset)
    incbin "../data/anim/script_035.bin"                        ; 200 bytes (whole asset)
    incbin "../data/anim/script_036.bin"                        ; 200 bytes (whole asset)
    incbin "../data/anim/script_037.bin"                        ; 102 bytes (whole asset)
    incbin "../data/anim/script_038.bin"                        ; 102 bytes (whole asset)
    incbin "../data/anim/script_039.bin"                        ; 102 bytes (whole asset)
    incbin "../data/anim/script_040.bin"                        ; 38 bytes (whole asset)
    incbin "../data/anim/script_041.bin"                        ; 38 bytes (whole asset)
    incbin "../data/anim/script_042.bin"                        ; 38 bytes (whole asset)
    incbin "../data/anim/script_043.bin"                        ; 118 bytes (whole asset)
    incbin "../data/anim/script_044.bin"                        ; 118 bytes (whole asset)
    incbin "../data/anim/script_045.bin"                        ; 118 bytes (whole asset)
    incbin "../data/anim/script_046.bin"                        ; 118 bytes (whole asset)
    incbin "../data/anim/script_047.bin"                        ; 134 bytes (whole asset)
    incbin "../data/anim/script_048.bin"                        ; 134 bytes (whole asset)
    incbin "../data/anim/script_049.bin"                        ; 134 bytes (whole asset)
    incbin "../data/anim/script_050.bin"                        ; 134 bytes (whole asset)
    incbin "../data/anim/script_051.bin"                        ; 134 bytes (whole asset)
    incbin "../data/anim/script_052.bin"                        ; 134 bytes (whole asset)
    incbin "../data/anim/script_053.bin"                        ; 102 bytes (whole asset)
    incbin "../data/anim/script_054.bin"                        ; 102 bytes (whole asset)
    incbin "../data/anim/script_055.bin"                        ; 160 bytes (whole asset)
    incbin "../data/anim/script_056.bin"                        ; 152 bytes (whole asset)
    incbin "../data/anim/script_057.bin"                        ; 152 bytes (whole asset)
    incbin "../data/anim/script_058.bin"                        ; 160 bytes (whole asset)
    incbin "../data/anim/script_059.bin"                        ; 638 bytes (whole asset)
    incbin "../data/anim/script_060.bin"                        ; 638 bytes (whole asset)
    incbin "../data/anim/script_061.bin"                        ; 638 bytes (whole asset)
    incbin "../data/anim/script_062.bin"                        ; 638 bytes (whole asset)
    incbin "../data/anim/script_063.bin"                        ; 104 bytes (whole asset)
    incbin "../data/anim/script_064.bin"                        ; 96 bytes (whole asset)
    incbin "../data/anim/script_065.bin"                        ; 104 bytes (whole asset)
    incbin "../data/anim/script_066.bin"                        ; 96 bytes (whole asset)
    incbin "../data/anim/script_067.bin"                        ; 120 bytes (whole asset)
    incbin "../data/anim/script_068.bin"                        ; 112 bytes (whole asset)
    incbin "../data/anim/script_069.bin"                        ; 120 bytes (whole asset)
    incbin "../data/anim/script_070.bin"                        ; 112 bytes (whole asset)
    incbin "../data/anim/script_071.bin"                        ; 120 bytes (whole asset)
    incbin "../data/anim/script_072.bin"                        ; 118 bytes (whole asset)
    incbin "../data/anim/script_073.bin"                        ; 118 bytes (whole asset)
    incbin "../data/anim/script_074.bin"                        ; 62 bytes (whole asset)
    incbin "../data/anim/script_075.bin"                        ; 46 bytes (whole asset)
    incbin "../data/anim/script_076.bin"                        ; 160 bytes (whole asset)
    incbin "../data/anim/script_077.bin"                        ; 160 bytes (whole asset)
    incbin "../data/anim/script_078.bin"                        ; 128 bytes (whole asset)
    incbin "../data/anim/script_079.bin"                        ; 128 bytes (whole asset)
    incbin "../data/anim/script_080.bin"                        ; 78 bytes (whole asset)
    incbin "../data/anim/script_081.bin"                        ; 78 bytes (whole asset)
    incbin "../data/anim/script_082.bin"                        ; 304 bytes (whole asset)
    incbin "../data/anim/script_083.bin"                        ; 304 bytes (whole asset)
    incbin "../data/anim/script_084.bin"                        ; 70 bytes (whole asset)
    incbin "../data/anim/script_085.bin"                        ; 70 bytes (whole asset)
    incbin "../data/anim/script_086.bin"                        ; 62 bytes (whole asset)
    incbin "../data/anim/script_087.bin"                        ; 62 bytes (whole asset)
    incbin "../data/anim/script_088.bin"                        ; 118 bytes (whole asset)
    incbin "../data/anim/script_089.bin"                        ; 118 bytes (whole asset)
    incbin "../data/anim/script_090.bin"                        ; 1238 bytes (whole asset)
    incbin "../data/anim/script_091.bin"                        ; 1238 bytes (whole asset)
    incbin "../data/anim/script_092.bin"                        ; 70 bytes (whole asset)
    incbin "../data/anim/script_093.bin"                        ; 14 bytes (whole asset)
    incbin "../data/anim/script_094.bin"                        ; 14 bytes (whole asset)

data_C46588:
    incbin "../data/hdma/wave_table_sine.bin"                        ; 512 bytes (whole asset)

data_C46788:
    incbin "../data/maps/camera_parallax_curves.bin":$0000..$0080        ; 128 bytes

data_C46808:
    incbin "../data/maps/camera_parallax_curves.bin":$0080..$0100        ; 128 bytes

data_C46888:
    incbin "../data/maps/camera_parallax_curves.bin":$0100..$0180        ; 128 bytes

data_C46908:
    incbin "../data/maps/camera_parallax_curves.bin":$0180..$0200        ; 128 bytes

data_C46988:
    incbin "../data/maps/scroll_offset_tables.bin":$0000..$0100        ; 256 bytes

data_C46A88:
    incbin "../data/maps/scroll_offset_tables.bin":$0100..$0200        ; 256 bytes

data_C46B88:
    incbin "../data/palettes/palette_cycle_ramp.bin"                        ; 192 bytes (whole asset)
    incbin "../data/palettes/palettes_main_block.bin"                        ; 2075 bytes (whole asset)
    incbin "../data/stale/stale_dup_c7_mid.bin":$0000..$0B9D        ; 2973 bytes
    incbin "../data/stale/stale_dup_c7_mid.bin":$0B9D..$69BF        ; 24098 bytes
    incbin "../data/stale/stale_dup_metatiles_mode0.bin"                        ; 8000 bytes (whole asset)
    incbin "../data/gfx/tiles_tail_50000.bin"                        ; 670 bytes (whole asset)
