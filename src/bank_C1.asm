; bank $C1  (file $010000)

org $C10000
    incbin "../data/maps/tilemap_strip1.bin"                        ; 256 bytes (whole asset)
    incbin "../data/gfx/tiles_strip1.bin"                        ; 9984 bytes (whole asset)
    incbin "../data/maps/tilemap_strip2.bin"                        ; 256 bytes (whole asset)
    incbin "../data/gfx/tiles_strip2.bin"                        ; 2560 bytes (whole asset)
    incbin "../data/maps/tilemap_strip3.bin"                        ; 256 bytes (whole asset)
    incbin "../data/gfx/tiles_strip3.bin"                        ; 7136 bytes (whole asset)
    incbin "../data/gfx/font_tiles.bin"                        ; 1536 bytes (whole asset)
    incbin "../data/filler/fill_55_a.bin"                        ; 10784 bytes (whole asset)

spc_init:
    rep.b #$30                             ; C18000 m0x0
    stz.w $0036                            ; C18002 m0x0
    stz.w $0038                            ; C18005 m0x0
    stz.w $003A                            ; C18008 m0x0
    stz.w $003C                            ; C1800B m0x0
    stz.w $003E                            ; C1800E m0x0
    stz.w $0040                            ; C18011 m0x0
    stz.w $0042                            ; C18014 m0x0
    stz.w $0044                            ; C18017 m0x0
    stz.w $0048                            ; C1801A m0x0
    stz.w spc_port0_counter                ; C1801D m0x0
    jsr.w spc_ipl_upload_loader            ; C18020 m0x0
    jsr.w spc_upload_driver                ; C18023 m0x0
    jsr.w upload_global_samples            ; C18026 m0x0
    lda.w #$2E5C                           ; C18029 m0x0
    sta.l $000004                          ; C1802C m0x0
    lda.w #$00C2                           ; C18030 m0x0
    sta.l $000006                          ; C18033 m0x0
    jsr.w upload_inline_spc_block          ; C18037 m0x0
    jsr.w execute_spc_sound_engine         ; C1803A m0x0
    rtl                                    ; C1803D m0x0

execute_spc_sound_engine:
    lda.w #$0672                           ; C1803E m0x0
    sta.l $000007                          ; C18041 m0x0
    stz.w spc_word_count                   ; C18045 m0x0
    jsr.w upload_spc_block                 ; C18048 m0x0
    rts                                    ; C1804B m0x0

unused_spc_execute:
    incbin "../data/03.bin":$004C..$005A      ; 14 bytes

spc_ipl_upload_loader:
    rep.b #$20                             ; C1805A m0x0
    sep.b #$10                             ; C1805C m0x0
    lda.w #$BBAA                           ; C1805E m0x1

loc_C18061:
    cmp.w APUIO0                           ; C18061 m0x1
    bne loc_C18061                         ; C18064 m0x1
    lda.w #$04D8                           ; C18066 m0x1
    sta.w APUIO2                           ; C18069 m0x1
    lda.w #$01CC                           ; C1806C m0x1
    sta.w APUIO0                           ; C1806F m0x1
    tax                                    ; C18072 m0x1

loc_C18073:
    cpx.w APUIO0                           ; C18073 m0x1
    bne loc_C18073                         ; C18076 m0x1
    ldx.b #$00                             ; C18078 m0x1

loc_C1807A:
    lda.l spc_loader_image,x               ; C1807A m0x1
    tay                                    ; C1807E m0x1
    sty.w APUIO1                           ; C1807F m0x1
    stx.w APUIO0                           ; C18082 m0x1

loc_C18085:
    cpx.w APUIO0                           ; C18085 m0x1
    bne loc_C18085                         ; C18088 m0x1
    inx                                    ; C1808A m0x1
    cpx.b #$88                             ; C1808B m0x1
    bne loc_C1807A                         ; C1808D m0x1
    inx                                    ; C1808F m0x1
    txa                                    ; C18090 m0x1
    sta.w APUIO0                           ; C18091 m0x1

loc_C18094:
    cpx.w APUIO0                           ; C18094 m0x1
    bne loc_C18094                         ; C18097 m0x1
    stz.w spc_port0_counter                ; C18099 m0x1
    rep.b #$30                             ; C1809C m0x1
    rts                                    ; C1809E m0x0

spc_upload_driver:
    rep.b #$30                             ; C1809F m0x0
    lda.w #$0088                           ; C180A1 m0x0
    sta.l $000004                          ; C180A4 m0x0
    lda.w #$00C2                           ; C180A8 m0x0
    sta.l $000006                          ; C180AB m0x0
    lda.w #$0560                           ; C180AF m0x0
    sta.l $000007                          ; C180B2 m0x0
    lda.w #$0699                           ; C180B6 m0x0
    sta.l $000009                          ; C180B9 m0x0
    jsr.w upload_spc_block                 ; C180BD m0x0
    rts                                    ; C180C0 m0x0

upload_global_samples:
    lda.w #$1109                           ; C180C1 m0x0
    sta.l $000042                          ; C180C4 m0x0
    lda.w #$00C2                           ; C180C8 m0x0
    sta.l $000044                          ; C180CB m0x0
    lda.w #$3100                           ; C180CF m0x0
    sta.l $000036                          ; C180D2 m0x0
    stz.w $003E                            ; C180D6 m0x0
    lda.w #$3400                           ; C180D9 m0x0
    sta.l $00003A                          ; C180DC m0x0
    stz.w $003E                            ; C180E0 m0x0
    jsr.w sample_uploader                  ; C180E3 m0x0
    lda.l $000036                          ; C180E6 m0x0
    sta.l $000038                          ; C180EA m0x0
    lda.l $00003A                          ; C180EE m0x0
    sta.l $00003C                          ; C180F2 m0x0
    lda.l $00003E                          ; C180F6 m0x0
    sta.l $000040                          ; C180FA m0x0
    rts                                    ; C180FE m0x0

write_spc_command:
    rep.b #$30                             ; C180FF m0x0
    txa                                    ; C18101 m0x0
    sep.b #$10                             ; C18102 m0x0
    ldx.w spc_port0_counter                ; C18104 m0x1

loc_C18107:
    cpx.w APUIO0                           ; C18107 m0x1
    bne loc_C18107                         ; C1810A m0x1
    sta.w APUIO1                           ; C1810C m0x1
    inx                                    ; C1810F m0x1
    stx.w APUIO0                           ; C18110 m0x1
    stx.w spc_port0_counter                ; C18113 m0x1
    rep.b #$30                             ; C18116 m0x1
    rts                                    ; C18118 m0x0

upload_song_data:
    lda.l $000048                          ; C18119 m0x0
    clc                                    ; C1811D m0x0
    asl                                    ; C1811E m0x0
    sta.l $000004                          ; C1811F m0x0
    asl                                    ; C18123 m0x0
    adc.l $000004                          ; C18124 m0x0
    tax                                    ; C18128 m0x0
    lda.l data_C210B9,x                    ; C18129 m0x0
    sta.l $000004                          ; C1812D m0x0
    lda.l data_C210BB,x                    ; C18131 m0x0
    sta.l $000006                          ; C18135 m0x0
    jsr.w upload_inline_spc_block          ; C18139 m0x0
    rts                                    ; C1813C m0x0

upload_song_sound_effects:
    lda.l $000048                          ; C1813D m0x0
    clc                                    ; C18141 m0x0
    adc.l $000048                          ; C18142 m0x0
    adc.l $000048                          ; C18146 m0x0
    tax                                    ; C1814A m0x0
    lda.l data_C210EE,x                    ; C1814B m0x0
    sta.l $000004                          ; C1814F m0x0
    lda.l data_C210F0,x                    ; C18153 m0x0
    sta.l $000006                          ; C18157 m0x0
    jsr.w upload_inline_spc_block          ; C1815B m0x0
    rts                                    ; C1815E m0x0

sample_uploader:
    stz.w $003E                            ; C1815F m0x0
    ldx.w #$0000                           ; C18162 m0x0
    lda.l $00003E                          ; C18165 m0x0
    sta.l $00000C                          ; C18169 m0x0
    lda.l $000042                          ; C1816D m0x0
    sta.l $000010                          ; C18171 m0x0
    lda.l $000044                          ; C18175 m0x0
    sta.l $000012                          ; C18179 m0x0
    lda.l $000036                          ; C1817D m0x0
    sta.l $000014                          ; C18181 m0x0
    lda.l $00003A                          ; C18185 m0x0
    sta.l $000016                          ; C18189 m0x0

loc_C1818D:
    lda.b [$10]                            ; C1818D m0x0
    inc.w $0010                            ; C1818F m0x0
    inc.w $0010                            ; C18192 m0x0
    cmp.w #$FFFF                           ; C18195 m0x0
    beq loc_C181F0                         ; C18198 m0x0
    sta.l $00000E                          ; C1819A m0x0
    clc                                    ; C1819E m0x0
    adc.l $00000E                          ; C1819F m0x0
    adc.l $00000E                          ; C181A3 m0x0
    txy                                    ; C181A7 m0x0
    tax                                    ; C181A8 m0x0
    lda.l data_C20DB9,x                    ; C181A9 m0x0
    sta.l $000004                          ; C181AD m0x0
    lda.l data_C20DBB,x                    ; C181B1 m0x0
    sta.l $000006                          ; C181B5 m0x0
    tyx                                    ; C181B9 m0x0
    lda.l $00003A                          ; C181BA m0x0
    sta.l $7E2000,x                        ; C181BE m0x0
    inx                                    ; C181C2 m0x0
    inx                                    ; C181C3 m0x0
    lda.b [ptr_04]                         ; C181C4 m0x0
    clc                                    ; C181C6 m0x0
    adc.l $00003A                          ; C181C7 m0x0
    sta.l $7E2000,x                        ; C181CB m0x0
    inx                                    ; C181CF m0x0
    inx                                    ; C181D0 m0x0
    inc.w $0036                            ; C181D1 m0x0
    inc.w $0036                            ; C181D4 m0x0
    inc.w $0036                            ; C181D7 m0x0
    inc.w $0036                            ; C181DA m0x0
    inc.w ptr_04                           ; C181DD m0x0
    inc.w ptr_04                           ; C181E0 m0x0
    lda.b [ptr_04]                         ; C181E3 m0x0
    clc                                    ; C181E5 m0x0
    adc.l $00003A                          ; C181E6 m0x0
    sta.l $00003A                          ; C181EA m0x0
    bra loc_C1818D                         ; C181EE m0x0

loc_C181F0:
    lda.w #$2000                           ; C181F0 m0x0
    sta.l $000004                          ; C181F3 m0x0
    lda.w #$007E                           ; C181F7 m0x0
    sta.l $000006                          ; C181FA m0x0
    lda.l $000014                          ; C181FE m0x0
    sta.l $000007                          ; C18202 m0x0
    lda.w #$3400                           ; C18206 m0x0
    sec                                    ; C18209 m0x0
    sbc.l $000014                          ; C1820A m0x0
    clc                                    ; C1820E m0x0
    inc                                    ; C1820F m0x0
    lsr                                    ; C18210 m0x0
    sta.l $000009                          ; C18211 m0x0
    jsr.w upload_spc_block                 ; C18215 m0x0
    lda.w #$1109                           ; C18218 m0x0
    sta.l $000010                          ; C1821B m0x0
    lda.w #$00C2                           ; C1821F m0x0
    sta.l $000012                          ; C18222 m0x0
    lda.l $000042                          ; C18226 m0x0
    cmp.l $000010                          ; C1822A m0x0
    bne loc_C1823A                         ; C1822E m0x0
    lda.l $000044                          ; C18230 m0x0
    cmp.l $000012                          ; C18234 m0x0
    beq loc_C182A7                         ; C18238 m0x0

loc_C1823A:
    lda.b [$10]                            ; C1823A m0x0
    cmp.w #$FFFF                           ; C1823C m0x0
    beq loc_C18259                         ; C1823F m0x0
    inc.w $0010                            ; C18241 m0x0
    inc.w $0010                            ; C18244 m0x0
    tax                                    ; C18247 m0x0
    lda.l $00003E                          ; C18248 m0x0
    sep.b #$20                             ; C1824C m0x0
    sta.l $7E2000,x                        ; C1824E m1x0
    rep.b #$20                             ; C18252 m1x0
    inc.w $003E                            ; C18254 m0x0
    bra loc_C1823A                         ; C18257 m0x0

loc_C18259:
    lda.l $000042                          ; C18259 m0x0
    sta.l $000010                          ; C1825D m0x0
    lda.l $000044                          ; C18261 m0x0
    sta.l $000012                          ; C18265 m0x0

loc_C18269:
    lda.b [$10]                            ; C18269 m0x0
    cmp.w #$FFFF                           ; C1826B m0x0
    beq loc_C18288                         ; C1826E m0x0
    inc.w $0010                            ; C18270 m0x0
    inc.w $0010                            ; C18273 m0x0
    tax                                    ; C18276 m0x0
    lda.l $00003E                          ; C18277 m0x0
    sep.b #$20                             ; C1827B m0x0
    sta.l $7E2000,x                        ; C1827D m1x0
    rep.b #$20                             ; C18281 m1x0
    inc.w $003E                            ; C18283 m0x0
    bra loc_C18269                         ; C18286 m0x0

loc_C18288:
    lda.w #$2000                           ; C18288 m0x0
    sta.l $000004                          ; C1828B m0x0
    lda.w #$007E                           ; C1828F m0x0
    sta.l $000006                          ; C18292 m0x0
    lda.w #$0560                           ; C18296 m0x0
    sta.l $000007                          ; C18299 m0x0
    lda.w #$0080                           ; C1829D m0x0
    sta.l $000009                          ; C182A0 m0x0
    jsr.w upload_spc_block                 ; C182A4 m0x0

loc_C182A7:
    lda.l $000016                          ; C182A7 m0x0
    sta.l $00003A                          ; C182AB m0x0

loc_C182AF:
    lda.b [$42]                            ; C182AF m0x0
    cmp.w #$FFFF                           ; C182B1 m0x0
    beq loc_C18309                         ; C182B4 m0x0
    inc.w $0042                            ; C182B6 m0x0
    inc.w $0042                            ; C182B9 m0x0
    sta.l $00000E                          ; C182BC m0x0
    clc                                    ; C182C0 m0x0
    adc.l $00000E                          ; C182C1 m0x0
    adc.l $00000E                          ; C182C5 m0x0
    tax                                    ; C182C9 m0x0
    lda.l data_C20DB9,x                    ; C182CA m0x0
    sta.l $000004                          ; C182CE m0x0
    lda.l data_C20DBB,x                    ; C182D2 m0x0
    sta.l $000006                          ; C182D6 m0x0
    inc.w ptr_04                           ; C182DA m0x0
    inc.w ptr_04                           ; C182DD m0x0
    lda.l $00003A                          ; C182E0 m0x0
    sta.l $000007                          ; C182E4 m0x0
    lda.b [ptr_04]                         ; C182E8 m0x0
    sta.l $000009                          ; C182EA m0x0
    clc                                    ; C182EE m0x0
    adc.l $000007                          ; C182EF m0x0
    sta.l $00003A                          ; C182F3 m0x0
    clc                                    ; C182F7 m0x0
    inc.w spc_word_count                   ; C182F8 m0x0
    lsr.w spc_word_count                   ; C182FB m0x0
    inc.w ptr_04                           ; C182FE m0x0
    inc.w ptr_04                           ; C18301 m0x0
    jsr.w upload_spc_block                 ; C18304 m0x0
    bra loc_C182AF                         ; C18307 m0x0

loc_C18309:
    rts                                    ; C18309 m0x0

upload_inline_spc_block:
    lda.b [ptr_04]                         ; C1830A m0x0
    sta.l $000007                          ; C1830C m0x0
    inc.w ptr_04                           ; C18310 m0x0
    inc.w ptr_04                           ; C18313 m0x0
    lda.b [ptr_04]                         ; C18316 m0x0
    sta.l $000009                          ; C18318 m0x0
    inc.w ptr_04                           ; C1831C m0x0
    inc.w ptr_04                           ; C1831F m0x0
    bra upload_spc_block                   ; C18322 m0x0

upload_spc_block:
    sep.b #$10                             ; C18324 m0x0
    ldx.w spc_port0_counter                ; C18326 m0x1

loc_C18329:
    cpx.w APUIO0                           ; C18329 m0x1
    bne loc_C18329                         ; C1832C m0x1
    lda.l $000007                          ; C1832E m0x1
    sta.w APUIO1                           ; C18332 m0x1
    inx                                    ; C18335 m0x1
    stx.w APUIO0                           ; C18336 m0x1
    lda.l $000009                          ; C18339 m0x1
    sta.l $00000B                          ; C1833D m0x1

loc_C18341:
    cpx.w APUIO0                           ; C18341 m0x1
    bne loc_C18341                         ; C18344 m0x1
    sta.w APUIO1                           ; C18346 m0x1
    inx                                    ; C18349 m0x1
    stx.w APUIO0                           ; C1834A m0x1
    lda.l $000009                          ; C1834D m0x1
    beq loc_C1836C                         ; C18351 m0x1
    ldy.b #$00                             ; C18353 m0x1

loc_C18355:
    lda.b [ptr_04],y                       ; C18355 m0x1
    iny                                    ; C18357 m0x1
    iny                                    ; C18358 m0x1
    beq loc_C18377                         ; C18359 m0x1

loc_C1835B:
    cpx.w APUIO0                           ; C1835B m0x1
    bne loc_C1835B                         ; C1835E m0x1
    inx                                    ; C18360 m0x1
    sta.w APUIO1                           ; C18361 m0x1
    stx.w APUIO0                           ; C18364 m0x1
    dec.w spc_word_count                   ; C18367 m0x1
    bne loc_C18355                         ; C1836A m0x1

loc_C1836C:
    stx.w spc_port0_counter                ; C1836C m0x1
    rep.b #$30                             ; C1836F m0x1
    lda.l $00000B                          ; C18371 m0x0
    asl                                    ; C18375 m0x0
    rts                                    ; C18376 m0x0

loc_C18377:
    ldy.w $0005                            ; C18377 m0x1
    iny                                    ; C1837A m0x1
    bne loc_C1838B                         ; C1837B m0x1
    sty.w $0005                            ; C1837D m0x1
    ldy.w $0006                            ; C18380 m0x1
    iny                                    ; C18383 m0x1
    sty.w $0006                            ; C18384 m0x1
    ldy.b #$00                             ; C18387 m0x1
    bra loc_C1835B                         ; C18389 m0x1

loc_C1838B:
    sty.w $0005                            ; C1838B m0x1
    ldy.b #$00                             ; C1838E m0x1
    bra loc_C1835B                         ; C18390 m0x1

upload_song_sample_set:
    lda.l $000048                          ; C18392 m0x0
    clc                                    ; C18396 m0x0
    asl                                    ; C18397 m0x0
    sta.l $000004                          ; C18398 m0x0
    asl                                    ; C1839C m0x0
    adc.l $000004                          ; C1839D m0x0
    tax                                    ; C183A1 m0x0
    lda.l data_C210BC,x                    ; C183A2 m0x0
    sta.l $000042                          ; C183A6 m0x0
    lda.l data_C210BE,x                    ; C183AA m0x0
    sta.l $000044                          ; C183AE m0x0
    lda.l $000038                          ; C183B2 m0x0
    sta.l $000036                          ; C183B6 m0x0
    lda.l $00003C                          ; C183BA m0x0
    sta.l $00003A                          ; C183BE m0x0
    lda.l $000040                          ; C183C2 m0x0
    sta.l $00003E                          ; C183C6 m0x0
    jsr.w sample_uploader                  ; C183CA m0x0
    rts                                    ; C183CD m0x0

spc_command:
    tax                                    ; C183CE m0x0
    and.w #$00FF                           ; C183CF m0x0
    sta.l $000048                          ; C183D2 m0x0
    txa                                    ; C183D6 m0x0
    ora.w #$00FF                           ; C183D7 m0x0
    tax                                    ; C183DA m0x0
    jsr.w write_spc_command                ; C183DB m0x0
    jsr.w upload_song_sample_set           ; C183DE m0x0
    jsr.w upload_song_data                 ; C183E1 m0x0
    jsr.w upload_song_sound_effects        ; C183E4 m0x0
    jsr.w execute_spc_sound_engine         ; C183E7 m0x0
    ldx.w #$00FE                           ; C183EA m0x0
    jsr.w write_spc_command                ; C183ED m0x0
    rtl                                    ; C183F0 m0x0

unused_spc_set_e7_and_play:
    incbin "../data/03.bin":$03F1..$0403      ; 18 bytes

unused_spc_set_fb_and_play:
    xba                                    ; C18403 m0x0
    and.w #$FF00                           ; C18404 m0x0
    ora.w #$00FB                           ; C18407 m0x0
    tax                                    ; C1840A m0x0
    jsr.w write_spc_command                ; C1840B m0x0
    ldx.w #$00FE                           ; C1840E m0x0
    jsr.w write_spc_command                ; C18411 m0x0
    rtl                                    ; C18414 m0x0

sfx_command_dispatch:
    tax                                    ; C18415 m0x0
    jsr.w write_spc_command                ; C18416 m0x0
    rtl                                    ; C18419 m0x0
    incbin "../data/stale/stale_dup_bg1_mode3.bin"                        ; 26278 bytes (whole asset)
    incbin "../data/stale/stale_dup_bg1_tiles_mode2_tail.bin"                        ; 1280 bytes (whole asset)
    incbin "../data/filler/fill_55_b.bin"                        ; 4160 bytes (whole asset)
