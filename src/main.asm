; Dream: Land of Giants (SNES prototype) - reassemblable disassembly
; build: make (see README.md); data/ is produced by tools/extract.py from your own ROM
hirom

; RAM / register symbols
nmi_handler_ptr = $0000
dma_pending_mask = $0002
ptr_04 = $0004
spc_dest_addr = $0007
spc_word_count = $0009
sprite_frame_ptr = $0026
sprite_frame_bank = $0028
sprite_frame_ptr2 = $002A
sprite_frame_bank2 = $002C
nmitimen_shadow = $0034
spc_port0_counter = $0046
entity_screen_x = $004C
entity_screen_y = $004E
oam_entry_ptr = $0054
camera_x = $0062
camera_y = $0068
walk_cycle_timer = $0070
walk_cycle_parity = $0072
tilemap_a_addr = $007A
tilemap_a_bank = $007C
tilemap_b_addr = $007E
metatile_data_bank = $0080
level_width_mask = $0086
level_height_mask = $0088
oam_write_ptr = $0094
entity_render_index = $0096
layer_parallax_mode = $0098
camera_y_lookahead = $009A
init_magic_AA55 = $009C
init_magic_FFFF = $009E
game_mode = $00A4
oam_buffer = $0200
oam_buffer_upper = $0400
entity_type = $0708
entity_state = $0728
entity_hitstun_timer = $0768
entity_flags = $0788
entity_frame_id = $07C8
entity_x = $0828
entity_x_sub = $0848
entity_vel_x = $0868
entity_vel_x_target = $0888
entity_y = $08A8
entity_y_sub = $08C8
entity_vel_y = $0948
entity_parent_index = $0968
entity_depth_key = $0988
entity_render_order = $09A8
entity_anim_id = $09E8
entity_anim_rate = $0A68
INIDISP = $2100
OBSEL = $2101
OAMADDL = $2102
OAMADDH = $2103
OAMDATA = $2104
BGMODE = $2105
MOSAIC = $2106
BG1SC = $2107
BG2SC = $2108
BG3SC = $2109
BG4SC = $210A
BG12NBA = $210B
BG34NBA = $210C
BG1HOFS = $210D
BG1VOFS = $210E
BG2HOFS = $210F
BG2VOFS = $2110
BG3HOFS = $2111
BG3VOFS = $2112
BG4HOFS = $2113
BG4VOFS = $2114
VMAIN = $2115
VMADDL = $2116
VMADDH = $2117
VMDATAL = $2118
VMDATAH = $2119
M7SEL = $211A
M7A = $211B
M7B = $211C
M7C = $211D
M7D = $211E
M7X = $211F
M7Y = $2120
CGADD = $2121
CGDATA = $2122
W12SEL = $2123
W34SEL = $2124
WOBJSEL = $2125
WH0 = $2126
WH1 = $2127
WH2 = $2128
WH3 = $2129
WBGLOG = $212A
WOBJLOG = $212B
TM = $212C
TS = $212D
TMW = $212E
TSW = $212F
CGWSEL = $2130
CGADSUB = $2131
COLDATA = $2132
SETINI = $2133
MPYL = $2134
MPYM = $2135
MPYH = $2136
SLHV = $2137
RDOAM = $2138
RDVRAML = $2139
RDVRAMH = $213A
RDCGRAM = $213B
OPHCT = $213C
OPVCT = $213D
STAT77 = $213E
STAT78 = $213F
APUIO0 = $2140
APUIO1 = $2141
APUIO2 = $2142
APUIO3 = $2143
WMDATA = $2180
WMADDL = $2181
WMADDM = $2182
WMADDH = $2183
JOYSER0 = $4016
JOYSER1 = $4017
NMITIMEN = $4200
WRIO = $4201
WRMPYA = $4202
WRMPYB = $4203
WRDIVL = $4204
WRDIVH = $4205
WRDIVB = $4206
HTIMEL = $4207
HTIMEH = $4208
VTIMEL = $4209
VTIMEH = $420A
MDMAEN = $420B
HDMAEN = $420C
MEMSEL = $420D
RDNMI = $4210
TIMEUP = $4211
HVBJOY = $4212
RDIO = $4213
RDDIVL = $4214
RDDIVH = $4215
RDMPYL = $4216
RDMPYH = $4217
JOY1L = $4218
JOY1H = $4219
JOY2L = $421A
JOY2H = $421B
JOY3L = $421C
JOY3H = $421D
JOY4L = $421E
JOY4H = $421F
DMAP0 = $4300
BBAD0 = $4301
A1TL0 = $4302
A1TH0 = $4303
A1B0 = $4304
DASL0 = $4305
DASH0 = $4306
DASB0 = $4307
A2AL0 = $4308
A2AH0 = $4309
NTRL0 = $430A
DMAP1 = $4310
BBAD1 = $4311
A1TL1 = $4312
A1TH1 = $4313
A1B1 = $4314
DASL1 = $4315
DASH1 = $4316
DASB1 = $4317
A2AL1 = $4318
A2AH1 = $4319
NTRL1 = $431A
DMAP2 = $4320
BBAD2 = $4321
A1TL2 = $4322
A1TH2 = $4323
A1B2 = $4324
DASL2 = $4325
DASH2 = $4326
DASB2 = $4327
A2AL2 = $4328
A2AH2 = $4329
NTRL2 = $432A
DMAP3 = $4330
BBAD3 = $4331
A1TL3 = $4332
A1TH3 = $4333
A1B3 = $4334
DASL3 = $4335
DASH3 = $4336
DASB3 = $4337
A2AL3 = $4338
A2AH3 = $4339
NTRL3 = $433A
DMAP4 = $4340
BBAD4 = $4341
A1TL4 = $4342
A1TH4 = $4343
A1B4 = $4344
DASL4 = $4345
DASH4 = $4346
DASB4 = $4347
A2AL4 = $4348
A2AH4 = $4349
NTRL4 = $434A
DMAP5 = $4350
BBAD5 = $4351
A1TL5 = $4352
A1TH5 = $4353
A1B5 = $4354
DASL5 = $4355
DASH5 = $4356
DASB5 = $4357
A2AL5 = $4358
A2AH5 = $4359
NTRL5 = $435A
DMAP6 = $4360
BBAD6 = $4361
A1TL6 = $4362
A1TH6 = $4363
A1B6 = $4364
DASL6 = $4365
DASH6 = $4366
DASB6 = $4367
A2AL6 = $4368
A2AH6 = $4369
NTRL6 = $436A
DMAP7 = $4370
BBAD7 = $4371
A1TL7 = $4372
A1TH7 = $4373
A1B7 = $4374
DASL7 = $4375
DASH7 = $4376
DASB7 = $4377
A2AL7 = $4378
A2AH7 = $4379
NTRL7 = $437A

incsrc "bank_C0.asm"
incsrc "bank_C1.asm"
incsrc "bank_C2.asm"
incsrc "bank_C3.asm"
incsrc "bank_C4.asm"
incsrc "bank_C5.asm"
incsrc "bank_C6.asm"
incsrc "bank_C7.asm"
incsrc "bank_C8.asm"
incsrc "bank_C9.asm"
incsrc "bank_CA.asm"
incsrc "bank_CB.asm"
incsrc "bank_CC.asm"
incsrc "bank_CD.asm"
incsrc "bank_CE.asm"
incsrc "bank_CF.asm"
incsrc "bank_D0.asm"
incsrc "bank_D1.asm"
incsrc "bank_D2.asm"
incsrc "bank_D3.asm"
incsrc "bank_D4.asm"
incsrc "bank_D5.asm"
incsrc "bank_D6.asm"
incsrc "bank_D7.asm"
incsrc "bank_D8.asm"
incsrc "bank_D9.asm"
incsrc "bank_DA.asm"
incsrc "bank_DB.asm"
incsrc "bank_DC.asm"
incsrc "bank_DD.asm"
incsrc "bank_DE.asm"
incsrc "bank_DF.asm"
