; bank $C2  (file $020000)

org $C20000

spc_loader_image:
    incbin "../data/spc/spc700_ipl_loader.bin"                        ; 136 bytes (whole asset)

spc_driver_blocks:
    incbin "../data/spc/spc700_driver.bin":$0000..$0D31        ; 3377 bytes

data_C20DB9:
    incbin "../data/spc/spc700_driver.bin":$0D31..$0D32        ; 1 bytes
    incbin "../data/music/sample_pointer_table.bin":$0000..$0001        ; 1 bytes

data_C20DBB:
    incbin "../data/music/sample_pointer_table.bin":$0001..$0098        ; 151 bytes
    incbin "../data/music/sample_pointer_tail.bin"                        ; 615 bytes (whole asset)

data_C210B9:
    incbin "../data/music/song_table.bin":$0000..$0002        ; 2 bytes

data_C210BB:
    incbin "../data/music/song_table.bin":$0002..$0003        ; 1 bytes

data_C210BC:
    incbin "../data/music/song_table.bin":$0003..$0005        ; 2 bytes

data_C210BE:
    incbin "../data/music/song_table.bin":$0005..$0035        ; 48 bytes

data_C210EE:
    incbin "../data/music/sfx_bank2_ptrs.bin":$0000..$0002        ; 2 bytes

data_C210F0:
    incbin "../data/music/sfx_bank2_ptrs.bin":$0002..$001B        ; 25 bytes
    incbin "../data/music/sample_lists.bin"                        ; 150 bytes (whole asset)
    incbin "../data/music/song_00.bin"                        ; 4253 bytes (whole asset)
    incbin "../data/music/song_01.bin"                        ; 2547 bytes (whole asset)
    incbin "../data/music/song_02.bin"                        ; 537 bytes (whole asset)
    incbin "../data/music/song_03.bin"                        ; 4 bytes (whole asset)
    incbin "../data/music/song_04.bin"                        ; 4 bytes (whole asset)
    incbin "../data/music/song_05.bin"                        ; 4 bytes (whole asset)
    incbin "../data/music/song_06.bin"                        ; 4 bytes (whole asset)
    incbin "../data/music/song_07.bin"                        ; 4 bytes (whole asset)
    incbin "../data/music/sfx_bank1.bin"                        ; 532 bytes (whole asset)
    incbin "../data/music/sfx_bank2_blocks.bin"                        ; 17 bytes (whole asset)
    incbin "../data/music/sfx_bank2_filler.bin"                        ; 20 bytes (whole asset)
    incbin "../data/brr/sample_00.bin"                        ; 32 bytes (whole asset)
    incbin "../data/brr/sample_01.bin"                        ; 1066 bytes (whole asset)
    incbin "../data/brr/sample_02.bin"                        ; 1778 bytes (whole asset)
    incbin "../data/brr/sample_03.bin"                        ; 1426 bytes (whole asset)
    incbin "../data/brr/sample_04.bin"                        ; 1084 bytes (whole asset)
    incbin "../data/brr/sample_05.bin"                        ; 2785 bytes (whole asset)
    incbin "../data/brr/sample_06.bin"                        ; 1274 bytes (whole asset)
    incbin "../data/brr/sample_07.bin"                        ; 734 bytes (whole asset)
    incbin "../data/brr/sample_08.bin"                        ; 2480 bytes (whole asset)
    incbin "../data/brr/sample_09.bin"                        ; 526 bytes (whole asset)
    incbin "../data/brr/sample_10.bin"                        ; 644 bytes (whole asset)
    incbin "../data/brr/sample_11.bin"                        ; 31 bytes (whole asset)
    incbin "../data/brr/sample_12.bin"                        ; 1274 bytes (whole asset)
    incbin "../data/brr/sample_13.bin"                        ; 2111 bytes (whole asset)
    incbin "../data/brr/sample_14.bin"                        ; 1903 bytes (whole asset)
    incbin "../data/brr/sample_15.bin"                        ; 212 bytes (whole asset)
    incbin "../data/brr/sample_16.bin"                        ; 661 bytes (whole asset)
    incbin "../data/brr/sample_17.bin":$0000..$0136        ; 310 bytes
    incbin "../data/brr/sample_17.bin":$0136..$02D5        ; 415 bytes
    incbin "../data/brr/sample_18.bin"                        ; 4558 bytes (whole asset)
    incbin "../data/brr/sample_19.bin"                        ; 410 bytes (whole asset)
    incbin "../data/brr/sample_20.bin"                        ; 2092 bytes (whole asset)
    incbin "../data/brr/sample_21.bin"                        ; 212 bytes (whole asset)
    incbin "../data/brr/sample_22.bin"                        ; 527 bytes (whole asset)
    incbin "../data/brr/sample_23.bin"                        ; 1283 bytes (whole asset)
    incbin "../data/brr/sample_24.bin"                        ; 5044 bytes (whole asset)
    incbin "../data/brr/sample_25.bin"                        ; 1841 bytes (whole asset)
    incbin "../data/brr/sample_26.bin"                        ; 1516 bytes (whole asset)
    incbin "../data/brr/sample_27.bin"                        ; 1750 bytes (whole asset)
    incbin "../data/brr/sample_28.bin"                        ; 1669 bytes (whole asset)
    incbin "../data/brr/sample_29.bin"                        ; 427 bytes (whole asset)
    incbin "../data/brr/sample_30.bin"                        ; 652 bytes (whole asset)
    incbin "../data/brr/sample_31.bin"                        ; 500 bytes (whole asset)
    incbin "../data/brr/sample_32.bin"                        ; 3514 bytes (whole asset)
    incbin "../data/brr/sample_33.bin"                        ; 3065 bytes (whole asset)
    incbin "../data/brr/sample_34.bin"                        ; 1067 bytes (whole asset)
    incbin "../data/brr/sample_35.bin":$0000..$08B2        ; 2226 bytes
