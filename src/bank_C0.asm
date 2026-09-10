; bank $C0  (file $000000)

org $C00000

stale_build_image:
    incbin "../data/stale/stale_code.bin"                        ; 12288 bytes (whole asset)
    incbin "../data/stale/stale_anim_table.bin"                        ; 6912 bytes (whole asset)
    incbin "../data/stale/stale_tiles.bin"                        ; 12232 bytes (whole asset)
    incbin "../data/stale/stale_palette.bin"                        ; 1016 bytes (whole asset)
    incbin "../data/stale/stale_tail.bin"                        ; 320 bytes (whole asset)

reset:
    clc                                    ; C08000 m1x1
    xce                                    ; C08001 m1x1
    sei                                    ; C08002 m1x1
    rep.b #$30                             ; C08003 m1x1
    cld                                    ; C08005 m0x0
    lda.w #$0000                           ; C08006 m0x0
    tcd                                    ; C08009 m0x0
    phk                                    ; C0800A m0x0
    plb                                    ; C0800B m0x0
    ldx.w #$01FF                           ; C0800C m0x0
    txs                                    ; C0800F m0x0
    tdc                                    ; C08010 m0x0
    tax                                    ; C08011 m0x0

loc_C08012:
    sta.l $7E0000,x                        ; C08012 m0x0
    sta.l $7F0000,x                        ; C08016 m0x0
    inx                                    ; C0801A m0x0
    inx                                    ; C0801B m0x0
    bne loc_C08012                         ; C0801C m0x0
    jsl.l $810000+(spc_init&$FFFF)         ; C0801E m0x0
    lda.w #$0001                           ; C08022 m0x0
    jsl.l $810000+(spc_command&$FFFF)      ; C08025 m0x0
    sep.b #$20                             ; C08029 m0x0
    lda.b #$80                             ; C0802B m1x0
    sta.w INIDISP                          ; C0802D m1x0
    rep.b #$20                             ; C08030 m1x0
    jsr.w ppu_init                         ; C08032 m0x0
    lda.w #$AA55                           ; C08035 m0x0
    sta.b init_magic_AA55                  ; C08038 m0x0
    lda.w #$FFFF                           ; C0803A m0x0
    sta.b init_magic_FFFF                  ; C0803D m0x0
    jmp.w loc_C0BB81                       ; C0803F m0x0

loc_C08042:
    rep.b #$30                             ; C08042 m0x0
    cld                                    ; C08044 m0x0
    lda.w #$0000                           ; C08045 m0x0
    tcd                                    ; C08048 m0x0
    phk                                    ; C08049 m0x0
    plb                                    ; C0804A m0x0
    ldx.w #$01FF                           ; C0804B m0x0
    txs                                    ; C0804E m0x0
    lda.w #$0000                           ; C0804F m0x0
    sta.b game_mode                        ; C08052 m0x0
    lda.w #$AA55                           ; C08054 m0x0
    sta.b init_magic_AA55                  ; C08057 m0x0
    lda.w #$FFFF                           ; C08059 m0x0
    sta.b init_magic_FFFF                  ; C0805C m0x0

loc_C0805E:
    lda.b game_mode                        ; C0805E m0x0
    beq loc_C08065                         ; C08060 m0x0
    lda.w #$0002                           ; C08062 m0x0

loc_C08065:
    jsl.l $810000+(spc_command&$FFFF)      ; C08065 m0x0
    jsr.w ppu_init                         ; C08069 m0x0
    lda.b game_mode                        ; C0806C m0x0
    asl                                    ; C0806E m0x0
    tax                                    ; C0806F m0x0
    jsr.w (game_mode_table,x)              ; C08070 m0x0
    jsr.w entity_render_order_reset        ; C08073 m0x0
    jsr.w entity_init_from_table           ; C08076 m0x0
    ldx.w #$0004                           ; C08079 m0x0

loc_C0807C:
    lda.w entity_type,x                    ; C0807C m0x0
    cmp.w #$000E                           ; C0807F m0x0
    bcc loc_C08089                         ; C08082 m0x0
    cmp.w #$0012                           ; C08084 m0x0
    bcc loc_C08092                         ; C08087 m0x0

loc_C08089:
    inx                                    ; C08089 m0x0
    inx                                    ; C0808A m0x0
    cpx.b $A6                              ; C0808B m0x0
    bcc loc_C0807C                         ; C0808D m0x0
    ldx.w #$0000                           ; C0808F m0x0

loc_C08092:
    stx.w $0BB4                            ; C08092 m0x0
    lda.w #$0002                           ; C08095 m0x0
    sta.w $0BB6                            ; C08098 m0x0
    sep.b #$20                             ; C0809B m0x0
    lda.w TIMEUP                           ; C0809D m1x0
    lda.b #$81                             ; C080A0 m1x0
    sta.b nmitimen_shadow                  ; C080A2 m1x0
    lda.b #$80                             ; C080A4 m1x0
    sta.w OAMADDH                          ; C080A6 m1x0
    lda.b #$01                             ; C080A9 m1x0
    sta.w MEMSEL                           ; C080AB m1x0
    rep.b #$20                             ; C080AE m1x0
    stz.b $30                              ; C080B0 m0x0
    lda.w #$0080                           ; C080B2 m0x0
    sta.b $32                              ; C080B5 m0x0
    jsr.w oam_dma_upload                   ; C080B7 m0x0
    lda.w #$FFFF                           ; C080BA m0x0
    sta.w $0BC6                            ; C080BD m0x0
    sta.w $0BC4                            ; C080C0 m0x0
    stz.w $0BC8                            ; C080C3 m0x0
    stz.w $0BCA                            ; C080C6 m0x0
    stz.w $0A88                            ; C080C9 m0x0
    stz.w $0B8A                            ; C080CC m0x0
    stz.w $0BB8                            ; C080CF m0x0
    stz.w $0BBA                            ; C080D2 m0x0
    stz.b walk_cycle_timer                 ; C080D5 m0x0
    stz.b walk_cycle_parity                ; C080D7 m0x0
    stz.b $74                              ; C080D9 m0x0
    stz.b $76                              ; C080DB m0x0
    stz.b $78                              ; C080DD m0x0
    stz.w $0C15                            ; C080DF m0x0
    stz.w $0C1B                            ; C080E2 m0x0
    stz.w $0C1D                            ; C080E5 m0x0
    lda.w #$3F60                           ; C080E8 m0x0
    sta.w $0C2D                            ; C080EB m0x0
    lda.w #$80F4                           ; C080EE m0x0
    jmp.w loc_C0A4E9                       ; C080F1 m0x0

nmi_handler_gameplay:
    ldx.w #$01FF                           ; C080F4 m0x0
    txs                                    ; C080F7 m0x0
    stz.w OAMADDL                          ; C080F8 m0x0
    lda.b game_mode                        ; C080FB m0x0
    asl                                    ; C080FD m0x0
    tax                                    ; C080FE m0x0
    jsr.w (jtbl_C08272,x)                  ; C080FF m0x0
    jsr.w cgram_upload_queue_flush         ; C08102 m0x0
    jsr.w entity_upload_pending_tiles      ; C08105 m0x0
    stz.w $0A88                            ; C08108 m0x0
    sep.b #$20                             ; C0810B m0x0
    stz.w NMITIMEN                         ; C0810D m1x0
    lda.b $4A                              ; C08110 m1x0
    bne loc_C08116                         ; C08112 m1x0
    lda.b $31                              ; C08114 m1x0

loc_C08116:
    sta.w INIDISP                          ; C08116 m1x0
    rep.b #$20                             ; C08119 m1x0
    lda.b $32                              ; C0811B m0x0
    beq loc_C08133                         ; C0811D m0x0
    bpl loc_C08127                         ; C0811F m0x0
    adc.b $30                              ; C08121 m0x0
    bne loc_C08131                         ; C08123 m0x0
    bra loc_C0812F                         ; C08125 m0x0

loc_C08127:
    clc                                    ; C08127 m0x0
    adc.b $30                              ; C08128 m0x0
    cmp.w #$0F00                           ; C0812A m0x0
    bcc loc_C08131                         ; C0812D m0x0

loc_C0812F:
    stz.b $32                              ; C0812F m0x0

loc_C08131:
    sta.b $30                              ; C08131 m0x0

loc_C08133:
    lda.b $32                              ; C08133 m0x0
    bne loc_C08147                         ; C08135 m0x0
    lda.b $8C                              ; C08137 m0x0
    ora.b $90                              ; C08139 m0x0
    bit.w #$1000                           ; C0813B m0x0
    beq loc_C08147                         ; C0813E m0x0
    lda.b $4A                              ; C08140 m0x0
    eor.w #$000A                           ; C08142 m0x0
    sta.b $4A                              ; C08145 m0x0

loc_C08147:
    lda.b $4A                              ; C08147 m0x0
    beq loc_C0814E                         ; C08149 m0x0
    jmp.w loc_C0822B                       ; C0814B m0x0

loc_C0814E:
    jsr.w camera_follow_player             ; C0814E m0x0
    jsr.w build_metatile_column_500        ; C08151 m0x0
    jsr.w build_metatile_column_580        ; C08154 m0x0
    lda.b game_mode                        ; C08157 m0x0
    asl                                    ; C08159 m0x0
    tax                                    ; C0815A m0x0
    jsr.w (jtbl_C0827A,x)                  ; C0815B m0x0
    lda.b $78                              ; C0815E m0x0
    beq loc_C08192                         ; C08160 m0x0
    dec.b $78                              ; C08162 m0x0
    bne loc_C08192                         ; C08164 m0x0
    lda.b walk_cycle_timer                 ; C08166 m0x0
    bne loc_C08192                         ; C08168 m0x0
    lda.w #$003C                           ; C0816A m0x0
    sta.b walk_cycle_timer                 ; C0816D m0x0
    stz.w $0C1F                            ; C0816F m0x0
    lda.w #$0004                           ; C08172 m0x0
    sta.w $0C21                            ; C08175 m0x0
    lda.w #$00FF                           ; C08178 m0x0
    sta.w $0C23                            ; C0817B m0x0
    sta.w $0C29                            ; C0817E m0x0
    lda.w #$0080                           ; C08181 m0x0
    sta.w $0C25                            ; C08184 m0x0
    lda.b camera_x                         ; C08187 m0x0
    sta.w $0C27                            ; C08189 m0x0
    lda.w #$0001                           ; C0818C m0x0
    sta.w $0C2B                            ; C0818F m0x0

loc_C08192:
    ldx.b walk_cycle_timer                 ; C08192 m0x0
    beq loc_C081A6                         ; C08194 m0x0
    cpx.w #$003C                           ; C08196 m0x0
    bne loc_C0819E                         ; C08199 m0x0
    jsr.w play_footstep_sound              ; C0819B m0x0

loc_C0819E:
    dec.b walk_cycle_timer                 ; C0819E m0x0
    dec.b walk_cycle_timer                 ; C081A0 m0x0
    txa                                    ; C081A2 m0x0
    ora.b walk_cycle_parity                ; C081A3 m0x0
    tax                                    ; C081A5 m0x0

loc_C081A6:
    lda.l data_C46A88,x                    ; C081A6 m0x0
    sta.b $74                              ; C081AA m0x0
    cmp.w #$8000                           ; C081AC m0x0
    ror                                    ; C081AF m0x0
    sta.b $76                              ; C081B0 m0x0
    lda.w $0C1B                            ; C081B2 m0x0
    beq loc_C081BA                         ; C081B5 m0x0
    jsr.w cgram_palette_ramp_step          ; C081B7 m0x0

loc_C081BA:
    lda.w $0C15                            ; C081BA m0x0
    beq loc_C081C2                         ; C081BD m0x0
    jsr.w particle_spawn_mode0_weather     ; C081BF m0x0

loc_C081C2:
    ldx.w #$0000                           ; C081C2 m0x0

loc_C081C5:
    jsr.w entity_update_tick               ; C081C5 m0x0
    inx                                    ; C081C8 m0x0
    inx                                    ; C081C9 m0x0
    cpx.b $A6                              ; C081CA m0x0
    bcc loc_C081C5                         ; C081CC m0x0
    lda.b $30                              ; C081CE m0x0
    ora.b $32                              ; C081D0 m0x0
    beq loc_C08209                         ; C081D2 m0x0
    lda.b $8C                              ; C081D4 m0x0
    bit.w #$0040                           ; C081D6 m0x0
    beq loc_C081F5                         ; C081D9 m0x0
    lda.w $0BB6                            ; C081DB m0x0
    inc                                    ; C081DE m0x0
    inc                                    ; C081DF m0x0
    cmp.w #$0006                           ; C081E0 m0x0
    bcc loc_C081E6                         ; C081E3 m0x0
    tdc                                    ; C081E5 m0x0

loc_C081E6:
    sta.w $0BB6                            ; C081E6 m0x0
    stz.w $0A48                            ; C081E9 m0x0
    stz.w $0A4A                            ; C081EC m0x0
    stz.w $0A4C                            ; C081EF m0x0
    stz.w $0A4E                            ; C081F2 m0x0

loc_C081F5:
    lda.b $8C                              ; C081F5 m0x0
    ora.b $90                              ; C081F7 m0x0
    bit.w #$2000                           ; C081F9 m0x0
    beq loc_C08229                         ; C081FC m0x0
    lda.b $32                              ; C081FE m0x0
    bne loc_C08229                         ; C08200 m0x0
    lda.w #$FF00                           ; C08202 m0x0
    sta.b $32                              ; C08205 m0x0
    bra loc_C08229                         ; C08207 m0x0

loc_C08209:
    lda.b game_mode                        ; C08209 m0x0
    inc                                    ; C0820B m0x0
    cmp.w #$0004                           ; C0820C m0x0
    bcc loc_C08212                         ; C0820F m0x0
    tdc                                    ; C08211 m0x0

loc_C08212:
    sta.b game_mode                        ; C08212 m0x0
    stz.b $8C                              ; C08214 m0x0
    stz.b $90                              ; C08216 m0x0
    sep.b #$20                             ; C08218 m0x0
    lda.b #$01                             ; C0821A m1x0
    sta.w NMITIMEN                         ; C0821C m1x0
    lda.b #$80                             ; C0821F m1x0
    sta.w INIDISP                          ; C08221 m1x0
    rep.b #$20                             ; C08224 m1x0
    jmp.w loc_C0805E                       ; C08226 m0x0

loc_C08229:
    inc.b $5E                              ; C08229 m0x0

loc_C0822B:
    jsr.w read_joypads                     ; C0822B m0x0
    lda.b $4A                              ; C0822E m0x0
    bne loc_C08267                         ; C08230 m0x0
    jsr.w entity_sort_draw_order           ; C08232 m0x0
    jsr.w clear_sprite_table               ; C08235 m0x0
    lda.b game_mode                        ; C08238 m0x0
    asl                                    ; C0823A m0x0
    tax                                    ; C0823B m0x0
    jsr.w (jtbl_C0828A,x)                  ; C0823C m0x0
    jsl.l $800000+(entity_build_oam_frame&$FFFF)   ; C0823F m0x0
    lda.b game_mode                        ; C08243 m0x0
    asl                                    ; C08245 m0x0
    tax                                    ; C08246 m0x0
    jsr.w (jtbl_C08282,x)                  ; C08247 m0x0
    jsr.w oam_hide_unused_sprites          ; C0824A m0x0
    jsr.w oam_dma_upload                   ; C0824D m0x0
    lda.b game_mode                        ; C08250 m0x0
    bne loc_C08267                         ; C08252 m0x0
    lda.w $0C04                            ; C08254 m0x0
    beq loc_C08267                         ; C08257 m0x0
    ldx.w #$0DB9                           ; C08259 m0x0
    ldy.w #$0C31                           ; C0825C m0x0
    lda.w #$0187                           ; C0825F m0x0
    phb                                    ; C08262 m0x0
    mvn $00,$00                            ; C08263 m0x0
    plb                                    ; C08266 m0x0

loc_C08267:
    jmp.w loc_C0A4F5                       ; C08267 m0x0

game_mode_table:
    dw mode0_level_init
    dw mode1_level_init
    dw mode2_level_init
    dw title_screen_init

jtbl_C08272:
    dw nmi_scroll_mode0
    dw nmi_scroll_mode1
    dw nmi_scroll_mode2
    dw nmi_scroll_title

jtbl_C0827A:
    dw mode0_camera_zone_update
    dw check_pending_player_attack
    dw check_pending_player_attack
    dw check_pending_player_attack

jtbl_C08282:
    dw particle_dispatch_noop
    dw mode1_particle_dispatch
    dw mode2_particle_dispatch
    dw particle_dispatch_noop

jtbl_C0828A:
    dw mode0_particle_draw_dispatch
    dw particle_dispatch_noop
    dw particle_dispatch_noop
    dw particle_dispatch_noop

mode0_level_init:
    stz.w $0BAC                            ; C08292 m0x0
    lda.w #$0001                           ; C08295 m0x0
    sta.w BGMODE                           ; C08298 m0x0
    lda.w #$1417                           ; C0829B m0x0
    sta.w TM                               ; C0829E m0x0
    lda.w #$8202                           ; C082A1 m0x0
    sta.w CGWSEL                           ; C082A4 m0x0
    lda.w #$0525                           ; C082A7 m0x0
    sta.w BG12NBA                          ; C082AA m0x0
    lda.w #$7969                           ; C082AD m0x0
    sta.w BG1SC                            ; C082B0 m0x0
    lda.w #$001C                           ; C082B3 m0x0
    sta.w BG3SC                            ; C082B6 m0x0
    jsr.w set_bg_scroll_prep               ; C082B9 m0x0
    lda.w #$0DFF                           ; C082BC m0x0
    sta.b level_width_mask                 ; C082BF m0x0
    lda.w #$011F                           ; C082C1 m0x0
    sta.b level_height_mask                ; C082C4 m0x0
    stz.b $82                              ; C082C6 m0x0
    lda.w #$0020                           ; C082C8 m0x0
    sta.b $84                              ; C082CB m0x0
    lda.w #$4860                           ; C082CD m0x0
    sta.b tilemap_a_addr                   ; C082D0 m0x0
    lda.w #$CACA                           ; C082D2 m0x0
    sta.b tilemap_a_bank                   ; C082D5 m0x0
    lda.w #$DCE0                           ; C082D7 m0x0
    sta.b tilemap_b_addr                   ; C082DA m0x0
    lda.w #$C9C9                           ; C082DC m0x0
    sta.b metatile_data_bank               ; C082DF m0x0
    lda.w #$FFFF                           ; C082E1 m0x0
    sta.b layer_parallax_mode              ; C082E4 m0x0
    stz.b $60                              ; C082E6 m0x0
    lda.w #$0048                           ; C082E8 m0x0
    sta.b camera_y                         ; C082EB m0x0
    lda.w #$0000                           ; C082ED m0x0

loc_C082F0:
    sta.b camera_x                         ; C082F0 m0x0
    jsr.w build_metatile_column_580        ; C082F2 m0x0
    jsr.w vram_upload_column_580           ; C082F5 m0x0
    lda.b camera_x                         ; C082F8 m0x0
    clc                                    ; C082FA m0x0
    adc.w #$0008                           ; C082FB m0x0
    cmp.w #$0100                           ; C082FE m0x0
    bne loc_C082F0                         ; C08301 m0x0
    sta.b camera_x                         ; C08303 m0x0
    lda.w #$1600                           ; C08305 m0x0
    sta.w VMADDL                           ; C08308 m0x0
    ldx.w #$00C5                           ; C0830B m0x0
    lda.w #$02C0                           ; C0830E m0x0
    ldy.w #$0C00                           ; C08311 m0x0
    jsr.w dma_upload_to_vram               ; C08314 m0x0
    lda.w #$1C00                           ; C08317 m0x0
    sta.w VMADDL                           ; C0831A m0x0
    ldx.w #$00CA                           ; C0831D m0x0
    lda.w #$E38E                           ; C08320 m0x0
    ldy.w #$0800                           ; C08323 m0x0
    jsr.w dma_upload_to_vram               ; C08326 m0x0
    lda.w #$2000                           ; C08329 m0x0
    sta.w VMADDL                           ; C0832C m0x0
    ldx.w #$00C9                           ; C0832F m0x0
    lda.w #$0000                           ; C08332 m0x0
    ldy.w #$5AC0                           ; C08335 m0x0
    jsr.w dma_upload_to_vram               ; C08338 m0x0
    lda.w #$5000                           ; C0833B m0x0
    sta.w VMADDL                           ; C0833E m0x0
    ldx.w #$00C9                           ; C08341 m0x0
    lda.w #$8AC0                           ; C08344 m0x0
    ldy.w #$2AA0                           ; C08347 m0x0
    jsr.w dma_upload_to_vram               ; C0834A m0x0
    lda.w #$6800                           ; C0834D m0x0
    sta.w VMADDL                           ; C08350 m0x0
    ldx.w #$00CA                           ; C08353 m0x0
    lda.w #$F38E                           ; C08356 m0x0
    ldy.w #$0800                           ; C08359 m0x0
    jsr.w dma_upload_to_vram               ; C0835C m0x0
    lda.w #$6C00                           ; C0835F m0x0
    jsr.w dma_fill_vram_zero               ; C08362 m0x0
    lda.w #$7000                           ; C08365 m0x0
    sta.w VMADDL                           ; C08368 m0x0
    ldx.w #$00CA                           ; C0836B m0x0
    lda.w #$EB8E                           ; C0836E m0x0
    ldy.w #$0800                           ; C08371 m0x0
    jsr.w dma_upload_to_vram               ; C08374 m0x0
    lda.w #$7400                           ; C08377 m0x0
    jsr.w dma_fill_vram_zero               ; C0837A m0x0
    ldy.w #$0000                           ; C0837D m0x0
    ldx.w #$0020                           ; C08380 m0x0
    lda.w #$6DA8                           ; C08383 m0x0
    jsr.w dma_upload_to_cgram              ; C08386 m0x0
    ldy.w #$0080                           ; C08389 m0x0
    ldx.w #$0020                           ; C0838C m0x0
    lda.w #$6C48                           ; C0838F m0x0
    jsr.w dma_upload_to_cgram              ; C08392 m0x0
    ldy.w #$00E0                           ; C08395 m0x0
    ldx.w #$0004                           ; C08398 m0x0
    lda.w #$6D68                           ; C0839B m0x0
    jsr.w dma_upload_to_cgram              ; C0839E m0x0
    ldy.w #$00F0                           ; C083A1 m0x0
    ldx.w #$0004                           ; C083A4 m0x0
    lda.w #$6D48                           ; C083A7 m0x0
    jsr.w dma_upload_to_cgram              ; C083AA m0x0
    jsr.w particle_table_clear             ; C083AD m0x0
    lda.w #$1016                           ; C083B0 m0x0
    sta.w $0BD4                            ; C083B3 m0x0
    inc                                    ; C083B6 m0x0
    sta.w $0BD6                            ; C083B7 m0x0
    stz.w $0BD2                            ; C083BA m0x0
    lda.w #$0080                           ; C083BD m0x0
    sta.b ptr_04                           ; C083C0 m0x0
    lda.w #$84A2                           ; C083C2 m0x0
    ldy.w #$2C41                           ; C083C5 m0x0
    ldx.w #$0010                           ; C083C8 m0x0
    jsr.w dma_setup_channel_step           ; C083CB m0x0
    lda.w #$0080                           ; C083CE m0x0
    sta.b ptr_04                           ; C083D1 m0x0
    lda.w #$84A9                           ; C083D3 m0x0
    ldy.w #$1143                           ; C083D6 m0x0
    ldx.w #$0020                           ; C083D9 m0x0
    jsr.w dma_setup_channel_step           ; C083DC m0x0
    lda.w #$0080                           ; C083DF m0x0
    sta.b ptr_04                           ; C083E2 m0x0
    lda.w #$84B0                           ; C083E4 m0x0
    ldy.w #$0900                           ; C083E7 m0x0
    ldx.w #$0030                           ; C083EA m0x0
    jsr.w dma_setup_channel_step           ; C083ED m0x0
    lda.w #$7F00                           ; C083F0 m0x0
    sta.w $0C00                            ; C083F3 m0x0
    lda.w #$007F                           ; C083F6 m0x0
    sta.l $7F00D0                          ; C083F9 m0x0
    lda.w #$003D                           ; C083FD m0x0
    sta.l $7F00D3                          ; C08400 m0x0
    lda.w #$0001                           ; C08404 m0x0
    sta.l $7F00D6                          ; C08407 m0x0
    lda.w #$0BF8                           ; C0840B m0x0
    sta.l $7F00D1                          ; C0840E m0x0
    sta.l $7F00D4                          ; C08412 m0x0
    inc                                    ; C08416 m0x0
    sta.l $7F00D7                          ; C08417 m0x0
    tdc                                    ; C0841B m0x0
    sta.l $7F00D9                          ; C0841C m0x0
    lda.l $7F00D3                          ; C08420 m0x0
    sta.w $0C02                            ; C08424 m0x0
    lda.w #$6969                           ; C08427 m0x0
    sta.w $0BF8                            ; C0842A m0x0
    lda.w #$007F                           ; C0842D m0x0
    sta.b ptr_04                           ; C08430 m0x0
    lda.w #$00D0                           ; C08432 m0x0
    ldy.w #$0740                           ; C08435 m0x0
    ldx.w #$0040                           ; C08438 m0x0
    jsr.w dma_setup_channel_step           ; C0843B m0x0
    lda.w #$FF00                           ; C0843E m0x0
    sta.w $0C04                            ; C08441 m0x0
    lda.w #$00FF                           ; C08444 m0x0
    sta.l $7F0540                          ; C08447 m0x0
    lda.w #$003D                           ; C0844B m0x0
    sta.l $7F0543                          ; C0844E m0x0
    lda.w #$0001                           ; C08452 m0x0
    sta.l $7F0546                          ; C08455 m0x0
    lda.w #$0C31                           ; C08459 m0x0
    sta.l $7F0541                          ; C0845C m0x0
    lda.w #$0D2F                           ; C08460 m0x0
    sta.l $7F0544                          ; C08463 m0x0
    lda.w #$0DB5                           ; C08467 m0x0
    sta.l $7F0547                          ; C0846A m0x0
    tdc                                    ; C0846E m0x0
    sta.l $7F0549                          ; C0846F m0x0
    ldx.w #$0186                           ; C08473 m0x0
    tdc                                    ; C08476 m0x0

loc_C08477:
    sta.w $0C31,x                          ; C08477 m0x0
    dex                                    ; C0847A m0x0
    dex                                    ; C0847B m0x0
    bpl loc_C08477                         ; C0847C m0x0
    lda.w #$007F                           ; C0847E m0x0
    sta.b ptr_04                           ; C08481 m0x0
    lda.w #$0540                           ; C08483 m0x0
    ldy.w #$0D42                           ; C08486 m0x0
    ldx.w #$0050                           ; C08489 m0x0

dma_setup_channel_step:
    sta.w A1TL0,x                          ; C0848C m0x0
    tya                                    ; C0848F m0x0
    sta.w DMAP0,x                          ; C08490 m0x0
    sep.b #$20                             ; C08493 m0x0
    lda.b ptr_04                           ; C08495 m1x0
    sta.w A1B0,x                           ; C08497 m1x0
    lda.b $05                              ; C0849A m1x0
    sta.w DASB0,x                          ; C0849C m1x0
    rep.b #$20                             ; C0849F m1x0
    rts                                    ; C084A1 m0x0

mode0_init_dma_curve_table:
    incbin "../data/01.bin":$04A2..$04B5      ; 19 bytes

camera_shake_ramp_table:
    incbin "../data/01.bin":$04B5..$04D7      ; 34 bytes

mode1_level_init:
    lda.w #$0018                           ; C084D7 m0x0
    sta.w $0BAC                            ; C084DA m0x0
    lda.w #$0001                           ; C084DD m0x0
    sta.w BGMODE                           ; C084E0 m0x0
    lda.w #$1417                           ; C084E3 m0x0
    sta.w TM                               ; C084E6 m0x0
    lda.w #$2202                           ; C084E9 m0x0
    sta.w CGWSEL                           ; C084EC m0x0
    lda.w #$0552                           ; C084EF m0x0
    sta.w BG12NBA                          ; C084F2 m0x0
    lda.w #$7079                           ; C084F5 m0x0
    sta.w BG1SC                            ; C084F8 m0x0
    sep.b #$20                             ; C084FB m0x0
    lda.b #$74                             ; C084FD m1x0
    sta.w BG3SC                            ; C084FF m1x0
    lda.b #$20                             ; C08502 m1x0
    sta.w COLDATA                          ; C08504 m1x0
    lda.b #$44                             ; C08507 m1x0
    sta.w COLDATA                          ; C08509 m1x0
    lda.b #$81                             ; C0850C m1x0
    sta.w COLDATA                          ; C0850E m1x0
    jsr.w set_bg_scroll                    ; C08511 m1x0
    lda.w #$03FF                           ; C08514 m0x0
    sta.b level_width_mask                 ; C08517 m0x0
    lda.w #$0030                           ; C08519 m0x0
    sta.b level_height_mask                ; C0851C m0x0
    lda.w #$0001                           ; C0851E m0x0
    sta.b $82                              ; C08521 m0x0
    lda.w #$0010                           ; C08523 m0x0
    sta.b $84                              ; C08526 m0x0
    lda.w #$FD80                           ; C08528 m0x0
    sta.b tilemap_a_addr                   ; C0852B m0x0
    lda.w #$C9C9                           ; C0852D m0x0
    sta.b tilemap_a_bank                   ; C08530 m0x0
    lda.w #$26A0                           ; C08532 m0x0
    sta.b tilemap_b_addr                   ; C08535 m0x0
    lda.w #$CACA                           ; C08537 m0x0
    sta.b metatile_data_bank               ; C0853A m0x0
    stz.b $60                              ; C0853C m0x0
    lda.w #$0018                           ; C0853E m0x0
    sta.b camera_y                         ; C08541 m0x0
    lda.w #$0300                           ; C08543 m0x0

loc_C08546:
    sta.b camera_x                         ; C08546 m0x0
    jsr.w build_metatile_column_580        ; C08548 m0x0
    jsr.w vram_upload_column_580           ; C0854B m0x0
    lda.b camera_x                         ; C0854E m0x0
    clc                                    ; C08550 m0x0
    adc.w #$0008                           ; C08551 m0x0
    cmp.w #$0400                           ; C08554 m0x0
    bne loc_C08546                         ; C08557 m0x0
    sta.b camera_x                         ; C08559 m0x0
    lda.w #$2000                           ; C0855B m0x0
    sta.w VMADDL                           ; C0855E m0x0
    ldx.w #$00C8                           ; C08561 m0x0
    lda.w #$6AC0                           ; C08564 m0x0
    ldy.w #$6000                           ; C08567 m0x0
    jsr.w dma_upload_to_vram               ; C0856A m0x0
    lda.w #$5000                           ; C0856D m0x0
    sta.w VMADDL                           ; C08570 m0x0
    ldx.w #$00C8                           ; C08573 m0x0
    lda.w #$C980                           ; C08576 m0x0
    ldy.w #$3620                           ; C08579 m0x0
    jsr.w dma_upload_to_vram               ; C0857C m0x0
    lda.w #$6C00                           ; C0857F m0x0
    jsr.w dma_fill_vram_zero               ; C08582 m0x0
    lda.w #$6C40                           ; C08585 m0x0
    sta.w VMADDL                           ; C08588 m0x0
    ldx.w #$00CB                           ; C0858B m0x0
    lda.w #$0000                           ; C0858E m0x0
    ldy.w #$0800                           ; C08591 m0x0
    jsr.w dma_upload_to_vram               ; C08594 m0x0
    lda.w #$7020                           ; C08597 m0x0
    sta.w VMADDL                           ; C0859A m0x0
    ldx.w #$00CC                           ; C0859D m0x0
    lda.w #$AB02                           ; C085A0 m0x0
    ldy.w #$0700                           ; C085A3 m0x0
    jsr.w dma_upload_to_vram               ; C085A6 m0x0
    lda.w #$7000                           ; C085A9 m0x0
    sta.w VMADDL                           ; C085AC m0x0
    ldx.w #$00CC                           ; C085AF m0x0
    lda.w #$AB02                           ; C085B2 m0x0
    ldy.w #$0700                           ; C085B5 m0x0
    jsr.w dma_upload_to_vram               ; C085B8 m0x0
    lda.w #$7420                           ; C085BB m0x0
    sta.w VMADDL                           ; C085BE m0x0
    ldx.w #$00CC                           ; C085C1 m0x0
    lda.w #$A402                           ; C085C4 m0x0
    ldy.w #$0700                           ; C085C7 m0x0
    jsr.w dma_upload_to_vram               ; C085CA m0x0
    lda.w #$7400                           ; C085CD m0x0
    sta.w VMADDL                           ; C085D0 m0x0
    ldx.w #$00CC                           ; C085D3 m0x0
    lda.w #$A402                           ; C085D6 m0x0
    ldy.w #$0700                           ; C085D9 m0x0
    jsr.w dma_upload_to_vram               ; C085DC m0x0
    ldx.w #$0000                           ; C085DF m0x0
    lda.w #$0068                           ; C085E2 m0x0
    sta.l $7F0000,x                        ; C085E5 m0x0
    lda.w #$00A8                           ; C085E9 m0x0
    sta.l $7F0001,x                        ; C085EC m0x0
    inx                                    ; C085F0 m0x0
    inx                                    ; C085F1 m0x0
    inx                                    ; C085F2 m0x0

loc_C085F3:
    lda.w #$0090                           ; C085F3 m0x0
    sta.l $7F0000,x                        ; C085F6 m0x0
    lda.w #$00AC                           ; C085FA m0x0
    sta.l $7F0001,x                        ; C085FD m0x0
    inx                                    ; C08601 m0x0
    inx                                    ; C08602 m0x0
    inx                                    ; C08603 m0x0
    cpx.w #$0033                           ; C08604 m0x0
    bne loc_C085F3                         ; C08607 m0x0
    ldx.w #$0040                           ; C08609 m0x0
    lda.w #$0062                           ; C0860C m0x0
    sta.l $7F0000,x                        ; C0860F m0x0
    lda.w #$84B5                           ; C08613 m0x0
    sta.l $7F0001,x                        ; C08616 m0x0
    inx                                    ; C0861A m0x0
    inx                                    ; C0861B m0x0
    inx                                    ; C0861C m0x0

loc_C0861D:
    lda.w #$0090                           ; C0861D m0x0
    sta.l $7F0000,x                        ; C08620 m0x0
    lda.w #$84B5                           ; C08624 m0x0
    sta.l $7F0001,x                        ; C08627 m0x0
    inx                                    ; C0862B m0x0
    inx                                    ; C0862C m0x0
    inx                                    ; C0862D m0x0
    cpx.w #$0070                           ; C0862E m0x0
    bne loc_C0861D                         ; C08631 m0x0
    ldx.w #$0080                           ; C08633 m0x0
    lda.w #$0070                           ; C08636 m0x0
    sta.l $7F0000,x                        ; C08639 m0x0
    lda.w #$0000                           ; C0863D m0x0
    sta.l $7F0001,x                        ; C08640 m0x0
    lda.w #$0040                           ; C08644 m0x0
    sta.l $7F0003,x                        ; C08647 m0x0
    lda.w #$0000                           ; C0864B m0x0
    sta.l $7F0004,x                        ; C0864E m0x0
    lda.w #$0001                           ; C08652 m0x0
    sta.l $7F0006,x                        ; C08655 m0x0
    lda.w #$0000                           ; C08659 m0x0
    sta.l $7F0007,x                        ; C0865C m0x0
    sta.l $7F0009,x                        ; C08660 m0x0
    sep.b #$20                             ; C08664 m0x0
    ldx.w #$2103                           ; C08666 m1x0
    stx.w DMAP1                            ; C08669 m1x0
    ldx.w #$6FA8                           ; C0866C m1x0
    stx.w A1TL1                            ; C0866F m1x0
    lda.b #$C4                             ; C08672 m1x0
    sta.w A1B1                             ; C08674 m1x0
    sta.w DASB1                            ; C08677 m1x0
    ldx.w #$0F42                           ; C0867A m1x0
    stx.w DMAP2                            ; C0867D m1x0
    ldx.w #$0000                           ; C08680 m1x0
    stx.w A1TL2                            ; C08683 m1x0
    lda.b #$7F                             ; C08686 m1x0
    sta.w A1B2                             ; C08688 m1x0
    lda.b #$00                             ; C0868B m1x0
    sta.w DASB2                            ; C0868D m1x0
    ldx.w #$1242                           ; C08690 m1x0
    stx.w DMAP3                            ; C08693 m1x0
    ldx.w #$0040                           ; C08696 m1x0
    stx.w A1TL3                            ; C08699 m1x0
    lda.b #$7F                             ; C0869C m1x0
    sta.w A1B3                             ; C0869E m1x0
    lda.b #$00                             ; C086A1 m1x0
    sta.w DASB3                            ; C086A3 m1x0
    ldx.w #$0D02                           ; C086A6 m1x0
    stx.w DMAP4                            ; C086A9 m1x0
    ldx.w #$0080                           ; C086AC m1x0
    stx.w A1TL4                            ; C086AF m1x0
    lda.b #$7F                             ; C086B2 m1x0
    sta.w A1B4                             ; C086B4 m1x0
    sta.w DASB4                            ; C086B7 m1x0
    ldx.w #$3100                           ; C086BA m1x0
    stx.w DMAP5                            ; C086BD m1x0
    ldx.w #$8793                           ; C086C0 m1x0
    stx.w A1TL5                            ; C086C3 m1x0
    lda.b #$80                             ; C086C6 m1x0
    sta.w A1B5                             ; C086C8 m1x0
    sta.w DASB5                            ; C086CB m1x0
    ldx.w #$1002                           ; C086CE m1x0
    stx.w DMAP6                            ; C086D1 m1x0
    ldx.w #$00C0                           ; C086D4 m1x0
    stx.w A1TL6                            ; C086D7 m1x0
    lda.b #$7F                             ; C086DA m1x0
    sta.w A1B6                             ; C086DC m1x0
    lda.b #$00                             ; C086DF m1x0
    sta.w DASB6                            ; C086E1 m1x0
    ldx.w #$0700                           ; C086E4 m1x0
    stx.w DMAP7                            ; C086E7 m1x0
    ldx.w #$08FE                           ; C086EA m1x0
    stx.w A1TL7                            ; C086ED m1x0
    lda.b #$7F                             ; C086F0 m1x0
    sta.w A1B7                             ; C086F2 m1x0
    rep.b #$20                             ; C086F5 m1x0
    lda.w #$7970                           ; C086F7 m0x0
    sta.l $7F08FE                          ; C086FA m0x0
    lda.w #$7940                           ; C086FE m0x0
    sta.l $7F0900                          ; C08701 m0x0
    lda.w #$6C01                           ; C08705 m0x0
    sta.l $7F0902                          ; C08708 m0x0
    tdc                                    ; C0870C m0x0
    sta.l $7F0904                          ; C0870D m0x0
    lda.w #$0068                           ; C08711 m0x0
    sta.l $7F00C0                          ; C08714 m0x0
    lda.w #$0000                           ; C08718 m0x0
    sta.l $7F00C1                          ; C0871B m0x0
    lda.w #$0002                           ; C0871F m0x0
    sta.l $7F00C3                          ; C08722 m0x0
    lda.w #$0000                           ; C08726 m0x0
    sta.l $7F00C4                          ; C08729 m0x0
    sta.l $7F00C6                          ; C0872D m0x0
    ldy.w #$0080                           ; C08731 m0x0
    ldx.w #$0020                           ; C08734 m0x0
    lda.w #$6C48                           ; C08737 m0x0
    jsr.w dma_upload_to_cgram              ; C0873A m0x0
    ldy.w #$00A0                           ; C0873D m0x0
    ldx.w #$0004                           ; C08740 m0x0
    lda.w #$6D08                           ; C08743 m0x0
    jsr.w dma_upload_to_cgram              ; C08746 m0x0
    ldy.w #$0000                           ; C08749 m0x0
    ldx.w #$0020                           ; C0874C m0x0
    lda.w #$6EA8                           ; C0874F m0x0
    jsr.w dma_upload_to_cgram              ; C08752 m0x0
    jsr.w particle_spawn_from_table        ; C08755 m0x0
    lda.w #$1E00                           ; C08758 m0x0
    sta.w VMADDL                           ; C0875B m0x0
    ldx.w #$00C5                           ; C0875E m0x0
    lda.w #$0000                           ; C08761 m0x0
    ldy.w #$0200                           ; C08764 m0x0
    jsr.w dma_upload_to_vram               ; C08767 m0x0
    jsr.w sparkle_array_init               ; C0876A m0x0
    ldy.w #$00B0                           ; C0876D m0x0
    ldx.w #$0004                           ; C08770 m0x0
    lda.w #$6D88                           ; C08773 m0x0
    jsr.w dma_upload_to_cgram              ; C08776 m0x0
    sep.b #$20                             ; C08779 m0x0
    lda.b #$E1                             ; C0877B m1x0
    sta.w CGADD                            ; C0877D m1x0
    lda.l $800000+(mode1_flash_cgram_lo&$FFFF)   ; C08780 m1x0
    sta.w CGDATA                           ; C08784 m1x0
    lda.l $800000+(mode1_flash_cgram_hi&$FFFF)   ; C08787 m1x0
    sta.w CGDATA                           ; C0878B m1x0
    rep.b #$20                             ; C0878E m1x0
    rts                                    ; C08790 m0x0

mode1_flash_cgram_lo:
    incbin "../data/01.bin":$0791..$0792      ; 1 bytes

mode1_flash_cgram_hi:
    incbin "../data/01.bin":$0792..$0798      ; 6 bytes

mode2_level_init:
    stz.w $0BAC                            ; C08798 m0x0
    lda.w #$0001                           ; C0879B m0x0
    sta.w BGMODE                           ; C0879E m0x0
    lda.w #$0413                           ; C087A1 m0x0
    sta.w TM                               ; C087A4 m0x0
    lda.w #$B402                           ; C087A7 m0x0
    sta.w CGWSEL                           ; C087AA m0x0
    lda.w #$795A                           ; C087AD m0x0
    sta.w BG1SC                            ; C087B0 m0x0
    lda.w #$0626                           ; C087B3 m0x0
    sta.w BG12NBA                          ; C087B6 m0x0
    sep.b #$20                             ; C087B9 m0x0
    lda.b #$74                             ; C087BB m1x0
    sta.w BG3SC                            ; C087BD m1x0
    lda.b #$E0                             ; C087C0 m1x0
    sta.w COLDATA                          ; C087C2 m1x0
    jsr.w set_bg_scroll                    ; C087C5 m1x0
    lda.w #$06FF                           ; C087C8 m0x0
    sta.b level_width_mask                 ; C087CB m0x0
    lda.w #$02A0                           ; C087CD m0x0
    sta.b level_height_mask                ; C087D0 m0x0
    lda.w #$FFFF                           ; C087D2 m0x0
    sta.b $82                              ; C087D5 m0x0
    lda.w #$0030                           ; C087D7 m0x0
    sta.b $84                              ; C087DA m0x0
    lda.w #$5760                           ; C087DC m0x0
    sta.b tilemap_a_addr                   ; C087DF m0x0
    lda.w #$CACA                           ; C087E1 m0x0
    sta.b tilemap_a_bank                   ; C087E4 m0x0
    lda.w #$B560                           ; C087E6 m0x0
    sta.b tilemap_b_addr                   ; C087E9 m0x0
    lda.w #$C9C9                           ; C087EB m0x0
    sta.b metatile_data_bank               ; C087EE m0x0
    stz.b layer_parallax_mode              ; C087F0 m0x0
    stz.b camera_y_lookahead               ; C087F2 m0x0
    stz.b $60                              ; C087F4 m0x0
    lda.w #$0015                           ; C087F6 m0x0
    sta.b camera_y                         ; C087F9 m0x0
    lda.w #$0600                           ; C087FB m0x0

loc_C087FE:
    sta.b camera_x                         ; C087FE m0x0
    jsr.w build_metatile_column_580        ; C08800 m0x0
    jsr.w vram_upload_column_580           ; C08803 m0x0
    lda.b camera_x                         ; C08806 m0x0
    clc                                    ; C08808 m0x0
    adc.w #$0008                           ; C08809 m0x0
    cmp.w #$0700                           ; C0880C m0x0
    bne loc_C087FE                         ; C0880F m0x0
    sta.b camera_x                         ; C08811 m0x0
    lda.w #$2000                           ; C08813 m0x0
    sta.w VMADDL                           ; C08816 m0x0
    ldx.w #$00C7                           ; C08819 m0x0
    lda.w #$0342                           ; C0881C m0x0
    ldy.w #$6FC0                           ; C0881F m0x0
    jsr.w dma_upload_to_vram               ; C08822 m0x0
    lda.w #$5800                           ; C08825 m0x0
    sta.w VMADDL                           ; C08828 m0x0
    ldx.w #$00CB                           ; C0882B m0x0
    lda.w #$1000                           ; C0882E m0x0
    ldy.w #$0800                           ; C08831 m0x0
    jsr.w dma_upload_to_vram               ; C08834 m0x0
    lda.w #$5C00                           ; C08837 m0x0
    jsr.w dma_fill_vram_zero               ; C0883A m0x0
    lda.w #$6000                           ; C0883D m0x0
    sta.w VMADDL                           ; C08840 m0x0
    ldx.w #$00C7                           ; C08843 m0x0
    lda.w #$DF82                           ; C08846 m0x0
    ldy.w #$19E0                           ; C08849 m0x0
    jsr.w dma_upload_to_vram               ; C0884C m0x0
    lda.w #$7400                           ; C0884F m0x0
    sta.w VMADDL                           ; C08852 m0x0
    ldx.w #$00CB                           ; C08855 m0x0
    lda.w #$0800                           ; C08858 m0x0
    ldy.w #$0800                           ; C0885B m0x0
    jsr.w dma_upload_to_vram               ; C0885E m0x0
    ldy.w #$0080                           ; C08861 m0x0
    ldx.w #$0020                           ; C08864 m0x0
    lda.w #$6C48                           ; C08867 m0x0
    jsr.w dma_upload_to_cgram              ; C0886A m0x0
    ldy.w #$00C0                           ; C0886D m0x0
    ldx.w #$0010                           ; C08870 m0x0
    lda.w #$6C48                           ; C08873 m0x0
    jsr.w dma_upload_to_cgram              ; C08876 m0x0
    ldy.w #$00E0                           ; C08879 m0x0
    ldx.w #$0004                           ; C0887C m0x0
    lda.w #$6CC8                           ; C0887F m0x0
    jsr.w dma_upload_to_cgram              ; C08882 m0x0
    ldy.w #$0000                           ; C08885 m0x0
    ldx.w #$0020                           ; C08888 m0x0
    lda.w #$6FE3                           ; C0888B m0x0
    jsr.w dma_upload_to_cgram              ; C0888E m0x0
    jsr.w particle_spawn_random            ; C08891 m0x0
    lda.w #$0080                           ; C08894 m0x0
    sta.b ptr_04                           ; C08897 m0x0
    lda.w #$88A6                           ; C08899 m0x0
    ldy.w #$2C00                           ; C0889C m0x0
    ldx.w #$0010                           ; C0889F m0x0
    jsr.w dma_setup_channel_step           ; C088A2 m0x0
    rts                                    ; C088A5 m0x0

mode2_init_dma_table:
    incbin "../data/01.bin":$08A6..$08AB      ; 5 bytes

title_screen_init:
    stz.w $0BAC                            ; C088AB m0x0
    lda.w #$0009                           ; C088AE m0x0
    sta.w BGMODE                           ; C088B1 m0x0
    lda.w #$0013                           ; C088B4 m0x0
    sta.w TM                               ; C088B7 m0x0
    lda.w #$1202                           ; C088BA m0x0
    sta.w CGWSEL                           ; C088BD m0x0
    lda.w #$7958                           ; C088C0 m0x0
    sta.w BG1SC                            ; C088C3 m0x0
    lda.w #$0626                           ; C088C6 m0x0
    sta.w BG12NBA                          ; C088C9 m0x0
    sep.b #$20                             ; C088CC m0x0
    lda.b #$5C                             ; C088CE m1x0
    sta.w BG3SC                            ; C088D0 m1x0
    lda.b #$E0                             ; C088D3 m1x0
    sta.w COLDATA                          ; C088D5 m1x0
    jsr.w set_bg_scroll                    ; C088D8 m1x0
    lda.w #$02FF                           ; C088DB m0x0
    sta.b level_width_mask                 ; C088DE m0x0
    lda.w #$02A0                           ; C088E0 m0x0
    sta.b level_height_mask                ; C088E3 m0x0
    lda.w #$FFFF                           ; C088E5 m0x0
    sta.b $82                              ; C088E8 m0x0
    lda.w #$0030                           ; C088EA m0x0
    sta.b $84                              ; C088ED m0x0
    lda.w #$8714                           ; C088EF m0x0
    sta.b tilemap_a_addr                   ; C088F2 m0x0
    lda.w #$CECE                           ; C088F4 m0x0
    sta.b tilemap_a_bank                   ; C088F7 m0x0
    lda.w #$37A0                           ; C088F9 m0x0
    sta.b tilemap_b_addr                   ; C088FC m0x0
    lda.w #$CACA                           ; C088FE m0x0
    sta.b metatile_data_bank               ; C08901 m0x0
    lda.w #$FFFF                           ; C08903 m0x0
    sta.b layer_parallax_mode              ; C08906 m0x0
    stz.b camera_y_lookahead               ; C08908 m0x0
    stz.b $60                              ; C0890A m0x0
    lda.w #$0088                           ; C0890C m0x0
    sta.b camera_y                         ; C0890F m0x0
    lda.w #$0000                           ; C08911 m0x0

loc_C08914:
    sta.b camera_x                         ; C08914 m0x0
    jsr.w build_metatile_column_580        ; C08916 m0x0
    jsr.w vram_upload_column_580           ; C08919 m0x0
    lda.b camera_x                         ; C0891C m0x0
    clc                                    ; C0891E m0x0
    adc.w #$0008                           ; C0891F m0x0
    cmp.w #$0100                           ; C08922 m0x0
    bne loc_C08914                         ; C08925 m0x0
    sta.b camera_x                         ; C08927 m0x0
    lda.w #$1800                           ; C08929 m0x0
    jsr.w dma_fill_vram_zero               ; C0892C m0x0
    lda.w #$1C00                           ; C0892F m0x0
    sta.w VMADDL                           ; C08932 m0x0
    ldx.w #$00CB                           ; C08935 m0x0
    lda.w #$3000                           ; C08938 m0x0
    ldy.w #$0800                           ; C0893B m0x0
    jsr.w dma_upload_to_vram               ; C0893E m0x0
    lda.w #$2000                           ; C08941 m0x0
    sta.w VMADDL                           ; C08944 m0x0
    ldx.w #$00C8                           ; C08947 m0x0
    lda.w #$0000                           ; C0894A m0x0
    ldy.w #$6AC0                           ; C0894D m0x0
    jsr.w dma_upload_to_vram               ; C08950 m0x0
    lda.w #$5800                           ; C08953 m0x0
    sta.w VMADDL                           ; C08956 m0x0
    ldx.w #$00CB                           ; C08959 m0x0
    lda.w #$3800                           ; C0895C m0x0
    ldy.w #$0800                           ; C0895F m0x0
    jsr.w dma_upload_to_vram               ; C08962 m0x0
    lda.w #$5C00                           ; C08965 m0x0
    sta.w VMADDL                           ; C08968 m0x0
    ldx.w #$00CB                           ; C0896B m0x0
    lda.w #$2800                           ; C0896E m0x0
    ldy.w #$0800                           ; C08971 m0x0
    jsr.w dma_upload_to_vram               ; C08974 m0x0
    lda.w #$6000                           ; C08977 m0x0
    sta.w VMADDL                           ; C0897A m0x0
    ldx.w #$00C9                           ; C0897D m0x0
    lda.w #$5AC0                           ; C08980 m0x0
    ldy.w #$3000                           ; C08983 m0x0
    jsr.w dma_upload_to_vram               ; C08986 m0x0
    ldy.w #$0080                           ; C08989 m0x0
    ldx.w #$0020                           ; C0898C m0x0
    lda.w #$6C48                           ; C0898F m0x0
    jsr.w dma_upload_to_cgram              ; C08992 m0x0
    ldy.w #$00C0                           ; C08995 m0x0
    ldx.w #$0010                           ; C08998 m0x0
    lda.w #$6C48                           ; C0899B m0x0
    jsr.w dma_upload_to_cgram              ; C0899E m0x0
    ldy.w #$00A0                           ; C089A1 m0x0
    ldx.w #$0004                           ; C089A4 m0x0
    lda.w #$7443                           ; C089A7 m0x0
    jsr.w dma_upload_to_cgram              ; C089AA m0x0
    ldy.w #$0000                           ; C089AD m0x0
    ldx.w #$0020                           ; C089B0 m0x0
    lda.w #$7343                           ; C089B3 m0x0
    jsr.w dma_upload_to_cgram              ; C089B6 m0x0
    lda.w #$0040                           ; C089B9 m0x0
    sta.l $7F00D0                          ; C089BC m0x0
    lda.w #$0038                           ; C089C0 m0x0
    sta.l $7F00D3                          ; C089C3 m0x0
    lda.w #$0001                           ; C089C7 m0x0
    sta.l $7F00D6                          ; C089CA m0x0
    lda.w #$0BF8                           ; C089CE m0x0
    sta.l $7F00D1                          ; C089D1 m0x0
    inc                                    ; C089D5 m0x0
    sta.l $7F00D4                          ; C089D6 m0x0
    inc                                    ; C089DA m0x0
    sta.l $7F00D7                          ; C089DB m0x0
    tdc                                    ; C089DF m0x0
    sta.l $7F00D9                          ; C089E0 m0x0
    lda.l $7F00D0                          ; C089E4 m0x0
    sta.w $0C00                            ; C089E8 m0x0
    lda.l $7F00D3                          ; C089EB m0x0
    sta.w $0C02                            ; C089EF m0x0
    lda.w #$1858                           ; C089F2 m0x0
    sta.w $0BF8                            ; C089F5 m0x0
    sta.w $0BFC                            ; C089F8 m0x0
    lda.w #$0058                           ; C089FB m0x0
    sta.w $0BFA                            ; C089FE m0x0
    sta.w $0BFE                            ; C08A01 m0x0
    lda.w #$007F                           ; C08A04 m0x0
    sta.b ptr_04                           ; C08A07 m0x0
    lda.w #$00D0                           ; C08A09 m0x0
    ldy.w #$0740                           ; C08A0C m0x0
    ldx.w #$0010                           ; C08A0F m0x0
    jsr.w dma_setup_channel_step           ; C08A12 m0x0
    lda.w #$0080                           ; C08A15 m0x0
    sta.b ptr_04                           ; C08A18 m0x0
    lda.w #$8A5A                           ; C08A1A m0x0
    ldy.w #$2C01                           ; C08A1D m0x0
    ldx.w #$0020                           ; C08A20 m0x0
    jsr.w dma_setup_channel_step           ; C08A23 m0x0
    lda.w #$0080                           ; C08A26 m0x0
    sta.b ptr_04                           ; C08A29 m0x0
    lda.w #$8A61                           ; C08A2B m0x0
    ldy.w #$0900                           ; C08A2E m0x0
    ldx.w #$0030                           ; C08A31 m0x0
    jsr.w dma_setup_channel_step           ; C08A34 m0x0
    lda.w #$0080                           ; C08A37 m0x0
    sta.b ptr_04                           ; C08A3A m0x0
    lda.w #$8A66                           ; C08A3C m0x0
    ldy.w #$1143                           ; C08A3F m0x0
    ldx.w #$0040                           ; C08A42 m0x0
    jsr.w dma_setup_channel_step           ; C08A45 m0x0
    lda.w #$0080                           ; C08A48 m0x0
    sta.b ptr_04                           ; C08A4B m0x0
    lda.w #$8A6D                           ; C08A4D m0x0
    ldy.w #$0D43                           ; C08A50 m0x0
    ldx.w #$0050                           ; C08A53 m0x0
    jsr.w dma_setup_channel_step           ; C08A56 m0x0
    rts                                    ; C08A59 m0x0

mode0_zone_hdma_table:
    incbin "../data/01.bin":$0A5A..$0A74      ; 26 bytes

mode0_camera_zone_update:
    stz.w $0C04                            ; C08A74 m0x0
    lda.w $0C1F                            ; C08A77 m0x0
    bne loc_C08A9F                         ; C08A7A m0x0
    lda.w #$00B4                           ; C08A7C m0x0
    sta.w $0C1F                            ; C08A7F m0x0
    lda.w #$0003                           ; C08A82 m0x0
    sta.w $0C21                            ; C08A85 m0x0
    lda.w #$00FF                           ; C08A88 m0x0
    sta.w $0C23                            ; C08A8B m0x0
    sta.w $0C29                            ; C08A8E m0x0
    lda.w #$0070                           ; C08A91 m0x0
    sta.w $0C25                            ; C08A94 m0x0
    lda.b camera_x                         ; C08A97 m0x0
    sta.w $0C27                            ; C08A99 m0x0
    stz.w $0C2B                            ; C08A9C m0x0

loc_C08A9F:
    dec.w $0C1F                            ; C08A9F m0x0
    lda.b camera_x                         ; C08AA2 m0x0
    cmp.w #$0100                           ; C08AA4 m0x0
    bcs loc_C08AAD                         ; C08AA7 m0x0
    stz.b $78                              ; C08AA9 m0x0
    bra loc_C08AD3                         ; C08AAB m0x0

loc_C08AAD:
    ldy.b $78                              ; C08AAD m0x0
    bne loc_C08AD5                         ; C08AAF m0x0
    ldy.w $0C0C                            ; C08AB1 m0x0
    bne loc_C08AD5                         ; C08AB4 m0x0
    sec                                    ; C08AB6 m0x0
    sbc.w #$0400                           ; C08AB7 m0x0
    bmi loc_C08AC7                         ; C08ABA m0x0
    cmp.w #$0100                           ; C08ABC m0x0
    bcc loc_C08AC6                         ; C08ABF m0x0
    lda.w #$005A                           ; C08AC1 m0x0
    bra loc_C08AD1                         ; C08AC4 m0x0

loc_C08AC6:
    tdc                                    ; C08AC6 m0x0

loc_C08AC7:
    eor.w #$FFFF                           ; C08AC7 m0x0
    inc                                    ; C08ACA m0x0
    lsr                                    ; C08ACB m0x0
    lsr                                    ; C08ACC m0x0
    clc                                    ; C08ACD m0x0
    adc.w #$0078                           ; C08ACE m0x0

loc_C08AD1:
    sta.b $78                              ; C08AD1 m0x0

loc_C08AD3:
    stz.b walk_cycle_parity                ; C08AD3 m0x0

loc_C08AD5:
    lda.b camera_x                         ; C08AD5 m0x0
    tax                                    ; C08AD7 m0x0
    lsr                                    ; C08AD8 m0x0
    lsr                                    ; C08AD9 m0x0
    sta.w $0BF0                            ; C08ADA m0x0
    lda.b camera_y                         ; C08ADD m0x0
    lsr                                    ; C08ADF m0x0
    lsr                                    ; C08AE0 m0x0
    sta.w $0BF2                            ; C08AE1 m0x0
    cpx.w #$0090                           ; C08AE4 m0x0
    bcs loc_C08AEC                         ; C08AE7 m0x0
    jmp.w loc_C08B80                       ; C08AE9 m0x0

loc_C08AEC:
    cpx.w #$0180                           ; C08AEC m0x0
    bcs loc_C08AF4                         ; C08AEF m0x0
    jmp.w loc_C08E2B                       ; C08AF1 m0x0

loc_C08AF4:
    cpx.w #$02E0                           ; C08AF4 m0x0
    bcs loc_C08AFC                         ; C08AF7 m0x0
    jmp.w loc_C08E39                       ; C08AF9 m0x0

loc_C08AFC:
    cpx.w #$0438                           ; C08AFC m0x0
    bcs loc_C08B04                         ; C08AFF m0x0
    jmp.w loc_C08B2C                       ; C08B01 m0x0

loc_C08B04:
    cpx.w #$05E0                           ; C08B04 m0x0
    bcs loc_C08B0C                         ; C08B07 m0x0
    jmp.w loc_C08B9D                       ; C08B09 m0x0

loc_C08B0C:
    cpx.w #$0800                           ; C08B0C m0x0
    bcs loc_C08B14                         ; C08B0F m0x0
    jmp.w loc_C08B98                       ; C08B11 m0x0

loc_C08B14:
    cpx.w #$0980                           ; C08B14 m0x0
    bcs loc_C08B1C                         ; C08B17 m0x0
    jmp.w loc_C08B2C                       ; C08B19 m0x0

loc_C08B1C:
    cpx.w #$0B00                           ; C08B1C m0x0
    bcs loc_C08B24                         ; C08B1F m0x0
    jmp.w loc_C08E2B                       ; C08B21 m0x0

loc_C08B24:
    cpx.w #$0E00                           ; C08B24 m0x0
    bcs loc_C08B2C                         ; C08B27 m0x0
    jmp.w loc_C08E39                       ; C08B29 m0x0

loc_C08B2C:
    lda.w entity_flags                     ; C08B2C m0x0
    and.w #$CFFF                           ; C08B2F m0x0
    ora.w #$2000                           ; C08B32 m0x0
    sta.w entity_flags                     ; C08B35 m0x0
    lda.w $0C1B                            ; C08B38 m0x0
    beq loc_C08B49                         ; C08B3B m0x0
    lda.w #$FFFF                           ; C08B3D m0x0
    sta.w $0C1B                            ; C08B40 m0x0
    lda.w #$0080                           ; C08B43 m0x0
    sta.w $0C1D                            ; C08B46 m0x0

loc_C08B49:
    lda.w #$1016                           ; C08B49 m0x0
    sta.w $0BD8                            ; C08B4C m0x0
    lda.w #$1012                           ; C08B4F m0x0
    sta.w $0BDA                            ; C08B52 m0x0
    lda.w #$0100                           ; C08B55 m0x0
    sta.w $0C00                            ; C08B58 m0x0
    lda.l $7F00D3                          ; C08B5B m0x0
    and.w #$FF00                           ; C08B5F m0x0
    ora.w #$003C                           ; C08B62 m0x0
    sta.w $0C02                            ; C08B65 m0x0
    stz.w $0C04                            ; C08B68 m0x0
    ldy.w #$00C1                           ; C08B6B m0x0
    ldx.w #$0000                           ; C08B6E m0x0
    lda.w #$6C6C                           ; C08B71 m0x0
    stz.w $0C0C                            ; C08B74 m0x0
    stz.w $0C11                            ; C08B77 m0x0
    stz.w $0C13                            ; C08B7A m0x0
    jmp.w loc_C08E7F                       ; C08B7D m0x0

loc_C08B80:
    lda.w #$1017                           ; C08B80 m0x0
    sta.w $0BD8                            ; C08B83 m0x0
    lda.w #$1013                           ; C08B86 m0x0
    sta.w $0BDA                            ; C08B89 m0x0
    lda.b camera_y                         ; C08B8C m0x0
    adc.w #$0088                           ; C08B8E m0x0
    tay                                    ; C08B91 m0x0
    lda.w #$6969                           ; C08B92 m0x0
    jmp.w loc_C08E7F                       ; C08B95 m0x0

loc_C08B98:
    lda.w #$0700                           ; C08B98 m0x0
    bra loc_C08BA0                         ; C08B9B m0x0

loc_C08B9D:
    lda.w #$0500                           ; C08B9D m0x0

loc_C08BA0:
    sta.w $0C0F                            ; C08BA0 m0x0
    lda.w #$1017                           ; C08BA3 m0x0
    sta.w $0BD8                            ; C08BA6 m0x0
    lda.w #$1417                           ; C08BA9 m0x0
    sta.w $0BDA                            ; C08BAC m0x0
    ldy.w #$0020                           ; C08BAF m0x0
    lda.w entity_state                     ; C08BB2 m0x0
    and.w #$FFFC                           ; C08BB5 m0x0
    cmp.w #$0008                           ; C08BB8 m0x0
    bne loc_C08BC0                         ; C08BBB m0x0
    ldy.w #$0040                           ; C08BBD m0x0

loc_C08BC0:
    sty.b $18                              ; C08BC0 m0x0
    lda.b camera_x                         ; C08BC2 m0x0
    sec                                    ; C08BC4 m0x0
    sbc.w $0C0F                            ; C08BC5 m0x0
    sta.b $20                              ; C08BC8 m0x0
    bpl loc_C08BD0                         ; C08BCA m0x0
    eor.w #$FFFF                           ; C08BCC m0x0
    inc                                    ; C08BCF m0x0

loc_C08BD0:
    sta.b $1A                              ; C08BD0 m0x0
    cmp.b $18                              ; C08BD2 m0x0
    bcs loc_C08BE4                         ; C08BD4 m0x0
    lda.w $0C11                            ; C08BD6 m0x0
    bne loc_C08BE4                         ; C08BD9 m0x0
    lda.w #$0097                           ; C08BDB m0x0
    sta.w $0C11                            ; C08BDE m0x0
    stz.w $0C13                            ; C08BE1 m0x0

loc_C08BE4:
    lda.w $0C0D                            ; C08BE4 m0x0
    asl                                    ; C08BE7 m0x0
    tax                                    ; C08BE8 m0x0
    lda.b camera_x                         ; C08BE9 m0x0
    sec                                    ; C08BEB m0x0
    sbc.w $0C0F                            ; C08BEC m0x0
    bpl loc_C08BF8                         ; C08BEF m0x0
    cmp.w #$FF20                           ; C08BF1 m0x0
    bcc loc_C08BFD                         ; C08BF4 m0x0
    bra loc_C08C09                         ; C08BF6 m0x0

loc_C08BF8:
    cmp.w #$0100                           ; C08BF8 m0x0
    bcc loc_C08C09                         ; C08BFB m0x0

loc_C08BFD:
    lda.w #$0100                           ; C08BFD m0x0
    stz.w $0C0C                            ; C08C00 m0x0
    stz.w $0C11                            ; C08C03 m0x0
    stz.w $0C13                            ; C08C06 m0x0

loc_C08C09:
    sta.b ptr_04                           ; C08C09 m0x0
    ldy.w $0C11                            ; C08C0B m0x0
    beq loc_C08C21                         ; C08C0E m0x0
    ldy.w $0C0C                            ; C08C10 m0x0
    beq loc_C08C21                         ; C08C13 m0x0
    clc                                    ; C08C15 m0x0
    adc.l data_C46788,x                    ; C08C16 m0x0
    bmi loc_C08C24                         ; C08C1A m0x0
    cmp.w #$00D9                           ; C08C1C m0x0
    bcc loc_C08C24                         ; C08C1F m0x0

loc_C08C21:
    lda.w #$00D8                           ; C08C21 m0x0

loc_C08C24:
    clc                                    ; C08C24 m0x0
    adc.w #$0028                           ; C08C25 m0x0
    sta.w $0BF4                            ; C08C28 m0x0
    lda.w $0C0C                            ; C08C2B m0x0
    ldy.w $0C11                            ; C08C2E m0x0
    beq loc_C08C56                         ; C08C31 m0x0
    bpl loc_C08C4B                         ; C08C33 m0x0
    inc.w $0C11                            ; C08C35 m0x0
    tay                                    ; C08C38 m0x0
    beq loc_C08C47                         ; C08C39 m0x0
    ldy.w #$FF88                           ; C08C3B m0x0
    sty.w $0C11                            ; C08C3E m0x0
    sec                                    ; C08C41 m0x0
    sbc.w #$0100                           ; C08C42 m0x0
    bpl loc_C08C48                         ; C08C45 m0x0

loc_C08C47:
    tdc                                    ; C08C47 m0x0

loc_C08C48:
    jmp.w loc_C08CF7                       ; C08C48 m0x0

loc_C08C4B:
    dec.w $0C11                            ; C08C4B m0x0
    dec.w $0C11                            ; C08C4E m0x0
    cmp.w #$3F00                           ; C08C51 m0x0
    bcc loc_C08C59                         ; C08C54 m0x0

loc_C08C56:
    jmp.w loc_C08CFA                       ; C08C56 m0x0

loc_C08C59:
    cmp.w #$2000                           ; C08C59 m0x0
    bcc loc_C08C62                         ; C08C5C m0x0
    clc                                    ; C08C5E m0x0
    adc.w #$0100                           ; C08C5F m0x0

loc_C08C62:
    adc.w #$0100                           ; C08C62 m0x0
    cmp.w #$3F00                           ; C08C65 m0x0
    bcc loc_C08C80                         ; C08C68 m0x0
    ldy.w $0C15                            ; C08C6A m0x0
    bne loc_C08C80                         ; C08C6D m0x0
    tay                                    ; C08C6F m0x0
    lda.w #$000A                           ; C08C70 m0x0
    sta.w $0C15                            ; C08C73 m0x0
    lda.w $0C0F                            ; C08C76 m0x0
    adc.w #$007F                           ; C08C79 m0x0
    sta.w $0C17                            ; C08C7C m0x0
    tya                                    ; C08C7F m0x0

loc_C08C80:
    cmp.w #$4000                           ; C08C80 m0x0
    bcs loc_C08CE6                         ; C08C83 m0x0
    cmp.w #$2000                           ; C08C85 m0x0
    bcs loc_C08CC1                         ; C08C88 m0x0
    cmp.w #$0100                           ; C08C8A m0x0
    bne loc_C08CC1                         ; C08C8D m0x0
    tay                                    ; C08C8F m0x0
    jsr.w play_zone_transition_sound       ; C08C90 m0x0
    tya                                    ; C08C93 m0x0
    stz.w $0C1F                            ; C08C94 m0x0
    ldy.w #$0010                           ; C08C97 m0x0
    sty.w $0C21                            ; C08C9A m0x0
    ldy.w #$01FF                           ; C08C9D m0x0
    sty.w $0C23                            ; C08CA0 m0x0
    ldy.w #$0100                           ; C08CA3 m0x0
    sty.w $0C25                            ; C08CA6 m0x0
    tay                                    ; C08CA9 m0x0
    lda.w $0C0F                            ; C08CAA m0x0
    clc                                    ; C08CAD m0x0
    adc.w #$0040                           ; C08CAE m0x0
    sta.w $0C27                            ; C08CB1 m0x0
    lda.w #$007F                           ; C08CB4 m0x0
    sta.w $0C29                            ; C08CB7 m0x0
    lda.w #$0003                           ; C08CBA m0x0
    sta.w $0C2B                            ; C08CBD m0x0
    tya                                    ; C08CC0 m0x0

loc_C08CC1:
    cmp.w #$2C00                           ; C08CC1 m0x0
    bcc loc_C08CF7                         ; C08CC4 m0x0
    sta.w $0C0C                            ; C08CC6 m0x0
    ldy.b $1A                              ; C08CC9 m0x0
    cpy.w #$0030                           ; C08CCB m0x0
    bcs loc_C08CFA                         ; C08CCE m0x0
    lda.w entity_state                     ; C08CD0 m0x0
    and.w #$FFFC                           ; C08CD3 m0x0
    cmp.w #$0014                           ; C08CD6 m0x0
    beq loc_C08CFA                         ; C08CD9 m0x0
    and.w #$0002                           ; C08CDB m0x0
    ora.w #$0014                           ; C08CDE m0x0
    sta.w entity_state                     ; C08CE1 m0x0
    bra loc_C08CFA                         ; C08CE4 m0x0

loc_C08CE6:
    lda.w #$003C                           ; C08CE6 m0x0
    sta.b walk_cycle_timer                 ; C08CE9 m0x0
    lda.w #$0080                           ; C08CEB m0x0
    sta.b walk_cycle_parity                ; C08CEE m0x0
    stz.b $74                              ; C08CF0 m0x0
    stz.b $76                              ; C08CF2 m0x0
    lda.w #$3F00                           ; C08CF4 m0x0

loc_C08CF7:
    sta.w $0C0C                            ; C08CF7 m0x0

loc_C08CFA:
    ldy.b $1A                              ; C08CFA m0x0
    cpy.w #$0030                           ; C08CFC m0x0
    bcc loc_C08D27                         ; C08CFF m0x0
    lda.w entity_flags                     ; C08D01 m0x0
    and.w #$CFFF                           ; C08D04 m0x0
    ora.w #$2000                           ; C08D07 m0x0
    ldy.b $20                              ; C08D0A m0x0
    bpl loc_C08D11                         ; C08D0C m0x0
    ora.w #$3000                           ; C08D0E m0x0

loc_C08D11:
    sta.w entity_flags                     ; C08D11 m0x0
    lda.w $0C1B                            ; C08D14 m0x0
    beq loc_C08D48                         ; C08D17 m0x0
    lda.w #$FFFF                           ; C08D19 m0x0
    sta.w $0C1B                            ; C08D1C m0x0
    lda.w #$0080                           ; C08D1F m0x0
    sta.w $0C1D                            ; C08D22 m0x0
    bra loc_C08D48                         ; C08D25 m0x0

loc_C08D27:
    lda.w entity_flags                     ; C08D27 m0x0
    and.w #$CFFF                           ; C08D2A m0x0
    ora.w #$2000                           ; C08D2D m0x0
    sta.w entity_flags                     ; C08D30 m0x0
    lda.w $0C0C                            ; C08D33 m0x0
    cmp.w #$2E00                           ; C08D36 m0x0
    bne loc_C08D48                         ; C08D39 m0x0
    lda.w $0C11                            ; C08D3B m0x0
    bpl loc_C08D45                         ; C08D3E m0x0
    ldy.w $0C1D                            ; C08D40 m0x0
    beq loc_C08D48                         ; C08D43 m0x0

loc_C08D45:
    sta.w $0C1B                            ; C08D45 m0x0

loc_C08D48:
    lda.b camera_y                         ; C08D48 m0x0
    sec                                    ; C08D4A m0x0
    sbc.w #$0076                           ; C08D4B m0x0
    clc                                    ; C08D4E m0x0
    adc.l data_C46888,x                    ; C08D4F m0x0
    bpl loc_C08D5F                         ; C08D53 m0x0
    cmp.w #$FFFF                           ; C08D55 m0x0
    beq loc_C08D67                         ; C08D58 m0x0
    lda.w #$FFFF                           ; C08D5A m0x0
    bra loc_C08D67                         ; C08D5D m0x0

loc_C08D5F:
    cmp.w #$00C2                           ; C08D5F m0x0
    bcc loc_C08D67                         ; C08D62 m0x0
    lda.w #$00C1                           ; C08D64 m0x0

loc_C08D67:
    tay                                    ; C08D67 m0x0
    inc                                    ; C08D68 m0x0
    sta.b $18                              ; C08D69 m0x0
    lda.l data_C46B88,x                    ; C08D6B m0x0
    sta.w $0C0A                            ; C08D6F m0x0
    lda.w #$0012                           ; C08D72 m0x0
    sta.w $0C08                            ; C08D75 m0x0
    lda.b camera_y                         ; C08D78 m0x0
    sec                                    ; C08D7A m0x0
    sbc.w #$007F                           ; C08D7B m0x0
    clc                                    ; C08D7E m0x0
    adc.l data_C46808,x                    ; C08D7F m0x0
    sta.w $0BF6                            ; C08D83 m0x0
    lda.l data_C46908,x                    ; C08D86 m0x0
    asl                                    ; C08D8A m0x0
    bit.w $0C11                            ; C08D8B m0x0
    bmi loc_C08D94                         ; C08D8E m0x0
    eor.w #$FFFF                           ; C08D90 m0x0
    inc                                    ; C08D93 m0x0

loc_C08D94:
    sta.b $0C                              ; C08D94 m0x0
    clc                                    ; C08D96 m0x0
    adc.b ptr_04                           ; C08D97 m0x0
    sta.b ptr_04                           ; C08D99 m0x0
    ldx.w #$003E                           ; C08D9B m0x0
    lda.w #$007F                           ; C08D9E m0x0
    sec                                    ; C08DA1 m0x0
    sbc.b $18                              ; C08DA2 m0x0
    beq loc_C08DAB                         ; C08DA4 m0x0
    bpl loc_C08DAE                         ; C08DA6 m0x0
    ldx.w #$003C                           ; C08DA8 m0x0

loc_C08DAB:
    lda.w #$0001                           ; C08DAB m0x0

loc_C08DAE:
    cmp.w #$0080                           ; C08DAE m0x0
    bcc loc_C08DB6                         ; C08DB1 m0x0
    lda.w #$007F                           ; C08DB3 m0x0

loc_C08DB6:
    xba                                    ; C08DB6 m0x0
    sta.w $0C00                            ; C08DB7 m0x0
    stx.b $1A                              ; C08DBA m0x0
    lda.l $7F00D3                          ; C08DBC m0x0
    and.w #$FF00                           ; C08DC0 m0x0
    ora.b $1A                              ; C08DC3 m0x0
    sta.w $0C02                            ; C08DC5 m0x0
    tya                                    ; C08DC8 m0x0
    inc                                    ; C08DC9 m0x0
    asl                                    ; C08DCA m0x0
    adc.w #$0C31                           ; C08DCB m0x0
    sta.w $0C06                            ; C08DCE m0x0
    lda.w $0C11                            ; C08DD1 m0x0
    bmi loc_C08DDE                         ; C08DD4 m0x0
    lda.w $0C0C                            ; C08DD6 m0x0
    cmp.w #$3000                           ; C08DD9 m0x0
    bcc loc_C08DE8                         ; C08DDC m0x0

loc_C08DDE:
    lda.w $0C13                            ; C08DDE m0x0
    clc                                    ; C08DE1 m0x0
    adc.w #$0200                           ; C08DE2 m0x0
    sta.w $0C13                            ; C08DE5 m0x0

loc_C08DE8:
    lda.w $0C14                            ; C08DE8 m0x0
    and.w #$00FF                           ; C08DEB m0x0
    asl                                    ; C08DEE m0x0
    tax                                    ; C08DEF m0x0
    lda.l data_C46988,x                    ; C08DF0 m0x0
    xba                                    ; C08DF4 m0x0
    clc                                    ; C08DF5 m0x0
    rol                                    ; C08DF6 m0x0
    adc.w #$0000                           ; C08DF7 m0x0
    sta.w $0C2F                            ; C08DFA m0x0
    lda.b ptr_04                           ; C08DFD m0x0
    and.w #$01FF                           ; C08DFF m0x0
    ldx.w #$0186                           ; C08E02 m0x0

loc_C08E05:
    sta.w $0DB9,x                          ; C08E05 m0x0
    dex                                    ; C08E08 m0x0
    dex                                    ; C08E09 m0x0
    cpx.w #$00DE                           ; C08E0A m0x0
    bne loc_C08E05                         ; C08E0D m0x0
    clc                                    ; C08E0F m0x0

loc_C08E10:
    sta.w $0DB9,x                          ; C08E10 m0x0
    adc.w $0C2F                            ; C08E13 m0x0
    adc.w #$0000                           ; C08E16 m0x0
    dex                                    ; C08E19 m0x0
    dex                                    ; C08E1A m0x0
    bpl loc_C08E10                         ; C08E1B m0x0
    lda.w #$FF00                           ; C08E1D m0x0
    sta.w $0C04                            ; C08E20 m0x0
    ldx.b ptr_04                           ; C08E23 m0x0
    lda.w #$6C69                           ; C08E25 m0x0
    jmp.w loc_C08E7F                       ; C08E28 m0x0

loc_C08E2B:
    lda.b camera_y                         ; C08E2B m0x0
    lsr                                    ; C08E2D m0x0
    clc                                    ; C08E2E m0x0
    adc.b camera_y                         ; C08E2F m0x0
    sec                                    ; C08E31 m0x0
    sbc.w #$0048                           ; C08E32 m0x0
    bmi loc_C08E4A                         ; C08E35 m0x0
    bra loc_C08E45                         ; C08E37 m0x0

loc_C08E39:
    lda.b camera_y                         ; C08E39 m0x0
    lsr                                    ; C08E3B m0x0
    clc                                    ; C08E3C m0x0
    adc.b camera_y                         ; C08E3D m0x0
    sec                                    ; C08E3F m0x0
    sbc.w #$0048                           ; C08E40 m0x0
    bmi loc_C08E4A                         ; C08E43 m0x0

loc_C08E45:
    cmp.w #$00A0                           ; C08E45 m0x0
    bcs loc_C08E4D                         ; C08E48 m0x0

loc_C08E4A:
    lda.w #$00A0                           ; C08E4A m0x0

loc_C08E4D:
    tay                                    ; C08E4D m0x0
    lda.w #$1016                           ; C08E4E m0x0
    sta.w $0BD8                            ; C08E51 m0x0
    lda.w #$1017                           ; C08E54 m0x0
    sta.w $0BDA                            ; C08E57 m0x0
    lda.b $5E                              ; C08E5A m0x0
    and.w #$00FF                           ; C08E5C m0x0
    asl                                    ; C08E5F m0x0
    tax                                    ; C08E60 m0x0
    lda.l data_C46588,x                    ; C08E61 m0x0
    lsr                                    ; C08E65 m0x0
    lsr                                    ; C08E66 m0x0
    lsr                                    ; C08E67 m0x0
    lsr                                    ; C08E68 m0x0
    lsr                                    ; C08E69 m0x0
    lsr                                    ; C08E6A m0x0
    sta.b ptr_04                           ; C08E6B m0x0
    lsr                                    ; C08E6D m0x0
    sta.b $06                              ; C08E6E m0x0
    tya                                    ; C08E70 m0x0
    clc                                    ; C08E71 m0x0
    adc.b $06                              ; C08E72 m0x0
    tay                                    ; C08E74 m0x0
    lda.b camera_x                         ; C08E75 m0x0
    asl                                    ; C08E77 m0x0
    clc                                    ; C08E78 m0x0
    adc.b ptr_04                           ; C08E79 m0x0
    tax                                    ; C08E7B m0x0
    lda.w #$7171                           ; C08E7C m0x0

loc_C08E7F:
    sta.w $0BFC                            ; C08E7F m0x0
    stx.w $0BE4                            ; C08E82 m0x0
    sty.w $0BE6                            ; C08E85 m0x0
    lda.w #$0000                           ; C08E88 m0x0
    jsr.w vram_stream_descriptor_dispatch   ; C08E8B m0x0

check_pending_player_attack:
    ldx.w $0BB4                            ; C08E8E m0x0
    beq loc_C08E9A                         ; C08E91 m0x0
    lda.b $8E                              ; C08E93 m0x0
    ldy.b $90                              ; C08E95 m0x0
    jmp.w loc_C09A66                       ; C08E97 m0x0

loc_C08E9A:
    rts                                    ; C08E9A m0x0

nmi_scroll_mode0:
    lda.b dma_pending_mask                 ; C08E9B m0x0
    ora.w #$3E00                           ; C08E9D m0x0
    sta.w MDMAEN                           ; C08EA0 m0x0
    stz.b dma_pending_mask                 ; C08EA3 m0x0
    jsr.w vram_upload_column_580           ; C08EA5 m0x0
    jsr.w vram_upload_column_500           ; C08EA8 m0x0
    lda.b $76                              ; C08EAB m0x0
    clc                                    ; C08EAD m0x0
    adc.w $0BE6                            ; C08EAE m0x0
    sta.b ptr_04                           ; C08EB1 m0x0
    lda.b camera_x                         ; C08EB3 m0x0
    sep.b #$20                             ; C08EB5 m0x0
    sta.w BG2HOFS                          ; C08EB7 m1x0
    xba                                    ; C08EBA m1x0
    sta.w BG2HOFS                          ; C08EBB m1x0
    lda.b camera_y                         ; C08EBE m1x0
    sta.w BG2VOFS                          ; C08EC0 m1x0
    sta.w BG2VOFS                          ; C08EC3 m1x0
    lda.w $0BE4                            ; C08EC6 m1x0
    sta.w BG1HOFS                          ; C08EC9 m1x0
    lda.w $0BE5                            ; C08ECC m1x0
    sta.w BG1HOFS                          ; C08ECF m1x0
    lda.b ptr_04                           ; C08ED2 m1x0
    sta.w BG1VOFS                          ; C08ED4 m1x0
    lda.b $05                              ; C08ED7 m1x0
    sta.w BG1VOFS                          ; C08ED9 m1x0
    lda.w $0C08                            ; C08EDC m1x0
    beq loc_C08EF6                         ; C08EDF m1x0
    sta.w $0C08                            ; C08EE1 m1x0
    sta.w CGADD                            ; C08EE4 m1x0
    sta.w $0C08                            ; C08EE7 m1x0
    lda.w $0C0A                            ; C08EEA m1x0
    sta.w CGDATA                           ; C08EED m1x0
    lda.w $0C0B                            ; C08EF0 m1x0
    sta.w CGDATA                           ; C08EF3 m1x0

loc_C08EF6:
    rep.b #$20                             ; C08EF6 m1x0
    lda.w $0BD8                            ; C08EF8 m0x0
    sta.w $0BD4                            ; C08EFB m0x0
    lda.w $0BDA                            ; C08EFE m0x0
    sta.w $0BD6                            ; C08F01 m0x0
    lda.w $0BFC                            ; C08F04 m0x0
    sta.w $0BF8                            ; C08F07 m0x0
    lda.w $0BF0                            ; C08F0A m0x0
    sta.w $0BE8                            ; C08F0D m0x0
    lda.w $0BF2                            ; C08F10 m0x0
    sta.w $0BEA                            ; C08F13 m0x0
    lda.w $0BF4                            ; C08F16 m0x0
    sta.w $0BEC                            ; C08F19 m0x0
    lda.w $0BF6                            ; C08F1C m0x0
    sta.w $0BEE                            ; C08F1F m0x0
    lda.w $0C00                            ; C08F22 m0x0
    sta.l $7F00CF                          ; C08F25 m0x0
    lda.w $0C02                            ; C08F29 m0x0
    sta.l $7F00D3                          ; C08F2C m0x0
    lda.w $0C06                            ; C08F30 m0x0
    sta.l $7F0541                          ; C08F33 m0x0
    clc                                    ; C08F37 m0x0
    adc.w #$00FE                           ; C08F38 m0x0
    sta.l $7F0544                          ; C08F3B m0x0
    lda.w $0C04                            ; C08F3F m0x0
    sta.l $7F053F                          ; C08F42 m0x0
    rts                                    ; C08F46 m0x0

nmi_scroll_mode1:
    lda.b dma_pending_mask                 ; C08F47 m0x0
    ora.w #$FE00                           ; C08F49 m0x0
    sta.w MDMAEN                           ; C08F4C m0x0
    stz.b dma_pending_mask                 ; C08F4F m0x0
    jsr.w vram_upload_column_580           ; C08F51 m0x0
    jsr.w vram_upload_column_500           ; C08F54 m0x0
    lda.b camera_x                         ; C08F57 m0x0
    lsr                                    ; C08F59 m0x0
    sta.b ptr_04                           ; C08F5A m0x0
    lda.b $60                              ; C08F5C m0x0
    ror                                    ; C08F5E m0x0
    clc                                    ; C08F5F m0x0
    adc.b $60                              ; C08F60 m0x0
    lda.b camera_x                         ; C08F62 m0x0
    adc.b ptr_04                           ; C08F64 m0x0
    sta.l $7F0087                          ; C08F66 m0x0
    lda.b camera_x                         ; C08F6A m0x0
    sta.l $7F0081                          ; C08F6C m0x0
    sta.l $7F0084                          ; C08F70 m0x0
    lsr                                    ; C08F74 m0x0
    sep.b #$20                             ; C08F75 m0x0
    sta.w BG3HOFS                          ; C08F77 m1x0
    stz.w BG3HOFS                          ; C08F7A m1x0
    lda.b camera_y                         ; C08F7D m1x0
    sta.w BG1VOFS                          ; C08F7F m1x0
    sta.w BG1VOFS                          ; C08F82 m1x0
    eor.b #$FF                             ; C08F85 m1x0
    inc                                    ; C08F87 m1x0
    clc                                    ; C08F88 m1x0
    adc.b #$50                             ; C08F89 m1x0
    sta.l $7F0900                          ; C08F8B m1x0
    sta.l $7F0083                          ; C08F8F m1x0
    rep.b #$20                             ; C08F93 m1x0
    lda.b $5E                              ; C08F95 m0x0
    lsr                                    ; C08F97 m0x0
    lsr                                    ; C08F98 m0x0
    lsr                                    ; C08F99 m0x0
    clc                                    ; C08F9A m0x0
    adc.b $5E                              ; C08F9B m0x0
    and.w #$00FF                           ; C08F9D m0x0
    asl                                    ; C08FA0 m0x0
    tax                                    ; C08FA1 m0x0
    lda.l data_C46588,x                    ; C08FA2 m0x0
    cmp.w #$8000                           ; C08FA6 m0x0
    ror                                    ; C08FA9 m0x0
    cmp.w #$8000                           ; C08FAA m0x0
    ror                                    ; C08FAD m0x0
    cmp.w #$8000                           ; C08FAE m0x0
    ror                                    ; C08FB1 m0x0
    cmp.w #$8000                           ; C08FB2 m0x0
    ror                                    ; C08FB5 m0x0
    cmp.w #$8000                           ; C08FB6 m0x0
    ror                                    ; C08FB9 m0x0
    cmp.w #$8000                           ; C08FBA m0x0
    ror                                    ; C08FBD m0x0
    cmp.w #$8000                           ; C08FBE m0x0
    ror                                    ; C08FC1 m0x0
    clc                                    ; C08FC2 m0x0
    adc.w #$0003                           ; C08FC3 m0x0
    sta.l $7F00C4                          ; C08FC6 m0x0
    lda.b $60                              ; C08FCA m0x0
    asl                                    ; C08FCC m0x0
    sta.b $06                              ; C08FCD m0x0
    lda.b camera_x                         ; C08FCF m0x0
    rol                                    ; C08FD1 m0x0
    sta.b $08                              ; C08FD2 m0x0
    lda.b $5E                              ; C08FD4 m0x0
    and.w #$00FF                           ; C08FD6 m0x0
    asl                                    ; C08FD9 m0x0
    tax                                    ; C08FDA m0x0
    lda.l data_C46588,x                    ; C08FDB m0x0
    sta.b ptr_04                           ; C08FDF m0x0
    lda.w #$0000                           ; C08FE1 m0x0
    lsr.b ptr_04                           ; C08FE4 m0x0
    ror                                    ; C08FE6 m0x0
    lsr.b ptr_04                           ; C08FE7 m0x0
    ror                                    ; C08FE9 m0x0
    lsr.b ptr_04                           ; C08FEA m0x0
    ror                                    ; C08FEC m0x0
    lsr.b ptr_04                           ; C08FED m0x0
    ror                                    ; C08FEF m0x0
    lsr.b ptr_04                           ; C08FF0 m0x0
    ror                                    ; C08FF2 m0x0
    clc                                    ; C08FF3 m0x0
    adc.b $06                              ; C08FF4 m0x0
    lda.b ptr_04                           ; C08FF6 m0x0
    adc.b $08                              ; C08FF8 m0x0
    sta.b $A8                              ; C08FFA m0x0
    ldx.w #$0000                           ; C08FFC m0x0

loc_C08FFF:
    lda.l $800000+(camera_shake_ramp_table&$FFFF),x   ; C08FFF m0x0
    clc                                    ; C09003 m0x0
    adc.b camera_x                         ; C09004 m0x0
    sta.b $AC,x                            ; C09006 m0x0
    inx                                    ; C09008 m0x0
    inx                                    ; C09009 m0x0
    cpx.w #$0020                           ; C0900A m0x0
    bne loc_C08FFF                         ; C0900D m0x0
    lda.b $5E                              ; C0900F m0x0
    lsr                                    ; C09011 m0x0
    lsr                                    ; C09012 m0x0
    and.w #$000F                           ; C09013 m0x0
    sec                                    ; C09016 m0x0
    adc.w #$0080                           ; C09017 m0x0
    sep.b #$20                             ; C0901A m0x0
    sta.l $7F0003                          ; C0901C m1x0
    rep.b #$20                             ; C09020 m1x0
    lda.b $5E                              ; C09022 m0x0
    lsr                                    ; C09024 m0x0
    lsr                                    ; C09025 m0x0
    eor.w #$000F                           ; C09026 m0x0
    and.w #$000F                           ; C09029 m0x0
    sta.b ptr_04                           ; C0902C m0x0
    sec                                    ; C0902E m0x0
    adc.w #$0080                           ; C0902F m0x0
    sep.b #$20                             ; C09032 m0x0
    sta.l $7F0043                          ; C09034 m1x0
    rep.b #$20                             ; C09038 m1x0
    lda.b ptr_04                           ; C0903A m0x0
    eor.w #$000F                           ; C0903C m0x0
    asl                                    ; C0903F m0x0
    clc                                    ; C09040 m0x0
    adc.w #$84B5                           ; C09041 m0x0
    sta.l $7F0044                          ; C09044 m0x0
    rts                                    ; C09048 m0x0

nmi_scroll_mode2:
    lda.b dma_pending_mask                 ; C09049 m0x0
    sta.w MDMAEN                           ; C0904B m0x0
    stz.b dma_pending_mask                 ; C0904E m0x0
    jsr.w vram_upload_column_580           ; C09050 m0x0
    jsr.w vram_upload_column_500           ; C09053 m0x0
    lda.b camera_x                         ; C09056 m0x0
    ldy.b camera_y                         ; C09058 m0x0
    dey                                    ; C0905A m0x0
    sty.b ptr_04                           ; C0905B m0x0
    sep.b #$20                             ; C0905D m0x0
    sta.w BG2HOFS                          ; C0905F m1x0
    xba                                    ; C09062 m1x0
    sta.w BG2HOFS                          ; C09063 m1x0
    xba                                    ; C09066 m1x0
    tya                                    ; C09067 m1x0
    sta.w BG2VOFS                          ; C09068 m1x0
    xba                                    ; C0906B m1x0
    sta.w BG2VOFS                          ; C0906C m1x0
    rep.b #$20                             ; C0906F m1x0
    lda.b $5E                              ; C09071 m0x0
    and.w #$00FF                           ; C09073 m0x0
    asl                                    ; C09076 m0x0
    tax                                    ; C09077 m0x0
    lda.l data_C46588,x                    ; C09078 m0x0
    cmp.w #$8000                           ; C0907C m0x0
    ror                                    ; C0907F m0x0
    cmp.w #$8000                           ; C09080 m0x0
    ror                                    ; C09083 m0x0
    cmp.w #$8000                           ; C09084 m0x0
    ror                                    ; C09087 m0x0
    cmp.w #$8000                           ; C09088 m0x0
    ror                                    ; C0908B m0x0
    cmp.w #$8000                           ; C0908C m0x0
    ror                                    ; C0908F m0x0
    sta.b $06                              ; C09090 m0x0
    clc                                    ; C09092 m0x0
    adc.b camera_x                         ; C09093 m0x0
    sep.b #$20                             ; C09095 m0x0
    sta.w BG3HOFS                          ; C09097 m1x0
    xba                                    ; C0909A m1x0
    sta.w BG3HOFS                          ; C0909B m1x0
    rep.b #$20                             ; C0909E m1x0
    lda.b $06                              ; C090A0 m0x0
    cmp.w #$8000                           ; C090A2 m0x0
    ror                                    ; C090A5 m0x0
    eor.w #$FFFF                           ; C090A6 m0x0
    inc                                    ; C090A9 m0x0
    sta.b $08                              ; C090AA m0x0
    clc                                    ; C090AC m0x0
    adc.b ptr_04                           ; C090AD m0x0
    sep.b #$20                             ; C090AF m0x0
    sta.w BG3VOFS                          ; C090B1 m1x0
    xba                                    ; C090B4 m1x0
    sta.w BG3VOFS                          ; C090B5 m1x0
    rep.b #$20                             ; C090B8 m1x0
    lda.b ptr_04                           ; C090BA m0x0
    sec                                    ; C090BC m0x0
    sbc.w #$0010                           ; C090BD m0x0
    clc                                    ; C090C0 m0x0
    adc.b $08                              ; C090C1 m0x0
    bpl loc_C090CA                         ; C090C3 m0x0
    lda.w #$FFFF                           ; C090C5 m0x0
    bra loc_C090D2                         ; C090C8 m0x0

loc_C090CA:
    cmp.w #$0070                           ; C090CA m0x0
    bcc loc_C090D2                         ; C090CD m0x0
    lda.w #$0070                           ; C090CF m0x0

loc_C090D2:
    sep.b #$20                             ; C090D2 m0x0
    sta.w BG1VOFS                          ; C090D4 m1x0
    xba                                    ; C090D7 m1x0
    sta.w BG1VOFS                          ; C090D8 m1x0
    rep.b #$20                             ; C090DB m1x0
    lda.b $06                              ; C090DD m0x0
    cmp.w #$8000                           ; C090DF m0x0
    ror                                    ; C090E2 m0x0
    sta.b $06                              ; C090E3 m0x0
    lda.b camera_x                         ; C090E5 m0x0
    lsr                                    ; C090E7 m0x0
    clc                                    ; C090E8 m0x0
    adc.b camera_x                         ; C090E9 m0x0
    clc                                    ; C090EB m0x0
    adc.b $06                              ; C090EC m0x0
    sep.b #$20                             ; C090EE m0x0
    sta.w BG1HOFS                          ; C090F0 m1x0
    xba                                    ; C090F3 m1x0
    sta.w BG1HOFS                          ; C090F4 m1x0
    rep.b #$20                             ; C090F7 m1x0
    rts                                    ; C090F9 m0x0

nmi_scroll_title:
    lda.b dma_pending_mask                 ; C090FA m0x0
    ora.w #$3E00                           ; C090FC m0x0
    sta.w MDMAEN                           ; C090FF m0x0
    stz.b dma_pending_mask                 ; C09102 m0x0
    jsr.w vram_upload_column_580           ; C09104 m0x0
    jsr.w vram_upload_column_500           ; C09107 m0x0
    lda.b camera_x                         ; C0910A m0x0
    ldy.b camera_y                         ; C0910C m0x0
    dey                                    ; C0910E m0x0
    sty.b ptr_04                           ; C0910F m0x0
    sep.b #$20                             ; C09111 m0x0
    sta.w BG2HOFS                          ; C09113 m1x0
    xba                                    ; C09116 m1x0
    sta.w BG2HOFS                          ; C09117 m1x0
    xba                                    ; C0911A m1x0
    tya                                    ; C0911B m1x0
    sta.w BG2VOFS                          ; C0911C m1x0
    xba                                    ; C0911F m1x0
    sta.w BG2VOFS                          ; C09120 m1x0
    rep.b #$20                             ; C09123 m1x0
    lda.b camera_x                         ; C09125 m0x0
    lsr                                    ; C09127 m0x0
    sta.w $0BE8                            ; C09128 m0x0
    lda.b camera_y                         ; C0912B m0x0
    lsr                                    ; C0912D m0x0
    sta.w $0BEA                            ; C0912E m0x0
    lda.b $5E                              ; C09131 m0x0
    lsr                                    ; C09133 m0x0
    lsr                                    ; C09134 m0x0
    lsr                                    ; C09135 m0x0
    clc                                    ; C09136 m0x0
    adc.b camera_x                         ; C09137 m0x0
    sta.w $0BEC                            ; C09139 m0x0
    lda.b camera_y                         ; C0913C m0x0
    lsr                                    ; C0913E m0x0
    sta.b $18                              ; C0913F m0x0
    lsr                                    ; C09141 m0x0
    clc                                    ; C09142 m0x0
    adc.b $18                              ; C09143 m0x0
    sec                                    ; C09145 m0x0
    sbc.w #$006C                           ; C09146 m0x0
    sta.w $0BEE                            ; C09149 m0x0
    lda.b camera_x                         ; C0914C m0x0
    lsr                                    ; C0914E m0x0
    clc                                    ; C0914F m0x0
    adc.b camera_x                         ; C09150 m0x0
    sta.w $0BE0                            ; C09152 m0x0
    lda.b camera_x                         ; C09155 m0x0
    asl                                    ; C09157 m0x0
    sta.w $0BDC                            ; C09158 m0x0
    lda.b camera_y                         ; C0915B m0x0
    sec                                    ; C0915D m0x0
    sbc.w #$0030                           ; C0915E m0x0
    asl                                    ; C09161 m0x0
    bpl loc_C09165                         ; C09162 m0x0
    tdc                                    ; C09164 m0x0

loc_C09165:
    cmp.w #$0040                           ; C09165 m0x0
    bcc loc_C0916D                         ; C09168 m0x0
    lda.w #$003F                           ; C0916A m0x0

loc_C0916D:
    sta.w $0BDE                            ; C0916D m0x0
    sep.b #$20                             ; C09170 m0x0
    cmp.b #$08                             ; C09172 m1x0
    bcc loc_C09178                         ; C09174 m1x0
    lda.b #$08                             ; C09176 m1x0

loc_C09178:
    sta.b $18                              ; C09178 m1x0
    eor.b #$3F                             ; C0917A m1x0
    inc                                    ; C0917C m1x0
    sta.l $7F00D0                          ; C0917D m1x0
    rep.b #$20                             ; C09181 m1x0
    lda.b camera_y                         ; C09183 m0x0
    sec                                    ; C09185 m0x0
    sbc.w #$0091                           ; C09186 m0x0
    bmi loc_C09190                         ; C09189 m0x0
    beq loc_C09198                         ; C0918B m0x0
    tdc                                    ; C0918D m0x0
    bra loc_C09198                         ; C0918E m0x0

loc_C09190:
    cmp.w #$FF92                           ; C09190 m0x0
    bcs loc_C09198                         ; C09193 m0x0
    lda.w #$FF92                           ; C09195 m0x0

loc_C09198:
    dec                                    ; C09198 m0x0
    sta.w $0BE2                            ; C09199 m0x0
    eor.w #$FFFF                           ; C0919C m0x0
    inc                                    ; C0919F m0x0
    sep.b #$20                             ; C091A0 m0x0
    clc                                    ; C091A2 m1x0
    adc.b #$38                             ; C091A3 m1x0
    clc                                    ; C091A5 m1x0
    adc.b $18                              ; C091A6 m1x0
    cmp.b #$80                             ; C091A8 m1x0
    bcc loc_C091AE                         ; C091AA m1x0
    lda.b #$7F                             ; C091AC m1x0

loc_C091AE:
    cmp.b #$38                             ; C091AE m1x0
    bcs loc_C091B4                         ; C091B0 m1x0
    lda.b #$38                             ; C091B2 m1x0

loc_C091B4:
    sta.l $7F00D3                          ; C091B4 m1x0
    rep.b #$20                             ; C091B8 m1x0
    rts                                    ; C091BA m0x0

cgram_palette_ramp_step:
    bmi loc_C091D1                         ; C091BB m0x0
    lda.w $0C1D                            ; C091BD m0x0
    clc                                    ; C091C0 m0x0
    adc.w #$0080                           ; C091C1 m0x0
    sta.w $0C1D                            ; C091C4 m0x0
    cmp.w #$0800                           ; C091C7 m0x0
    bcc loc_C091CF                         ; C091CA m0x0

loc_C091CC:
    stz.w $0C1B                            ; C091CC m0x0

loc_C091CF:
    bra loc_C091DD                         ; C091CF m0x0

loc_C091D1:
    lda.w $0C1D                            ; C091D1 m0x0
    sec                                    ; C091D4 m0x0
    sbc.w #$0080                           ; C091D5 m0x0
    sta.w $0C1D                            ; C091D8 m0x0
    beq loc_C091CC                         ; C091DB m0x0

loc_C091DD:
    ldx.w $0B8A                            ; C091DD m0x0
    and.w #$FF00                           ; C091E0 m0x0
    lsr                                    ; C091E3 m0x0
    lsr                                    ; C091E4 m0x0
    clc                                    ; C091E5 m0x0
    adc.w #$7103                           ; C091E6 m0x0
    sta.w $0B90,x                          ; C091E9 m0x0
    lda.w #$00C4                           ; C091EC m0x0
    sta.w $0B92,x                          ; C091EF m0x0
    lda.w #$0040                           ; C091F2 m0x0
    sta.w $0B8C,x                          ; C091F5 m0x0
    lda.w #$0080                           ; C091F8 m0x0
    sta.w $0B8E,x                          ; C091FB m0x0
    txa                                    ; C091FE m0x0
    adc.w #$0008                           ; C091FF m0x0
    sta.w $0B8A                            ; C09202 m0x0
    rts                                    ; C09205 m0x0

unused_stream_desc_dispatch:
    clc                                    ; C09206 m0x0
    adc.w $0BB8                            ; C09207 m0x0
    tax                                    ; C0920A m0x0
    lda.l $800000+(vram_stream_desc_table&$FFFF),x   ; C0920B m0x0
    beq loc_C0921C                         ; C0920F m0x0
    sta.b ptr_04                           ; C09211 m0x0
    lda.w $0BBA                            ; C09213 m0x0
    inc.w $0BBA                            ; C09216 m0x0
    jmp.w ($0004)                          ; C09219 m0x0

loc_C0921C:
    rts                                    ; C0921C m0x0

unused_stream_desc_dispatch_tail:
    incbin "../data/01.bin":$121D..$1227      ; 10 bytes

mode0_particle_draw_dispatch:
    jmp.w particle_update_and_draw_mode0   ; C09227 m0x0

mode1_particle_dispatch:
    jsr.w sparkle_update_and_draw          ; C0922A m0x0
    jmp.w loc_C09679                       ; C0922D m0x0

mode2_particle_dispatch:
    jmp.w loc_C097DD                       ; C09230 m0x0

particle_dispatch_noop:
    rts                                    ; C09233 m0x0

vram_upload_shared_tileset_c5:
    lda.w #$0000                           ; C09234 m0x0
    sta.w VMADDL                           ; C09237 m0x0
    ldx.w #$00C5                           ; C0923A m0x0
    lda.w #$02C0                           ; C0923D m0x0
    ldy.w #$0C00                           ; C09240 m0x0
    jsr.w dma_upload_to_vram               ; C09243 m0x0

particle_table_clear:
    ldx.w #$004E                           ; C09246 m0x0
    tdc                                    ; C09249 m0x0

loc_C0924A:
    sta.l $7F0906,x                        ; C0924A m0x0
    dex                                    ; C0924E m0x0
    dex                                    ; C0924F m0x0
    bpl loc_C0924A                         ; C09250 m0x0
    rts                                    ; C09252 m0x0

particle_spawn_mode0_weather:
    stz.w $0C15                            ; C09253 m0x0
    ldx.w #$004E                           ; C09256 m0x0

loc_C09259:
    lda.l $7F0906,x                        ; C09259 m0x0
    beq loc_C09262                         ; C0925D m0x0
    jmp.w loc_C092EB                       ; C0925F m0x0

loc_C09262:
    jsr.w random_next                      ; C09262 m0x0
    lda.b init_magic_AA55                  ; C09265 m0x0
    and.w #$003F                           ; C09267 m0x0
    sta.b ptr_04                           ; C0926A m0x0
    eor.w #$FFFF                           ; C0926C m0x0
    inc                                    ; C0926F m0x0
    clc                                    ; C09270 m0x0
    adc.w $0C17                            ; C09271 m0x0
    sta.l $7F0B06,x                        ; C09274 m0x0
    lda.b ptr_04                           ; C09278 m0x0
    lsr                                    ; C0927A m0x0
    lsr                                    ; C0927B m0x0
    eor.w #$FFFF                           ; C0927C m0x0
    inc                                    ; C0927F m0x0
    sta.b ptr_04                           ; C09280 m0x0
    lda.b ptr_04                           ; C09282 m0x0
    clc                                    ; C09284 m0x0
    adc.w #$0080                           ; C09285 m0x0
    sta.l $7F0B86,x                        ; C09288 m0x0
    lda.b camera_y                         ; C0928C m0x0
    adc.w #$0090                           ; C0928E m0x0
    sta.l $7F0A86,x                        ; C09291 m0x0
    lda.b $9D                              ; C09295 m0x0
    and.w #$00FF                           ; C09297 m0x0
    adc.w #$00C0                           ; C0929A m0x0
    sta.l $7F0D06,x                        ; C0929D m0x0
    lsr                                    ; C092A1 m0x0
    adc.w #$0040                           ; C092A2 m0x0
    sta.l $7F0E06,x                        ; C092A5 m0x0
    lda.w #$2D60                           ; C092A9 m0x0
    sta.l $7F0906,x                        ; C092AC m0x0
    tdc                                    ; C092B0 m0x0
    sta.l $7F0C06,x                        ; C092B1 m0x0
    sta.l $7F0C86,x                        ; C092B5 m0x0
    lda.b init_magic_AA55                  ; C092B9 m0x0
    and.w #$7FFF                           ; C092BB m0x0
    bit.b init_magic_FFFF                  ; C092BE m0x0
    bpl loc_C092C6                         ; C092C0 m0x0
    eor.w #$FFFF                           ; C092C2 m0x0
    inc                                    ; C092C5 m0x0

loc_C092C6:
    sta.l $7F0D86,x                        ; C092C6 m0x0
    beq loc_C092DA                         ; C092CA m0x0
    lda.b $9D                              ; C092CC m0x0
    and.w #$0003                           ; C092CE m0x0
    inc                                    ; C092D1 m0x0
    bit.b init_magic_FFFF                  ; C092D2 m0x0
    bpl loc_C092DA                         ; C092D4 m0x0
    eor.w #$FFFF                           ; C092D6 m0x0
    inc                                    ; C092D9 m0x0

loc_C092DA:
    sta.l $7F0A06,x                        ; C092DA m0x0
    lda.b init_magic_AA55                  ; C092DE m0x0
    and.w #$0010                           ; C092E0 m0x0
    clc                                    ; C092E3 m0x0
    adc.w #$0010                           ; C092E4 m0x0
    sta.l $7F0986,x                        ; C092E7 m0x0

loc_C092EB:
    dex                                    ; C092EB m0x0
    dex                                    ; C092EC m0x0
    bmi loc_C092F2                         ; C092ED m0x0
    jmp.w loc_C09259                       ; C092EF m0x0

loc_C092F2:
    rts                                    ; C092F2 m0x0

mode1_reset_particles_and_oam:
    rep.b #$30                             ; C092F3 m0x0
    stz.b camera_x                         ; C092F5 m0x0
    stz.b camera_y                         ; C092F7 m0x0
    stz.b level_height_mask                ; C092F9 m0x0
    stz.w $0C1F                            ; C092FB m0x0
    lda.w #$0001                           ; C092FE m0x0
    sta.w $0C21                            ; C09301 m0x0
    lda.w #$00FF                           ; C09304 m0x0
    sta.w $0C23                            ; C09307 m0x0
    sta.w $0C29                            ; C0930A m0x0
    lda.w #$0020                           ; C0930D m0x0
    sta.w $0C25                            ; C09310 m0x0
    stz.w $0C27                            ; C09313 m0x0
    stz.w $0C2B                            ; C09316 m0x0
    lda.b init_magic_AA55                  ; C09319 m0x0
    and.w #$3000                           ; C0931B m0x0
    ora.w #$0E00                           ; C0931E m0x0
    sta.w $0C2D                            ; C09321 m0x0
    jsr.w clear_sprite_table               ; C09324 m0x0
    jsr.w particle_update_and_draw_mode0   ; C09327 m0x0
    jsr.w oam_hide_unused_sprites          ; C0932A m0x0
    jsr.w oam_dma_upload                   ; C0932D m0x0
    rts                                    ; C09330 m0x0

particle_update_and_draw_mode0:
    tsc                                    ; C09331 m0x0
    sta.b $18                              ; C09332 m0x0
    lda.b camera_y                         ; C09334 m0x0
    and.w #$00FF                           ; C09336 m0x0
    sta.b $1A                              ; C09339 m0x0
    lda.b level_height_mask                ; C0933B m0x0
    clc                                    ; C0933D m0x0
    adc.w #$00F0                           ; C0933E m0x0
    sta.b $1C                              ; C09341 m0x0
    ldx.w #$004E                           ; C09343 m0x0
    ldy.b oam_write_ptr                    ; C09346 m0x0

loc_C09348:
    lda.l $7F0906,x                        ; C09348 m0x0
    beq loc_C09351                         ; C0934C m0x0
    jmp.w loc_C093FA                       ; C0934E m0x0

loc_C09351:
    lda.w $0C1F                            ; C09351 m0x0
    bne loc_C0935B                         ; C09354 m0x0
    lda.w $0C21                            ; C09356 m0x0
    bne loc_C0935E                         ; C09359 m0x0

loc_C0935B:
    jmp.w loc_C093ED                       ; C0935B m0x0

loc_C0935E:
    dec.w $0C21                            ; C0935E m0x0
    sep.b #$20                             ; C09361 m0x0
    lda.b $9D                              ; C09363 m1x0
    sta.b sprite_frame_bank                ; C09365 m1x0
    asl                                    ; C09367 m1x0
    lda.b init_magic_FFFF                  ; C09368 m1x0
    rol.b init_magic_FFFF                  ; C0936A m1x0
    rol.b init_magic_FFFF                  ; C0936C m1x0
    eor.b $9F                              ; C0936E m1x0
    sta.b $9D                              ; C09370 m1x0
    lda.b sprite_frame_bank                ; C09372 m1x0
    sta.b $9F                              ; C09374 m1x0
    eor.b init_magic_FFFF                  ; C09376 m1x0
    sta.b sprite_frame_bank                ; C09378 m1x0
    lda.b init_magic_AA55                  ; C0937A m1x0
    sta.b init_magic_FFFF                  ; C0937C m1x0
    lda.b sprite_frame_bank                ; C0937E m1x0
    sta.b init_magic_AA55                  ; C09380 m1x0
    rep.b #$20                             ; C09382 m1x0
    lda.b init_magic_AA55                  ; C09384 m0x0
    and.w $0C29                            ; C09386 m0x0
    adc.w $0C27                            ; C09389 m0x0
    sta.l $7F0B06,x                        ; C0938C m0x0
    lda.b init_magic_FFFF                  ; C09390 m0x0
    and.w $0C23                            ; C09392 m0x0
    adc.w $0C25                            ; C09395 m0x0
    sta.l $7F0D06,x                        ; C09398 m0x0
    lsr                                    ; C0939C m0x0
    sta.l $7F0E06,x                        ; C0939D m0x0
    lda.w $0C2D                            ; C093A1 m0x0
    sta.l $7F0906,x                        ; C093A4 m0x0
    lda.b $9F                              ; C093A8 m0x0
    and.w #$0030                           ; C093AA m0x0
    cmp.w #$0030                           ; C093AD m0x0
    bcc loc_C093B3                         ; C093B0 m0x0
    tdc                                    ; C093B2 m0x0

loc_C093B3:
    sta.l $7F0986,x                        ; C093B3 m0x0
    sta.l $7F0C86,x                        ; C093B7 m0x0
    sta.l $7F0B86,x                        ; C093BB m0x0
    lda.b init_magic_FFFF                  ; C093BF m0x0
    and.w $0C2B                            ; C093C1 m0x0
    sta.l $7F0A06,x                        ; C093C4 m0x0
    lda.b init_magic_AA55                  ; C093C8 m0x0
    and.w #$7FFF                           ; C093CA m0x0
    sta.l $7F0D86,x                        ; C093CD m0x0
    bit.b init_magic_FFFF                  ; C093D1 m0x0
    bpl loc_C093E7                         ; C093D3 m0x0
    eor.w #$FFFF                           ; C093D5 m0x0
    sta.l $7F0D86,x                        ; C093D8 m0x0
    lda.l $7F0A06,x                        ; C093DC m0x0
    eor.w #$FFFF                           ; C093E0 m0x0
    sta.l $7F0A06,x                        ; C093E3 m0x0

loc_C093E7:
    lda.b camera_y                         ; C093E7 m0x0
    sta.l $7F0A86,x                        ; C093E9 m0x0

loc_C093ED:
    dex                                    ; C093ED m0x0
    dex                                    ; C093EE m0x0
    bmi loc_C093F4                         ; C093EF m0x0
    jmp.w loc_C09348                       ; C093F1 m0x0

loc_C093F4:
    sty.b oam_write_ptr                    ; C093F4 m0x0
    lda.b $18                              ; C093F6 m0x0
    tcs                                    ; C093F8 m0x0
    rts                                    ; C093F9 m0x0

loc_C093FA:
    lda.l $7F0C06,x                        ; C093FA m0x0
    clc                                    ; C093FE m0x0
    adc.l $7F0E06,x                        ; C093FF m0x0
    sta.l $7F0C06,x                        ; C09403 m0x0
    xba                                    ; C09407 m0x0
    txs                                    ; C09408 m0x0
    and.w #$001F                           ; C09409 m0x0
    cmp.w #$0010                           ; C0940C m0x0
    bcc loc_C09425                         ; C0940F m0x0
    sta.b ptr_04                           ; C09411 m0x0
    lda.l $7F0986,x                        ; C09413 m0x0
    bne loc_C09420                         ; C09417 m0x0
    lda.b ptr_04                           ; C09419 m0x0
    eor.w #$001F                           ; C0941B m0x0
    sta.b ptr_04                           ; C0941E m0x0

loc_C09420:
    lda.b ptr_04                           ; C09420 m0x0
    and.w #$000F                           ; C09422 m0x0

loc_C09425:
    ora.l $7F0986,x                        ; C09425 m0x0
    asl                                    ; C09429 m0x0
    sta.b $08                              ; C0942A m0x0
    tax                                    ; C0942C m0x0
    lda.l data_C50200,x                    ; C0942D m0x0
    sta.b ptr_04                           ; C09431 m0x0
    lda.l data_C50260,x                    ; C09433 m0x0
    sta.b $06                              ; C09437 m0x0
    tsx                                    ; C09439 m0x0
    lda.l $7F0C86,x                        ; C0943A m0x0
    clc                                    ; C0943E m0x0
    adc.l $7F0D86,x                        ; C0943F m0x0
    sta.l $7F0C86,x                        ; C09443 m0x0
    lda.l $7F0D86,x                        ; C09447 m0x0
    lda.l $7F0B06,x                        ; C0944B m0x0
    adc.l $7F0A06,x                        ; C0944F m0x0
    sta.l $7F0B06,x                        ; C09453 m0x0
    lda.l $7F0B86,x                        ; C09457 m0x0
    clc                                    ; C0945B m0x0
    adc.l $7F0D06,x                        ; C0945C m0x0
    sta.l $7F0B86,x                        ; C09460 m0x0
    lda.l $7F0A87,x                        ; C09464 m0x0
    adc.w #$0000                           ; C09468 m0x0
    sta.l $7F0A87,x                        ; C0946B m0x0
    lda.l $7F0B87,x                        ; C0946F m0x0
    and.w #$00FF                           ; C09473 m0x0
    clc                                    ; C09476 m0x0
    adc.l $7F0A86,x                        ; C09477 m0x0
    sta.b $1E                              ; C0947B m0x0
    cmp.b $1C                              ; C0947D m0x0
    bcc loc_C09488                         ; C0947F m0x0
    tdc                                    ; C09481 m0x0
    sta.l $7F0906,x                        ; C09482 m0x0
    bra loc_C094D7                         ; C09486 m0x0

loc_C09488:
    lda.l $7F0B06,x                        ; C09488 m0x0
    sec                                    ; C0948C m0x0
    sbc.b camera_x                         ; C0948D m0x0
    bmi loc_C094D7                         ; C0948F m0x0
    cmp.w #$00F0                           ; C09491 m0x0
    bcs loc_C094D7                         ; C09494 m0x0
    sec                                    ; C09496 m0x0
    sbc.w #$0080                           ; C09497 m0x0
    tcs                                    ; C0949A m0x0
    clc                                    ; C0949B m0x0
    adc.b ptr_04                           ; C0949C m0x0
    sta.w nmi_handler_ptr,y                ; C0949E m0x0
    tsc                                    ; C094A1 m0x0
    clc                                    ; C094A2 m0x0
    adc.b $06                              ; C094A3 m0x0
    sta.w ptr_04,y                         ; C094A5 m0x0
    lda.b $1E                              ; C094A8 m0x0
    sec                                    ; C094AA m0x0
    sbc.b $1A                              ; C094AB m0x0
    cmp.w #$00F0                           ; C094AD m0x0
    bcs loc_C094D7                         ; C094B0 m0x0
    sec                                    ; C094B2 m0x0
    sbc.w #$0080                           ; C094B3 m0x0
    tcs                                    ; C094B6 m0x0
    clc                                    ; C094B7 m0x0
    adc.b $05                              ; C094B8 m0x0
    sta.w $0001,y                          ; C094BA m0x0
    tsc                                    ; C094BD m0x0
    clc                                    ; C094BE m0x0
    adc.b spc_dest_addr                    ; C094BF m0x0
    sta.w $0005,y                          ; C094C1 m0x0
    lda.b $08                              ; C094C4 m0x0
    clc                                    ; C094C6 m0x0
    adc.l $7F0906,x                        ; C094C7 m0x0
    sta.w dma_pending_mask,y               ; C094CB m0x0
    inc                                    ; C094CE m0x0
    sta.w $0006,y                          ; C094CF m0x0
    tya                                    ; C094D2 m0x0
    adc.w #$0008                           ; C094D3 m0x0
    tay                                    ; C094D6 m0x0

loc_C094D7:
    dex                                    ; C094D7 m0x0
    dex                                    ; C094D8 m0x0
    bmi loc_C094DE                         ; C094D9 m0x0
    jmp.w loc_C09348                       ; C094DB m0x0

loc_C094DE:
    sty.b oam_write_ptr                    ; C094DE m0x0
    lda.b $18                              ; C094E0 m0x0
    tcs                                    ; C094E2 m0x0
    rts                                    ; C094E3 m0x0

sparkle_array_init:
    lda.w #$00F8                           ; C094E4 m0x0

loc_C094E7:
    tax                                    ; C094E7 m0x0
    tdc                                    ; C094E8 m0x0
    sta.l $7F0E86,x                        ; C094E9 m0x0
    jsr.w random_next                      ; C094ED m0x0
    lda.b init_magic_AA55                  ; C094F0 m0x0
    and.w #$07FF                           ; C094F2 m0x0
    sta.l $7F0E87,x                        ; C094F5 m0x0
    lda.b init_magic_FFFF                  ; C094F9 m0x0
    and.w #$007F                           ; C094FB m0x0
    adc.w #$0060                           ; C094FE m0x0
    sta.l $7F0E89,x                        ; C09501 m0x0
    lda.b init_magic_FFFF                  ; C09505 m0x0
    and.w #$007F                           ; C09507 m0x0
    adc.w #$0040                           ; C0950A m0x0
    bit.b init_magic_FFFF                  ; C0950D m0x0
    bpl loc_C09515                         ; C0950F m0x0
    eor.w #$FFFF                           ; C09511 m0x0
    inc                                    ; C09514 m0x0

loc_C09515:
    sta.l $7F0E8A,x                        ; C09515 m0x0
    txa                                    ; C09519 m0x0
    sec                                    ; C0951A m0x0
    sbc.w #$0008                           ; C0951B m0x0
    bpl loc_C094E7                         ; C0951E m0x0
    rts                                    ; C09520 m0x0

sparkle_update_and_draw:
    ldy.b oam_write_ptr                    ; C09521 m0x0
    cpy.w #$0400                           ; C09523 m0x0
    bcc loc_C09529                         ; C09526 m0x0
    rts                                    ; C09528 m0x0

loc_C09529:
    lda.w #$00F8                           ; C09529 m0x0

loc_C0952C:
    tax                                    ; C0952C m0x0
    lda.l $7F0E8A,x                        ; C0952D m0x0
    sta.b $06                              ; C09531 m0x0
    lda.l $7F0E87,x                        ; C09533 m0x0
    sec                                    ; C09537 m0x0
    sbc.b camera_x                         ; C09538 m0x0
    bmi loc_C09593                         ; C0953A m0x0
    cmp.w #$0100                           ; C0953C m0x0
    bcs loc_C09593                         ; C0953F m0x0
    sta.w nmi_handler_ptr,y                ; C09541 m0x0
    adc.w #$0008                           ; C09544 m0x0
    bit.b $06                              ; C09547 m0x0
    bmi loc_C0954E                         ; C09549 m0x0
    adc.w #$FFF0                           ; C0954B m0x0

loc_C0954E:
    sta.w ptr_04,y                         ; C0954E m0x0
    phx                                    ; C09551 m0x0
    lda.l $7F0E87,x                        ; C09552 m0x0
    and.w #$001E                           ; C09556 m0x0
    tax                                    ; C09559 m0x0
    lda.l $800000+(camera_shake_ramp_table&$FFFF),x   ; C0955A m0x0
    plx                                    ; C0955E m0x0
    clc                                    ; C0955F m0x0
    adc.l $7F0E89,x                        ; C09560 m0x0
    sta.w $0001,y                          ; C09564 m0x0
    sta.w $0005,y                          ; C09567 m0x0
    lda.l $7F0E8C,x                        ; C0956A m0x0
    clc                                    ; C0956E m0x0
    adc.w #$0040                           ; C0956F m0x0
    sta.l $7F0E8C,x                        ; C09572 m0x0
    xba                                    ; C09576 m0x0
    and.w #$0007                           ; C09577 m0x0
    asl                                    ; C0957A m0x0
    clc                                    ; C0957B m0x0
    adc.w #$17E0                           ; C0957C m0x0
    bit.b $06                              ; C0957F m0x0
    bpl loc_C09586                         ; C09581 m0x0
    ora.w #$4000                           ; C09583 m0x0

loc_C09586:
    sta.w dma_pending_mask,y               ; C09586 m0x0
    inc                                    ; C09589 m0x0
    sta.w $0006,y                          ; C0958A m0x0
    tya                                    ; C0958D m0x0
    clc                                    ; C0958E m0x0
    adc.w #$0008                           ; C0958F m0x0
    tay                                    ; C09592 m0x0

loc_C09593:
    stz.b ptr_04                           ; C09593 m0x0
    bit.b $06                              ; C09595 m0x0
    bpl loc_C0959B                         ; C09597 m0x0
    dec.b ptr_04                           ; C09599 m0x0

loc_C0959B:
    lda.l $7F0E86,x                        ; C0959B m0x0
    adc.l $7F0E8A,x                        ; C0959F m0x0
    sta.l $7F0E86,x                        ; C095A3 m0x0
    lda.l $7F0E88,x                        ; C095A7 m0x0
    adc.b ptr_04                           ; C095AB m0x0
    sta.l $7F0E88,x                        ; C095AD m0x0
    lda.l $7F0E87,x                        ; C095B1 m0x0
    bit.b $06                              ; C095B5 m0x0
    bpl loc_C095C0                         ; C095B7 m0x0
    and.w #$FFFF                           ; C095B9 m0x0
    bmi loc_C095C5                         ; C095BC m0x0
    bra loc_C095D1                         ; C095BE m0x0

loc_C095C0:
    cmp.w #$0580                           ; C095C0 m0x0
    bcc loc_C095D1                         ; C095C3 m0x0

loc_C095C5:
    lda.l $7F0E8A,x                        ; C095C5 m0x0
    eor.w #$FFFF                           ; C095C9 m0x0
    inc                                    ; C095CC m0x0
    sta.l $7F0E8A,x                        ; C095CD m0x0

loc_C095D1:
    cpy.w #$0400                           ; C095D1 m0x0
    bcs loc_C095E0                         ; C095D4 m0x0
    txa                                    ; C095D6 m0x0
    sec                                    ; C095D7 m0x0
    sbc.w #$0008                           ; C095D8 m0x0
    bmi loc_C095E0                         ; C095DB m0x0
    jmp.w loc_C0952C                       ; C095DD m0x0

loc_C095E0:
    sty.b oam_write_ptr                    ; C095E0 m0x0
    rts                                    ; C095E2 m0x0

particle_spawn_from_table:
    lda.w #$1F00                           ; C095E3 m0x0
    jsr.w vram_generate_particle_tile      ; C095E6 m0x0
    lda.b game_mode                        ; C095E9 m0x0
    asl                                    ; C095EB m0x0
    tax                                    ; C095EC m0x0
    lda.l $800000+(particle_spawn_list_x&$FFFF),x   ; C095ED m0x0
    tax                                    ; C095F1 m0x0
    pea.w $807F                            ; C095F2 m0x0
    plb                                    ; C095F5 m0x0
    ldy.w #$003A                           ; C095F6 m0x0

loc_C095F9:
    lda.l $800000+(particle_spawn_list_x&$FFFF),x   ; C095F9 m0x0
    bmi loc_C0966A                         ; C095FD m0x0
    sec                                    ; C095FF m0x0
    sbc.w #$0080                           ; C09600 m0x0
    sta.w $0A06,y                          ; C09603 m0x0
    lda.l $800000+(particle_spawn_list_y&$FFFF),x   ; C09606 m0x0
    sec                                    ; C0960A m0x0
    sbc.w #$0080                           ; C0960B m0x0
    sta.w $0A86,y                          ; C0960E m0x0
    lda.w #$8000                           ; C09611 m0x0
    sta.w $0B06,y                          ; C09614 m0x0
    sta.w $0B86,y                          ; C09617 m0x0
    jsr.w random_next                      ; C0961A m0x0
    lda.b init_magic_AA55                  ; C0961D m0x0
    and.w #$1FFF                           ; C0961F m0x0
    sta.w $0C06,y                          ; C09622 m0x0
    jsr.w random_next                      ; C09625 m0x0
    lda.b init_magic_AA55                  ; C09628 m0x0
    and.w #$01FF                           ; C0962A m0x0
    clc                                    ; C0962D m0x0
    adc.w #$0100                           ; C0962E m0x0
    sta.w $0C86,y                          ; C09631 m0x0
    jsr.w random_next                      ; C09634 m0x0
    lda.b init_magic_AA55                  ; C09637 m0x0
    and.w #$01FF                           ; C09639 m0x0
    adc.w #$0100                           ; C0963C m0x0
    sta.w $0D06,y                          ; C0963F m0x0
    tdc                                    ; C09642 m0x0
    sta.w $0D86,y                          ; C09643 m0x0
    sta.w $0E06,y                          ; C09646 m0x0
    jsr.w random_next                      ; C09649 m0x0
    lda.b init_magic_AA55                  ; C0964C m0x0
    and.w #$00FF                           ; C0964E m0x0
    adc.w #$0080                           ; C09651 m0x0
    sta.w $0906,y                          ; C09654 m0x0
    lda.b init_magic_FFFF                  ; C09657 m0x0
    and.w #$007F                           ; C09659 m0x0
    adc.w #$0080                           ; C0965C m0x0
    sta.w $0986,y                          ; C0965F m0x0
    inx                                    ; C09662 m0x0
    inx                                    ; C09663 m0x0
    inx                                    ; C09664 m0x0
    inx                                    ; C09665 m0x0
    dey                                    ; C09666 m0x0
    dey                                    ; C09667 m0x0
    bpl loc_C095F9                         ; C09668 m0x0

loc_C0966A:
    tya                                    ; C0966A m0x0
    bmi loc_C09677                         ; C0966B m0x0
    lda.w #$FFFF                           ; C0966D m0x0

loc_C09670:
    sta.w $0906,y                          ; C09670 m0x0
    dey                                    ; C09673 m0x0
    dey                                    ; C09674 m0x0
    bpl loc_C09670                         ; C09675 m0x0

loc_C09677:
    plb                                    ; C09677 m0x0
    rts                                    ; C09678 m0x0

loc_C09679:
    lda.b oam_write_ptr                    ; C09679 m0x0
    tay                                    ; C0967B m0x0
    sec                                    ; C0967C m0x0
    sbc.w #$0400                           ; C0967D m0x0
    bmi loc_C09683                         ; C09680 m0x0
    rts                                    ; C09682 m0x0

loc_C09683:
    eor.w #$FFFF                           ; C09683 m0x0
    inc                                    ; C09686 m0x0
    lsr                                    ; C09687 m0x0
    cmp.w #$003C                           ; C09688 m0x0
    bcc loc_C09690                         ; C0968B m0x0
    lda.w #$003A                           ; C0968D m0x0

loc_C09690:
    tax                                    ; C09690 m0x0

loc_C09691:
    lda.l $7F0906,x                        ; C09691 m0x0
    bmi loc_C096C8                         ; C09695 m0x0
    bne loc_C096CF                         ; C09697 m0x0
    lda.l $7F0986,x                        ; C09699 m0x0
    bne loc_C096B6                         ; C0969D m0x0
    jsr.w random_next                      ; C0969F m0x0
    lda.b init_magic_AA55                  ; C096A2 m0x0
    and.w #$00FF                           ; C096A4 m0x0
    adc.w #$0080                           ; C096A7 m0x0
    sta.l $7F0906,x                        ; C096AA m0x0
    lda.b init_magic_FFFF                  ; C096AE m0x0
    and.w #$007F                           ; C096B0 m0x0
    adc.w #$0080                           ; C096B3 m0x0

loc_C096B6:
    dec                                    ; C096B6 m0x0
    sta.l $7F0986,x                        ; C096B7 m0x0
    lda.l $7F0C06,x                        ; C096BB m0x0
    adc.w #$0080                           ; C096BF m0x0
    sta.l $7F0C06,x                        ; C096C2 m0x0
    bra loc_C09729                         ; C096C6 m0x0

loc_C096C8:
    dex                                    ; C096C8 m0x0
    dex                                    ; C096C9 m0x0
    bpl loc_C09691                         ; C096CA m0x0
    jmp.w loc_C0977E                       ; C096CC m0x0

loc_C096CF:
    dec                                    ; C096CF m0x0
    sta.l $7F0906,x                        ; C096D0 m0x0
    lda.l $7F0C06,x                        ; C096D4 m0x0
    adc.w #$0100                           ; C096D8 m0x0
    sta.l $7F0C06,x                        ; C096DB m0x0
    lda.l $7F0D87,x                        ; C096DF m0x0
    and.w #$00FF                           ; C096E3 m0x0
    asl                                    ; C096E6 m0x0
    stx.b $06                              ; C096E7 m0x0
    tax                                    ; C096E9 m0x0
    lda.l data_C46588,x                    ; C096EA m0x0
    ldx.b $06                              ; C096EE m0x0
    adc.l $7F0B06,x                        ; C096F0 m0x0
    sta.l $7F0B06,x                        ; C096F4 m0x0
    lda.l $7F0E07,x                        ; C096F8 m0x0
    and.w #$00FF                           ; C096FC m0x0
    asl                                    ; C096FF m0x0
    tax                                    ; C09700 m0x0
    lda.l data_C46588,x                    ; C09701 m0x0
    ldx.b $06                              ; C09705 m0x0
    adc.l $7F0B86,x                        ; C09707 m0x0
    sta.l $7F0B86,x                        ; C0970B m0x0
    lda.l $7F0D86,x                        ; C0970F m0x0
    clc                                    ; C09713 m0x0
    adc.l $7F0C86,x                        ; C09714 m0x0
    sta.l $7F0D86,x                        ; C09718 m0x0
    lda.l $7F0E06,x                        ; C0971C m0x0
    clc                                    ; C09720 m0x0
    adc.l $7F0D06,x                        ; C09721 m0x0
    sta.l $7F0E06,x                        ; C09725 m0x0

loc_C09729:
    lda.l $7F0B07,x                        ; C09729 m0x0
    and.w #$00FF                           ; C0972D m0x0
    clc                                    ; C09730 m0x0
    adc.l $7F0A06,x                        ; C09731 m0x0
    sec                                    ; C09735 m0x0
    sbc.b camera_x                         ; C09736 m0x0
    bmi loc_C096C8                         ; C09738 m0x0
    cmp.w #$0100                           ; C0973A m0x0
    bcs loc_C096C8                         ; C0973D m0x0
    sta.w nmi_handler_ptr,y                ; C0973F m0x0
    lda.l $7F0B87,x                        ; C09742 m0x0
    and.w #$00FF                           ; C09746 m0x0
    adc.l $7F0A86,x                        ; C09749 m0x0
    sec                                    ; C0974D m0x0
    sbc.b camera_y                         ; C0974E m0x0
    bmi loc_C09777                         ; C09750 m0x0
    cmp.w #$00E0                           ; C09752 m0x0
    bcs loc_C09777                         ; C09755 m0x0
    sta.w $0001,y                          ; C09757 m0x0
    lda.l $7F0C07,x                        ; C0975A m0x0
    and.w #$001F                           ; C0975E m0x0
    cmp.w #$0010                           ; C09761 m0x0
    bcc loc_C0976C                         ; C09764 m0x0
    eor.w #$000F                           ; C09766 m0x0
    and.w #$000F                           ; C09769 m0x0

loc_C0976C:
    clc                                    ; C0976C m0x0
    adc.w #$2FF0                           ; C0976D m0x0
    sta.w dma_pending_mask,y               ; C09770 m0x0
    iny                                    ; C09773 m0x0
    iny                                    ; C09774 m0x0
    iny                                    ; C09775 m0x0
    iny                                    ; C09776 m0x0

loc_C09777:
    dex                                    ; C09777 m0x0
    dex                                    ; C09778 m0x0
    bmi loc_C0977E                         ; C09779 m0x0
    jmp.w loc_C09691                       ; C0977B m0x0

loc_C0977E:
    sty.b oam_write_ptr                    ; C0977E m0x0
    rts                                    ; C09780 m0x0

particle_spawn_random:
    lda.w #$1F00                           ; C09781 m0x0
    jsr.w vram_generate_particle_tile      ; C09784 m0x0
    ldx.w #$003A                           ; C09787 m0x0

loc_C0978A:
    jsr.w random_next                      ; C0978A m0x0
    lda.b init_magic_AA55                  ; C0978D m0x0
    and.w #$07FF                           ; C0978F m0x0
    sta.l $7F0A06,x                        ; C09792 m0x0
    lda.b init_magic_FFFF                  ; C09796 m0x0
    and.w #$003F                           ; C09798 m0x0
    adc.w #$0060                           ; C0979B m0x0
    sta.l $7F0A86,x                        ; C0979E m0x0
    lda.w #$8000                           ; C097A2 m0x0
    sta.l $7F0B06,x                        ; C097A5 m0x0
    sta.l $7F0B86,x                        ; C097A9 m0x0
    lda.w #$FC00                           ; C097AD m0x0
    sta.l $7F0C86,x                        ; C097B0 m0x0
    lda.w #$0800                           ; C097B4 m0x0
    sta.l $7F0D06,x                        ; C097B7 m0x0
    tdc                                    ; C097BB m0x0
    sta.l $7F0D86,x                        ; C097BC m0x0
    sta.l $7F0E06,x                        ; C097C0 m0x0
    sta.l $7F0906,x                        ; C097C4 m0x0
    lda.w #$0700                           ; C097C8 m0x0
    sta.l $7F0C06,x                        ; C097CB m0x0
    lda.b init_magic_FFFF                  ; C097CF m0x0
    and.w #$00FF                           ; C097D1 m0x0
    sta.l $7F0986,x                        ; C097D4 m0x0
    dex                                    ; C097D8 m0x0
    dex                                    ; C097D9 m0x0
    bpl loc_C0978A                         ; C097DA m0x0
    rts                                    ; C097DC m0x0

loc_C097DD:
    lda.b oam_write_ptr                    ; C097DD m0x0
    tay                                    ; C097DF m0x0
    sec                                    ; C097E0 m0x0
    sbc.w #$0400                           ; C097E1 m0x0
    bmi loc_C097EE                         ; C097E4 m0x0
    rts                                    ; C097E6 m0x0

unused_particle_spawn_tail:
    incbin "../data/01.bin":$17E7..$17EE      ; 7 bytes

loc_C097EE:
    eor.w #$FFFF                           ; C097EE m0x0
    inc                                    ; C097F1 m0x0
    lsr                                    ; C097F2 m0x0
    cmp.w #$003C                           ; C097F3 m0x0
    bcc loc_C097FB                         ; C097F6 m0x0
    lda.w #$003A                           ; C097F8 m0x0

loc_C097FB:
    tax                                    ; C097FB m0x0

loc_C097FC:
    lda.l $7F0906,x                        ; C097FC m0x0
    bne loc_C0983E                         ; C09800 m0x0
    lda.l $7F0986,x                        ; C09802 m0x0
    beq loc_C09810                         ; C09806 m0x0
    dec                                    ; C09808 m0x0
    sta.l $7F0986,x                        ; C09809 m0x0
    jmp.w loc_C0988D                       ; C0980D m0x0

loc_C09810:
    jsr.w random_next                      ; C09810 m0x0
    lda.b init_magic_AA55                  ; C09813 m0x0
    and.w #$00FF                           ; C09815 m0x0
    sta.l $7F0986,x                        ; C09818 m0x0
    lda.w #$0400                           ; C0981C m0x0
    bit.b init_magic_AA55                  ; C0981F m0x0
    bpl loc_C09827                         ; C09821 m0x0
    eor.w #$FFFF                           ; C09823 m0x0
    inc                                    ; C09826 m0x0

loc_C09827:
    sta.l $7F0C86,x                        ; C09827 m0x0
    lda.w #$0800                           ; C0982B m0x0
    sta.l $7F0D06,x                        ; C0982E m0x0
    tdc                                    ; C09832 m0x0
    sta.l $7F0D86,x                        ; C09833 m0x0
    sta.l $7F0E06,x                        ; C09837 m0x0
    lda.w #$0021                           ; C0983B m0x0

loc_C0983E:
    dec                                    ; C0983E m0x0
    sta.l $7F0906,x                        ; C0983F m0x0
    lda.l $7F0D87,x                        ; C09843 m0x0
    and.w #$00FF                           ; C09847 m0x0
    asl                                    ; C0984A m0x0
    stx.b $06                              ; C0984B m0x0
    tax                                    ; C0984D m0x0
    lda.l data_C46588,x                    ; C0984E m0x0
    ldx.b $06                              ; C09852 m0x0
    adc.l $7F0B06,x                        ; C09854 m0x0
    sta.l $7F0B06,x                        ; C09858 m0x0
    lda.l $7F0E07,x                        ; C0985C m0x0
    and.w #$00FF                           ; C09860 m0x0
    asl                                    ; C09863 m0x0
    tax                                    ; C09864 m0x0
    lda.l data_C46588,x                    ; C09865 m0x0
    ldx.b $06                              ; C09869 m0x0
    adc.l $7F0B86,x                        ; C0986B m0x0
    sta.l $7F0B86,x                        ; C0986F m0x0
    lda.l $7F0D86,x                        ; C09873 m0x0
    clc                                    ; C09877 m0x0
    adc.l $7F0C86,x                        ; C09878 m0x0
    sta.l $7F0D86,x                        ; C0987C m0x0
    lda.l $7F0E06,x                        ; C09880 m0x0
    clc                                    ; C09884 m0x0
    adc.l $7F0D06,x                        ; C09885 m0x0
    sta.l $7F0E06,x                        ; C09889 m0x0

loc_C0988D:
    lda.l $7F0B07,x                        ; C0988D m0x0
    and.w #$00FF                           ; C09891 m0x0
    clc                                    ; C09894 m0x0
    adc.l $7F0A06,x                        ; C09895 m0x0
    sec                                    ; C09899 m0x0
    sbc.b camera_x                         ; C0989A m0x0
    bmi loc_C098D0                         ; C0989C m0x0
    cmp.w #$0100                           ; C0989E m0x0
    bcs loc_C098D0                         ; C098A1 m0x0
    sta.w nmi_handler_ptr,y                ; C098A3 m0x0
    lda.l $7F0B87,x                        ; C098A6 m0x0
    and.w #$00FF                           ; C098AA m0x0
    adc.l $7F0A86,x                        ; C098AD m0x0
    sec                                    ; C098B1 m0x0
    sbc.b camera_y                         ; C098B2 m0x0
    bmi loc_C098D0                         ; C098B4 m0x0
    cmp.w #$00E0                           ; C098B6 m0x0
    bcs loc_C098D0                         ; C098B9 m0x0
    sta.w $0001,y                          ; C098BB m0x0
    lda.l $7F0C07,x                        ; C098BE m0x0
    and.w #$000F                           ; C098C2 m0x0
    clc                                    ; C098C5 m0x0
    adc.w #$29F0                           ; C098C6 m0x0
    sta.w dma_pending_mask,y               ; C098C9 m0x0
    iny                                    ; C098CC m0x0
    iny                                    ; C098CD m0x0
    iny                                    ; C098CE m0x0
    iny                                    ; C098CF m0x0

loc_C098D0:
    dex                                    ; C098D0 m0x0
    dex                                    ; C098D1 m0x0
    bmi loc_C098D7                         ; C098D2 m0x0
    jmp.w loc_C097FC                       ; C098D4 m0x0

loc_C098D7:
    sty.b oam_write_ptr                    ; C098D7 m0x0
    rts                                    ; C098D9 m0x0

entity_update_tick:
    txy                                    ; C098DA m0x0
    lda.w entity_type,x                    ; C098DB m0x0
    tax                                    ; C098DE m0x0
    jsr.w (jtbl_C099DD,x)                  ; C098DF m0x0
    lda.w entity_hitstun_timer,x           ; C098E2 m0x0
    beq loc_C098EA                         ; C098E5 m0x0
    dec.w entity_hitstun_timer,x           ; C098E7 m0x0

loc_C098EA:
    lda.w entity_type,x                    ; C098EA m0x0
    tay                                    ; C098ED m0x0
    pea.w $8080                            ; C098EE m0x0
    plb                                    ; C098F1 m0x0
    lda.w entity_hitstun_timer,x           ; C098F2 m0x0
    beq loc_C098FC                         ; C098F5 m0x0
    lda.w entity_vel_x_target,x            ; C098F7 m0x0
    bne loc_C0990D                         ; C098FA m0x0

loc_C098FC:
    lda.w entity_state,x                   ; C098FC m0x0
    clc                                    ; C098FF m0x0
    adc.w $0BAC                            ; C09900 m0x0
    adc.w entity_state_velocity_table,y    ; C09903 m0x0
    tay                                    ; C09906 m0x0
    lda.w entity_state_velocity_table,y    ; C09907 m0x0
    sta.w entity_vel_x_target,x            ; C0990A m0x0

loc_C0990D:
    jsr.w entity_accelerate_velocity_x     ; C0990D m0x0
    lda.w entity_anim_id,x                 ; C09910 m0x0
    beq loc_C09927                         ; C09913 m0x0
    lda.w entity_state,x                   ; C09915 m0x0
    cmp.w #$000C                           ; C09918 m0x0
    bcc loc_C09927                         ; C0991B m0x0
    cmp.w #$0024                           ; C0991D m0x0
    bcs loc_C0993D                         ; C09920 m0x0
    cmp.w #$0018                           ; C09922 m0x0
    bcc loc_C0993D                         ; C09925 m0x0

loc_C09927:
    lda.w entity_vel_x,x                   ; C09927 m0x0
    bne loc_C09948                         ; C0992A m0x0
    stz.w entity_vel_y,x                   ; C0992C m0x0
    lda.w #$0000                           ; C0992F m0x0
    bit.w entity_flags,x                   ; C09932 m0x0
    bvs loc_C0993A                         ; C09935 m0x0
    lda.w #$0002                           ; C09937 m0x0

loc_C0993A:
    sta.w entity_state,x                   ; C0993A m0x0

loc_C0993D:
    jsr.w entity_ground_y_lookup           ; C0993D m0x0
    lda.w entity_flags,x                   ; C09940 m0x0
    cmp.w #$4000                           ; C09943 m0x0
    bra loc_C0995A                         ; C09946 m0x0

loc_C09948:
    jsr.w entity_ground_y_lookup           ; C09948 m0x0
    jsr.w entity_vel_y_from_vel_x          ; C0994B m0x0
    jsr.w entity_apply_velocity_x          ; C0994E m0x0
    jsr.w entity_apply_velocity_y          ; C09951 m0x0
    lda.w entity_vel_x,x                   ; C09954 m0x0
    cmp.w #$8000                           ; C09957 m0x0

loc_C0995A:
    rol                                    ; C0995A m0x0
    asl                                    ; C0995B m0x0
    and.w #$0002                           ; C0995C m0x0
    ora.w $0BAE                            ; C0995F m0x0
    and.w #$000E                           ; C09962 m0x0
    asl                                    ; C09965 m0x0
    tay                                    ; C09966 m0x0
    lda.w entity_flags,x                   ; C09967 m0x0
    and.w #$BFFF                           ; C0996A m0x0
    ora.w facing_flag_table,y              ; C0996D m0x0
    sta.w entity_flags,x                   ; C09970 m0x0
    lda.w entity_state,x                   ; C09973 m0x0
    and.w #$FFFC                           ; C09976 m0x0
    ora.w facing_state_bits_table,y        ; C09979 m0x0
    sta.w entity_state,x                   ; C0997C m0x0
    lda.w entity_type,x                    ; C0997F m0x0
    tay                                    ; C09982 m0x0
    lda.w entity_state,x                   ; C09983 m0x0
    clc                                    ; C09986 m0x0
    adc.w $0BAC                            ; C09987 m0x0
    adc.w entity_state_anim_table,y        ; C0998A m0x0
    tay                                    ; C0998D m0x0
    lda.w entity_state_anim_table,y        ; C0998E m0x0
    sta.w entity_anim_id,x                 ; C09991 m0x0
    lda.w entity_type,x                    ; C09994 m0x0
    tay                                    ; C09997 m0x0
    lda.w entity_state,x                   ; C09998 m0x0
    adc.w $0BAC                            ; C0999B m0x0
    lsr                                    ; C0999E m0x0
    and.w #$FFFE                           ; C0999F m0x0
    adc.w anim_rate_fn_table,y             ; C099A2 m0x0
    tay                                    ; C099A5 m0x0
    lda.w anim_rate_fn_table,y             ; C099A6 m0x0
    beq anim_rate_store                    ; C099A9 m0x0
    sta.b ptr_04                           ; C099AB m0x0
    lda.w entity_vel_x,x                   ; C099AD m0x0
    bpl loc_C099B6                         ; C099B0 m0x0
    eor.w #$FFFF                           ; C099B2 m0x0
    inc                                    ; C099B5 m0x0

loc_C099B6:
    jmp.w ($0004)                          ; C099B6 m0x0

anim_rate_3_16:
    lsr                                    ; C099B9 m0x0

anim_rate_3_8:
    lsr                                    ; C099BA m0x0
    lsr                                    ; C099BB m0x0
    sta.b $18                              ; C099BC m0x0
    lsr                                    ; C099BE m0x0
    clc                                    ; C099BF m0x0
    adc.b $18                              ; C099C0 m0x0
    bra anim_rate_store                    ; C099C2 m0x0

anim_rate_3_32:
    lsr                                    ; C099C4 m0x0
    lsr                                    ; C099C5 m0x0
    lsr                                    ; C099C6 m0x0
    lsr                                    ; C099C7 m0x0
    sta.b $18                              ; C099C8 m0x0
    lsr                                    ; C099CA m0x0
    clc                                    ; C099CB m0x0
    adc.b $18                              ; C099CC m0x0
    bra anim_rate_store                    ; C099CE m0x0

anim_rate_1_16:
    lsr                                    ; C099D0 m0x0

anim_rate_1_8:
    lsr                                    ; C099D1 m0x0

anim_rate_1_4:
    lsr                                    ; C099D2 m0x0

anim_rate_1_2:
    lsr                                    ; C099D3 m0x0

anim_rate_store:
    sta.w entity_anim_rate,x               ; C099D4 m0x0
    plb                                    ; C099D7 m0x0
    jsl.l $800000+(anim_update&$FFFF)      ; C099D8 m0x0
    rts                                    ; C099DC m0x0

jtbl_C099DD:
    dw entity_animate_only
    dw entity_apply_hit_reaction
    dw entity_spawn_transform_a
    dw entity_spawn_transform_b
    dw entity_spawn_transform_b
    dw entity_spawn_transform_c
    dw entity_ai_chase_player
    dw entity_ai_none
    dw entity_ai_none

entity_ai_chase_player:
    tyx                                    ; C099EF m0x0
    lda.w entity_type,x                    ; C099F0 m0x0
    cmp.w #$000C                           ; C099F3 m0x0
    bne loc_C09A51                         ; C099F6 m0x0
    lda.w entity_hitstun_timer,x           ; C099F8 m0x0
    bne loc_C09A51                         ; C099FB m0x0
    lda.w entity_state                     ; C099FD m0x0
    cmp.w #$000C                           ; C09A00 m0x0
    bcs loc_C09A11                         ; C09A03 m0x0
    lda.w $0748,x                          ; C09A05 m0x0
    bmi loc_C09A11                         ; C09A08 m0x0
    bne loc_C09A49                         ; C09A0A m0x0
    lda.w $0A08,x                          ; C09A0C m0x0
    bne loc_C09A51                         ; C09A0F m0x0

loc_C09A11:
    lda.w entity_x                         ; C09A11 m0x0
    sec                                    ; C09A14 m0x0
    sbc.w entity_x,x                       ; C09A15 m0x0
    sta.b ptr_04                           ; C09A18 m0x0
    bpl loc_C09A20                         ; C09A1A m0x0
    eor.w #$FFFF                           ; C09A1C m0x0
    inc                                    ; C09A1F m0x0

loc_C09A20:
    cmp.w #$0040                           ; C09A20 m0x0
    bcc loc_C09A52                         ; C09A23 m0x0
    cmp.w #$0080                           ; C09A25 m0x0
    bcc loc_C09A2F                         ; C09A28 m0x0
    lda.w #$0008                           ; C09A2A m0x0
    bra loc_C09A32                         ; C09A2D m0x0

loc_C09A2F:
    lda.w #$0004                           ; C09A2F m0x0

loc_C09A32:
    bit.b ptr_04                           ; C09A32 m0x0
    bmi loc_C09A38                         ; C09A34 m0x0
    inc                                    ; C09A36 m0x0
    inc                                    ; C09A37 m0x0

loc_C09A38:
    sta.w entity_state,x                   ; C09A38 m0x0
    jsr.w random_next                      ; C09A3B m0x0
    lda.b init_magic_AA55                  ; C09A3E m0x0
    and.w #$003F                           ; C09A40 m0x0
    ora.w #$8000                           ; C09A43 m0x0
    sta.w $0748,x                          ; C09A46 m0x0

loc_C09A49:
    sep.b #$20                             ; C09A49 m0x0
    dec                                    ; C09A4B m1x0
    rep.b #$20                             ; C09A4C m1x0
    sta.w $0748,x                          ; C09A4E m0x0

loc_C09A51:
    rts                                    ; C09A51 m0x0

loc_C09A52:
    lda.w #$0000                           ; C09A52 m0x0
    sta.w entity_state,x                   ; C09A55 m0x0
    lda.w #$0001                           ; C09A58 m0x0
    sta.w $0748,x                          ; C09A5B m0x0
    rts                                    ; C09A5E m0x0

entity_ai_none:
    tyx                                    ; C09A5F m0x0
    rts                                    ; C09A60 m0x0

entity_apply_hit_reaction:
    tyx                                    ; C09A61 m0x0
    lda.b $8A                              ; C09A62 m0x0
    ldy.b $8C                              ; C09A64 m0x0

loc_C09A66:
    sta.b ptr_04                           ; C09A66 m0x0
    lda.w entity_hitstun_timer,x           ; C09A68 m0x0
    bne loc_C09AA9                         ; C09A6B m0x0
    lda.w entity_state,x                   ; C09A6D m0x0
    cmp.w #$000C                           ; C09A70 m0x0
    bcc loc_C09A7F                         ; C09A73 m0x0
    cmp.w #$0024                           ; C09A75 m0x0
    bcs loc_C09AB7                         ; C09A78 m0x0
    cmp.w #$0018                           ; C09A7A m0x0
    bcc loc_C09AB7                         ; C09A7D m0x0

loc_C09A7F:
    lda.b ptr_04                           ; C09A7F m0x0
    bit.w #$0200                           ; C09A81 m0x0
    beq loc_C09A8B                         ; C09A84 m0x0
    lda.w #$0004                           ; C09A86 m0x0
    bra loc_C09A93                         ; C09A89 m0x0

loc_C09A8B:
    bit.w #$0100                           ; C09A8B m0x0
    beq loc_C09A9D                         ; C09A8E m0x0
    lda.w #$0006                           ; C09A90 m0x0

loc_C09A93:
    bit.b ptr_04                           ; C09A93 m0x0
    bvc loc_C09A9A                         ; C09A95 m0x0
    adc.w #$0004                           ; C09A97 m0x0

loc_C09A9A:
    sta.w entity_state,x                   ; C09A9A m0x0

loc_C09A9D:
    tya                                    ; C09A9D m0x0
    bit.w #$8000                           ; C09A9E m0x0
    beq loc_C09AAA                         ; C09AA1 m0x0
    lda.w #$000C                           ; C09AA3 m0x0
    sta.w entity_state,x                   ; C09AA6 m0x0

loc_C09AA9:
    rts                                    ; C09AA9 m0x0

loc_C09AAA:
    lda.b ptr_04                           ; C09AAA m0x0
    and.w #$0300                           ; C09AAC m0x0
    bne loc_C09AB7                         ; C09AAF m0x0
    lda.w #$0000                           ; C09AB1 m0x0
    sta.w entity_state,x                   ; C09AB4 m0x0

loc_C09AB7:
    rts                                    ; C09AB7 m0x0

entity_spawn_transform_a:
    tyx                                    ; C09AB8 m0x0
    lda.w entity_parent_index,x            ; C09AB9 m0x0
    tay                                    ; C09ABC m0x0
    lda.w entity_x,y                       ; C09ABD m0x0
    sta.w entity_x,x                       ; C09AC0 m0x0
    lda.w entity_y,y                       ; C09AC3 m0x0
    sta.w entity_y,x                       ; C09AC6 m0x0
    lda.w $08E8,y                          ; C09AC9 m0x0
    sta.w $08E8,x                          ; C09ACC m0x0
    lda.w entity_flags,y                   ; C09ACF m0x0
    eor.w entity_flags,x                   ; C09AD2 m0x0
    and.w #$7000                           ; C09AD5 m0x0
    eor.w entity_flags,x                   ; C09AD8 m0x0
    sta.w entity_flags,x                   ; C09ADB m0x0
    lda.w entity_anim_id,y                 ; C09ADE m0x0
    beq loc_C09AE7                         ; C09AE1 m0x0
    clc                                    ; C09AE3 m0x0
    adc.w #$0002                           ; C09AE4 m0x0

loc_C09AE7:
    sta.w entity_anim_id,x                 ; C09AE7 m0x0
    lda.w entity_anim_rate,y               ; C09AEA m0x0
    sta.w entity_anim_rate,x               ; C09AED m0x0
    jsl.l $800000+(anim_update&$FFFF)      ; C09AF0 m0x0
    pla                                    ; C09AF4 m0x0
    rts                                    ; C09AF5 m0x0

entity_animate_only:
    tyx                                    ; C09AF6 m0x0
    jsl.l $800000+(anim_update&$FFFF)      ; C09AF7 m0x0
    pla                                    ; C09AFB m0x0
    rts                                    ; C09AFC m0x0

unused_entity_animate_tail:
    incbin "../data/01.bin":$1AFD..$1B00      ; 3 bytes

entity_spawn_transform_b:
    tyx                                    ; C09B00 m0x0
    lda.w $0BAC                            ; C09B01 m0x0
    bne loc_C09B51                         ; C09B04 m0x0
    lda.b game_mode                        ; C09B06 m0x0
    cmp.w #$0002                           ; C09B08 m0x0
    beq loc_C09B4F                         ; C09B0B m0x0
    lda.w entity_parent_index,x            ; C09B0D m0x0
    tay                                    ; C09B10 m0x0
    lda.w entity_x,y                       ; C09B11 m0x0
    sta.w entity_x,x                       ; C09B14 m0x0
    lda.w $08E8,y                          ; C09B17 m0x0
    clc                                    ; C09B1A m0x0
    adc.w #$0010                           ; C09B1B m0x0
    sta.w $08E8,x                          ; C09B1E m0x0
    lda.w entity_y,y                       ; C09B21 m0x0
    sta.w entity_y,x                       ; C09B24 m0x0
    lda.w entity_flags,y                   ; C09B27 m0x0
    eor.w entity_flags,x                   ; C09B2A m0x0
    and.w #$4000                           ; C09B2D m0x0
    eor.w entity_flags,x                   ; C09B30 m0x0
    ora.w #$8000                           ; C09B33 m0x0
    sta.w entity_flags,x                   ; C09B36 m0x0
    lda.w entity_anim_id,y                 ; C09B39 m0x0
    beq loc_C09B42                         ; C09B3C m0x0
    clc                                    ; C09B3E m0x0
    adc.w #$0004                           ; C09B3F m0x0

loc_C09B42:
    sta.w entity_anim_id,x                 ; C09B42 m0x0
    lda.w entity_anim_rate,y               ; C09B45 m0x0
    sta.w entity_anim_rate,x               ; C09B48 m0x0
    jsl.l $800000+(anim_update&$FFFF)      ; C09B4B m0x0

loc_C09B4F:
    pla                                    ; C09B4F m0x0
    rts                                    ; C09B50 m0x0

loc_C09B51:
    lda.w entity_parent_index,x            ; C09B51 m0x0
    tay                                    ; C09B54 m0x0
    lda.w entity_x,y                       ; C09B55 m0x0
    sta.w entity_x,x                       ; C09B58 m0x0
    lda.w $08E8,y                          ; C09B5B m0x0
    sta.w $08E8,x                          ; C09B5E m0x0
    lda.w entity_y,y                       ; C09B61 m0x0
    dec                                    ; C09B64 m0x0
    sta.w entity_y,x                       ; C09B65 m0x0
    lda.w entity_flags,y                   ; C09B68 m0x0
    eor.w entity_flags,x                   ; C09B6B m0x0
    and.w #$C000                           ; C09B6E m0x0
    eor.w entity_flags,x                   ; C09B71 m0x0
    sta.w entity_flags,x                   ; C09B74 m0x0
    lda.w #$0158                           ; C09B77 m0x0
    sta.w entity_anim_id,x                 ; C09B7A m0x0
    lda.w entity_anim_rate,y               ; C09B7D m0x0
    sta.w entity_anim_rate,x               ; C09B80 m0x0
    jsl.l $800000+(anim_update&$FFFF)      ; C09B83 m0x0
    pla                                    ; C09B87 m0x0
    rts                                    ; C09B88 m0x0

entity_spawn_transform_c:
    tyx                                    ; C09B89 m0x0
    lda.w entity_parent_index,x            ; C09B8A m0x0
    tay                                    ; C09B8D m0x0
    lda.w entity_x,y                       ; C09B8E m0x0
    sta.w entity_x,x                       ; C09B91 m0x0
    lda.w entity_y,y                       ; C09B94 m0x0
    sta.w entity_y,x                       ; C09B97 m0x0
    lda.w $08E8,y                          ; C09B9A m0x0
    sta.w $08E8,x                          ; C09B9D m0x0
    lda.w entity_flags,y                   ; C09BA0 m0x0
    eor.w entity_flags,x                   ; C09BA3 m0x0
    and.w #$7000                           ; C09BA6 m0x0
    eor.w entity_flags,x                   ; C09BA9 m0x0
    sta.w entity_flags,x                   ; C09BAC m0x0
    lda.w $0BB6                            ; C09BAF m0x0
    beq loc_C09BD5                         ; C09BB2 m0x0
    lda.w #$0002                           ; C09BB4 m0x0
    sta.w $07A8,x                          ; C09BB7 m0x0
    lda.w entity_anim_id,y                 ; C09BBA m0x0
    beq loc_C09BC6                         ; C09BBD m0x0
    clc                                    ; C09BBF m0x0
    adc.w #$0004                           ; C09BC0 m0x0
    adc.w $0BB6                            ; C09BC3 m0x0

loc_C09BC6:
    sta.w entity_anim_id,x                 ; C09BC6 m0x0
    lda.w entity_anim_rate,y               ; C09BC9 m0x0
    sta.w entity_anim_rate,x               ; C09BCC m0x0
    jsl.l $800000+(anim_update&$FFFF)      ; C09BCF m0x0
    pla                                    ; C09BD3 m0x0
    rts                                    ; C09BD4 m0x0

loc_C09BD5:
    stz.w $07A8,x                          ; C09BD5 m0x0
    pla                                    ; C09BD8 m0x0
    rts                                    ; C09BD9 m0x0

entity_ground_y_lookup:
    txy                                    ; C09BDA m0x0
    lda.w entity_x,x                       ; C09BDB m0x0
    bpl loc_C09BE1                         ; C09BDE m0x0
    tdc                                    ; C09BE0 m0x0

loc_C09BE1:
    sta.b ptr_04                           ; C09BE1 m0x0
    stz.w $0BAE                            ; C09BE3 m0x0
    lda.b game_mode                        ; C09BE6 m0x0
    asl                                    ; C09BE8 m0x0
    tax                                    ; C09BE9 m0x0
    lda.l $800000+(ground_y_lookup_threshold&$FFFF),x   ; C09BEA m0x0

loc_C09BEE:
    tax                                    ; C09BEE m0x0
    lda.l $800000+(ground_y_lookup_threshold&$FFFF),x   ; C09BEF m0x0
    beq loc_C09C03                         ; C09BF3 m0x0
    cmp.b ptr_04                           ; C09BF5 m0x0
    bcs loc_C09BFF                         ; C09BF7 m0x0
    txa                                    ; C09BF9 m0x0
    adc.w #$0006                           ; C09BFA m0x0
    bra loc_C09BEE                         ; C09BFD m0x0

loc_C09BFF:
    lda.l $800000+(ground_y_lookup_default&$FFFF),x   ; C09BFF m0x0

loc_C09C03:
    sta.w $0BAE                            ; C09C03 m0x0
    beq loc_C09C14                         ; C09C06 m0x0
    bmi loc_C09C14                         ; C09C08 m0x0
    lda.l $800000+(entity_state_ground_y_table&$FFFF),x   ; C09C0A m0x0
    beq loc_C09C14                         ; C09C0E m0x0
    tyx                                    ; C09C10 m0x0
    sta.w entity_y,x                       ; C09C11 m0x0

loc_C09C14:
    tyx                                    ; C09C14 m0x0
    rts                                    ; C09C15 m0x0

vram_generate_particle_tile:
    ldy.w #$0080                           ; C09C16 m0x0
    sty.w VMAIN                            ; C09C19 m0x0
    sta.w VMADDL                           ; C09C1C m0x0
    ldy.w #$0001                           ; C09C1F m0x0
    jsr.w vram_write_tile_row_planes       ; C09C22 m0x0
    ldy.w #$000F                           ; C09C25 m0x0

vram_write_tile_row_planes:
    tdc                                    ; C09C28 m0x0

loc_C09C29:
    sty.b ptr_04                           ; C09C29 m0x0
    ror.b ptr_04                           ; C09C2B m0x0
    ror                                    ; C09C2D m0x0
    xba                                    ; C09C2E m0x0
    sta.b $06                              ; C09C2F m0x0
    tdc                                    ; C09C31 m0x0
    ror.b ptr_04                           ; C09C32 m0x0
    ror                                    ; C09C34 m0x0
    ora.b $06                              ; C09C35 m0x0
    sta.w VMDATAL                          ; C09C37 m0x0
    lda.w #$0007                           ; C09C3A m0x0

loc_C09C3D:
    stz.w VMDATAL                          ; C09C3D m0x0
    dec                                    ; C09C40 m0x0
    bne loc_C09C3D                         ; C09C41 m0x0
    ror.b ptr_04                           ; C09C43 m0x0
    ror                                    ; C09C45 m0x0
    xba                                    ; C09C46 m0x0
    sta.b $06                              ; C09C47 m0x0
    tdc                                    ; C09C49 m0x0
    ror.b ptr_04                           ; C09C4A m0x0
    ror                                    ; C09C4C m0x0
    ora.b $06                              ; C09C4D m0x0
    sta.w VMDATAL                          ; C09C4F m0x0
    lda.w #$0007                           ; C09C52 m0x0

loc_C09C55:
    stz.w VMDATAL                          ; C09C55 m0x0
    dec                                    ; C09C58 m0x0
    bne loc_C09C55                         ; C09C59 m0x0
    iny                                    ; C09C5B m0x0
    cpy.w #$0010                           ; C09C5C m0x0
    bcc loc_C09C29                         ; C09C5F m0x0
    rts                                    ; C09C61 m0x0

vram_stream_descriptor_dispatch:
    sta.b ptr_04                           ; C09C62 m0x0
    lda.w $0BC6                            ; C09C64 m0x0
    bpl loc_C09C84                         ; C09C67 m0x0
    lda.b camera_x                         ; C09C69 m0x0
    xba                                    ; C09C6B m0x0
    and.w #$00FF                           ; C09C6C m0x0
    asl                                    ; C09C6F m0x0
    adc.b ptr_04                           ; C09C70 m0x0
    tax                                    ; C09C72 m0x0
    lda.l $800000+(stream_zone_index_table&$FFFF),x   ; C09C73 m0x0
    bmi loc_C09C93                         ; C09C77 m0x0
    cmp.w $0BC4                            ; C09C79 m0x0
    beq loc_C09C93                         ; C09C7C m0x0
    sta.w $0BC4                            ; C09C7E m0x0
    sta.w $0BC6                            ; C09C81 m0x0

loc_C09C84:
    tax                                    ; C09C84 m0x0
    lda.w $0BCA                            ; C09C85 m0x0
    bne loc_C09CB8                         ; C09C88 m0x0
    lda.l $800000+(vram_stream_desc_table&$FFFF),x   ; C09C8A m0x0
    bpl loc_C09C94                         ; C09C8E m0x0
    sta.w $0BC6                            ; C09C90 m0x0

loc_C09C93:
    rts                                    ; C09C93 m0x0

loc_C09C94:
    sta.w $0BCC                            ; C09C94 m0x0
    lda.l $800000+(vram_stream_desc_bank&$FFFF),x   ; C09C97 m0x0
    bpl loc_C09D02                         ; C09C9B m0x0
    sta.w $0BD0                            ; C09C9D m0x0
    lda.l $800000+(vram_stream_desc_addr&$FFFF),x   ; C09CA0 m0x0
    sta.w $0BCE                            ; C09CA4 m0x0
    lda.l $800000+(vram_stream_desc_payload&$FFFF),x   ; C09CA7 m0x0
    sta.w $0BC8                            ; C09CAB m0x0
    txa                                    ; C09CAE m0x0
    clc                                    ; C09CAF m0x0
    adc.w #$0008                           ; C09CB0 m0x0
    sta.w $0BC6                            ; C09CB3 m0x0
    bra loc_C09CE7                         ; C09CB6 m0x0

loc_C09CB8:
    ldx.w $0A88                            ; C09CB8 m0x0
    lda.w $0BD0                            ; C09CBB m0x0
    sta.w $0A90,x                          ; C09CBE m0x0
    lda.w $0BCC                            ; C09CC1 m0x0
    sta.w $0A8C,x                          ; C09CC4 m0x0
    lda.w $0BCA                            ; C09CC7 m0x0
    sta.w $0A8A,x                          ; C09CCA m0x0
    lsr                                    ; C09CCD m0x0
    adc.w $0BCC                            ; C09CCE m0x0
    sta.w $0BCC                            ; C09CD1 m0x0
    lda.w $0BCE                            ; C09CD4 m0x0
    sta.w $0A8E,x                          ; C09CD7 m0x0
    adc.w $0BCA                            ; C09CDA m0x0
    sta.w $0BCE                            ; C09CDD m0x0
    txa                                    ; C09CE0 m0x0
    adc.w #$0008                           ; C09CE1 m0x0
    sta.w $0A88                            ; C09CE4 m0x0

loc_C09CE7:
    ldy.w #$0400                           ; C09CE7 m0x0
    lda.w $0BC8                            ; C09CEA m0x0
    beq loc_C09CF9                         ; C09CED m0x0
    sec                                    ; C09CEF m0x0
    sbc.w #$0400                           ; C09CF0 m0x0
    bpl loc_C09CFB                         ; C09CF3 m0x0
    eor.w #$FFFF                           ; C09CF5 m0x0
    inc                                    ; C09CF8 m0x0

loc_C09CF9:
    tay                                    ; C09CF9 m0x0
    tdc                                    ; C09CFA m0x0

loc_C09CFB:
    sta.w $0BC8                            ; C09CFB m0x0
    sty.w $0BCA                            ; C09CFE m0x0
    rts                                    ; C09D01 m0x0

loc_C09D02:
    ldy.w $0B8A                            ; C09D02 m0x0
    sta.w $0B92,y                          ; C09D05 m0x0
    lda.w $0BCC                            ; C09D08 m0x0
    sta.w $0B8E,y                          ; C09D0B m0x0
    lda.l $800000+(vram_stream_desc_payload&$FFFF),x   ; C09D0E m0x0
    sta.w $0B8C,y                          ; C09D12 m0x0
    lda.l $800000+(vram_stream_desc_addr&$FFFF),x   ; C09D15 m0x0
    sta.w $0B90,y                          ; C09D19 m0x0
    tya                                    ; C09D1C m0x0
    clc                                    ; C09D1D m0x0
    adc.w #$0008                           ; C09D1E m0x0
    sta.w $0B8A                            ; C09D21 m0x0
    txa                                    ; C09D24 m0x0
    adc.w #$0008                           ; C09D25 m0x0
    sta.w $0BC6                            ; C09D28 m0x0
    stz.w $0BC8                            ; C09D2B m0x0
    stz.w $0BCA                            ; C09D2E m0x0
    rts                                    ; C09D31 m0x0

cgram_upload_queue_flush:
    ldx.w $0B8A                            ; C09D32 m0x0
    beq loc_C09D69                         ; C09D35 m0x0
    lda.w #$2202                           ; C09D37 m0x0
    sta.w DMAP0                            ; C09D3A m0x0

loc_C09D3D:
    lda.w $0B84,x                          ; C09D3D m0x0
    sta.w DASL0                            ; C09D40 m0x0
    lda.w $0B88,x                          ; C09D43 m0x0
    sta.w A1TL0                            ; C09D46 m0x0
    sep.b #$20                             ; C09D49 m0x0
    lda.w $0B8A,x                          ; C09D4B m1x0
    sta.w A1B0                             ; C09D4E m1x0
    lda.w $0B86,x                          ; C09D51 m1x0
    sta.w CGADD                            ; C09D54 m1x0
    lda.b #$01                             ; C09D57 m1x0
    sta.w MDMAEN                           ; C09D59 m1x0
    rep.b #$20                             ; C09D5C m1x0
    txa                                    ; C09D5E m0x0
    sec                                    ; C09D5F m0x0
    sbc.w #$0008                           ; C09D60 m0x0
    tax                                    ; C09D63 m0x0
    bne loc_C09D3D                         ; C09D64 m0x0
    stz.w $0B8A                            ; C09D66 m0x0

loc_C09D69:
    rts                                    ; C09D69 m0x0

entity_init_from_table:
    pea.w $8000                            ; C09D6A m0x0
    plb                                    ; C09D6D m0x0
    stz.b $A6                              ; C09D6E m0x0
    lda.b game_mode                        ; C09D70 m0x0
    asl                                    ; C09D72 m0x0
    tax                                    ; C09D73 m0x0
    lda.l $800000+(entity_init_table&$FFFF),x   ; C09D74 m0x0
    ldy.w #$0000                           ; C09D78 m0x0

loc_C09D7B:
    tax                                    ; C09D7B m0x0
    lda.l $800000+(entity_init_table&$FFFF),x   ; C09D7C m0x0
    bpl loc_C09D85                         ; C09D80 m0x0
    jmp.w loc_C09E28                       ; C09D82 m0x0

loc_C09D85:
    sta.w entity_type,y                    ; C09D85 m0x0
    sta.b ptr_04                           ; C09D88 m0x0
    lda.l $800000+(entity_init_state&$FFFF),x   ; C09D8A m0x0
    sta.w entity_state,y                   ; C09D8E m0x0
    clc                                    ; C09D91 m0x0
    adc.w $0BAC                            ; C09D92 m0x0
    phx                                    ; C09D95 m0x0
    ldx.b ptr_04                           ; C09D96 m0x0
    adc.l $800000+(entity_state_anim_table&$FFFF),x   ; C09D98 m0x0
    tax                                    ; C09D9C m0x0
    lda.l $800000+(entity_state_anim_table&$FFFF),x   ; C09D9D m0x0
    plx                                    ; C09DA1 m0x0
    sta.w entity_anim_id,y                 ; C09DA2 m0x0
    lda.l $800000+(entity_init_unk_07a8&$FFFF),x   ; C09DA5 m0x0
    sta.w $07A8,y                          ; C09DA9 m0x0
    lda.l $800000+(entity_init_x&$FFFF),x   ; C09DAC m0x0
    sta.w entity_x,y                       ; C09DB0 m0x0
    lda.l $800000+(entity_init_y&$FFFF),x   ; C09DB3 m0x0
    sta.w entity_y,y                       ; C09DB7 m0x0
    lda.l $800000+(entity_init_unk_08e8&$FFFF),x   ; C09DBA m0x0
    sta.w $08E8,y                          ; C09DBE m0x0
    lda.l $800000+(entity_init_flags&$FFFF),x   ; C09DC1 m0x0
    sta.w entity_flags,y                   ; C09DC5 m0x0
    lda.l $800000+(entity_init_parent&$FFFF),x   ; C09DC8 m0x0
    sta.w entity_parent_index,y            ; C09DCC m0x0
    lda.l $800000+(entity_init_hitstun&$FFFF),x   ; C09DCF m0x0
    sta.w entity_hitstun_timer,y           ; C09DD3 m0x0
    lda.w entity_type,y                    ; C09DD6 m0x0
    bne loc_C09DE2                         ; C09DD9 m0x0
    lda.l $800000+(entity_init_state&$FFFF),x   ; C09DDB m0x0
    sta.w entity_anim_id,y                 ; C09DDF m0x0

loc_C09DE2:
    tdc                                    ; C09DE2 m0x0
    sta.w entity_frame_id,y                ; C09DE3 m0x0
    sta.w $07E8,y                          ; C09DE6 m0x0
    sta.w $0808,y                          ; C09DE9 m0x0
    sta.w $0748,y                          ; C09DEC m0x0
    sta.w entity_x_sub,y                   ; C09DEF m0x0
    sta.w entity_vel_x,y                   ; C09DF2 m0x0
    sta.w entity_vel_x_target,y            ; C09DF5 m0x0
    sta.w entity_y_sub,y                   ; C09DF8 m0x0
    sta.w $0908,y                          ; C09DFB m0x0
    sta.w $0928,y                          ; C09DFE m0x0
    sta.w entity_vel_y,y                   ; C09E01 m0x0
    sta.w entity_depth_key,y               ; C09E04 m0x0
    sta.w $09C8,y                          ; C09E07 m0x0
    sta.w $0A08,y                          ; C09E0A m0x0
    sta.w $0A28,y                          ; C09E0D m0x0
    sta.w $0A48,y                          ; C09E10 m0x0
    sta.w entity_anim_rate,y               ; C09E13 m0x0
    inc.b $A6                              ; C09E16 m0x0
    inc.b $A6                              ; C09E18 m0x0
    iny                                    ; C09E1A m0x0
    iny                                    ; C09E1B m0x0
    cpy.w #$0020                           ; C09E1C m0x0
    bcs loc_C09E81                         ; C09E1F m0x0
    txa                                    ; C09E21 m0x0
    adc.w #$0012                           ; C09E22 m0x0
    jmp.w loc_C09D7B                       ; C09E25 m0x0

loc_C09E28:
    tdc                                    ; C09E28 m0x0

loc_C09E29:
    sta.w entity_type,y                    ; C09E29 m0x0
    sta.w entity_state,y                   ; C09E2C m0x0
    sta.w entity_hitstun_timer,y           ; C09E2F m0x0
    sta.w $0748,y                          ; C09E32 m0x0
    sta.w entity_anim_id,y                 ; C09E35 m0x0
    sta.w $07A8,y                          ; C09E38 m0x0
    sta.w entity_x,y                       ; C09E3B m0x0
    sta.w entity_y,y                       ; C09E3E m0x0
    sta.w $08E8,y                          ; C09E41 m0x0
    sta.w entity_flags,y                   ; C09E44 m0x0
    sta.w entity_parent_index,y            ; C09E47 m0x0
    sta.w entity_frame_id,y                ; C09E4A m0x0
    sta.w $07E8,y                          ; C09E4D m0x0
    sta.w $0808,y                          ; C09E50 m0x0
    sta.w entity_x_sub,y                   ; C09E53 m0x0
    sta.w entity_vel_x,y                   ; C09E56 m0x0
    sta.w entity_vel_x_target,y            ; C09E59 m0x0
    sta.w entity_y_sub,y                   ; C09E5C m0x0
    sta.w $0908,y                          ; C09E5F m0x0
    sta.w $0928,y                          ; C09E62 m0x0
    sta.w entity_vel_y,y                   ; C09E65 m0x0
    sta.w entity_depth_key,y               ; C09E68 m0x0
    sta.w $09C8,y                          ; C09E6B m0x0
    sta.w $0A08,y                          ; C09E6E m0x0
    sta.w $0A28,y                          ; C09E71 m0x0
    sta.w $0A48,y                          ; C09E74 m0x0
    sta.w entity_anim_rate,y               ; C09E77 m0x0
    iny                                    ; C09E7A m0x0
    iny                                    ; C09E7B m0x0
    cpy.w #$0020                           ; C09E7C m0x0
    bcc loc_C09E29                         ; C09E7F m0x0

loc_C09E81:
    plb                                    ; C09E81 m0x0
    rts                                    ; C09E82 m0x0

build_metatile_column_500:
    lda.b camera_x                         ; C09E83 m0x0
    and.w #$FFE0                           ; C09E85 m0x0
    ldy.b $82                              ; C09E88 m0x0
    bmi loc_C09E91                         ; C09E8A m0x0
    beq loc_C09E97                         ; C09E8C m0x0
    lsr                                    ; C09E8E m0x0
    bra loc_C09E97                         ; C09E8F m0x0

loc_C09E91:
    sta.b ptr_04                           ; C09E91 m0x0
    lsr                                    ; C09E93 m0x0
    clc                                    ; C09E94 m0x0
    adc.b ptr_04                           ; C09E95 m0x0

loc_C09E97:
    clc                                    ; C09E97 m0x0
    adc.b tilemap_a_addr                   ; C09E98 m0x0
    sta.b $18                              ; C09E9A m0x0
    lda.b tilemap_a_bank                   ; C09E9C m0x0
    sta.b $1A                              ; C09E9E m0x0
    lda.b camera_y_lookahead               ; C09EA0 m0x0
    bpl loc_C09EA8                         ; C09EA2 m0x0
    lda.b camera_y                         ; C09EA4 m0x0
    bra loc_C09EAE                         ; C09EA6 m0x0

loc_C09EA8:
    lda.b camera_y                         ; C09EA8 m0x0
    clc                                    ; C09EAA m0x0
    adc.w #$00E0                           ; C09EAB m0x0

loc_C09EAE:
    tay                                    ; C09EAE m0x0
    and.w #$FFE0                           ; C09EAF m0x0
    lsr                                    ; C09EB2 m0x0
    lsr                                    ; C09EB3 m0x0
    lsr                                    ; C09EB4 m0x0
    lsr                                    ; C09EB5 m0x0
    adc.b $18                              ; C09EB6 m0x0
    sta.b $18                              ; C09EB8 m0x0
    tya                                    ; C09EBA m0x0
    and.w #$0018                           ; C09EBB m0x0
    adc.b tilemap_b_addr                   ; C09EBE m0x0
    sta.b $1C                              ; C09EC0 m0x0
    tya                                    ; C09EC2 m0x0
    and.w #$0018                           ; C09EC3 m0x0
    eor.w #$0018                           ; C09EC6 m0x0
    adc.b tilemap_b_addr                   ; C09EC9 m0x0
    sta.b $1E                              ; C09ECB m0x0
    phk                                    ; C09ECD m0x0
    ldx.w #$06C0                           ; C09ECE m0x0

loc_C09ED1:
    lda.b metatile_data_bank               ; C09ED1 m0x0
    pha                                    ; C09ED3 m0x0
    plb                                    ; C09ED4 m0x0
    plb                                    ; C09ED5 m0x0
    lda.b [$18]                            ; C09ED6 m0x0
    bmi loc_C09F39                         ; C09ED8 m0x0
    bit.w #$4000                           ; C09EDA m0x0
    bne loc_C09F11                         ; C09EDD m0x0
    asl                                    ; C09EDF m0x0
    asl                                    ; C09EE0 m0x0
    asl                                    ; C09EE1 m0x0
    asl                                    ; C09EE2 m0x0
    asl                                    ; C09EE3 m0x0
    adc.b $1C                              ; C09EE4 m0x0
    tay                                    ; C09EE6 m0x0
    lda.w nmi_handler_ptr,y                ; C09EE7 m0x0
    sta.b nmi_handler_ptr,x                ; C09EEA m0x0
    lda.w dma_pending_mask,y               ; C09EEC m0x0
    sta.b dma_pending_mask,x               ; C09EEF m0x0
    lda.w ptr_04,y                         ; C09EF1 m0x0
    sta.b ptr_04,x                         ; C09EF4 m0x0
    lda.w $0006,y                          ; C09EF6 m0x0

loc_C09EF9:
    sta.b $06,x                            ; C09EF9 m0x0
    lda.b $18                              ; C09EFB m0x0
    clc                                    ; C09EFD m0x0
    adc.b $84                              ; C09EFE m0x0
    sta.b $18                              ; C09F00 m0x0
    txa                                    ; C09F02 m0x0
    clc                                    ; C09F03 m0x0
    adc.w #$0008                           ; C09F04 m0x0
    tax                                    ; C09F07 m0x0
    cmp.w #$0708                           ; C09F08 m0x0
    bne loc_C09ED1                         ; C09F0B m0x0
    plb                                    ; C09F0D m0x0
    jmp.w loc_C09F8F                       ; C09F0E m0x0

loc_C09F11:
    asl                                    ; C09F11 m0x0
    asl                                    ; C09F12 m0x0
    asl                                    ; C09F13 m0x0
    asl                                    ; C09F14 m0x0
    asl                                    ; C09F15 m0x0
    adc.b $1C                              ; C09F16 m0x0
    tay                                    ; C09F18 m0x0
    lda.w $0006,y                          ; C09F19 m0x0
    eor.w #$4000                           ; C09F1C m0x0
    sta.b nmi_handler_ptr,x                ; C09F1F m0x0
    lda.w ptr_04,y                         ; C09F21 m0x0
    eor.w #$4000                           ; C09F24 m0x0
    sta.b dma_pending_mask,x               ; C09F27 m0x0
    lda.w dma_pending_mask,y               ; C09F29 m0x0
    eor.w #$4000                           ; C09F2C m0x0
    sta.b ptr_04,x                         ; C09F2F m0x0
    lda.w nmi_handler_ptr,y                ; C09F31 m0x0
    eor.w #$4000                           ; C09F34 m0x0
    bra loc_C09EF9                         ; C09F37 m0x0

loc_C09F39:
    bit.w #$4000                           ; C09F39 m0x0
    bne loc_C09F66                         ; C09F3C m0x0
    asl                                    ; C09F3E m0x0
    asl                                    ; C09F3F m0x0
    asl                                    ; C09F40 m0x0
    asl                                    ; C09F41 m0x0
    asl                                    ; C09F42 m0x0
    adc.b $1E                              ; C09F43 m0x0
    tay                                    ; C09F45 m0x0
    lda.w nmi_handler_ptr,y                ; C09F46 m0x0
    eor.w #$8000                           ; C09F49 m0x0
    sta.b nmi_handler_ptr,x                ; C09F4C m0x0
    lda.w dma_pending_mask,y               ; C09F4E m0x0
    eor.w #$8000                           ; C09F51 m0x0
    sta.b dma_pending_mask,x               ; C09F54 m0x0
    lda.w ptr_04,y                         ; C09F56 m0x0
    eor.w #$8000                           ; C09F59 m0x0
    sta.b ptr_04,x                         ; C09F5C m0x0
    lda.w $0006,y                          ; C09F5E m0x0
    eor.w #$8000                           ; C09F61 m0x0
    bra loc_C09EF9                         ; C09F64 m0x0

loc_C09F66:
    asl                                    ; C09F66 m0x0
    asl                                    ; C09F67 m0x0
    asl                                    ; C09F68 m0x0
    asl                                    ; C09F69 m0x0
    asl                                    ; C09F6A m0x0
    adc.b $1E                              ; C09F6B m0x0
    tay                                    ; C09F6D m0x0
    lda.w $0006,y                          ; C09F6E m0x0
    eor.w #$C000                           ; C09F71 m0x0
    sta.b nmi_handler_ptr,x                ; C09F74 m0x0
    lda.w ptr_04,y                         ; C09F76 m0x0
    eor.w #$C000                           ; C09F79 m0x0
    sta.b dma_pending_mask,x               ; C09F7C m0x0
    lda.w dma_pending_mask,y               ; C09F7E m0x0
    eor.w #$C000                           ; C09F81 m0x0
    sta.b ptr_04,x                         ; C09F84 m0x0
    lda.w nmi_handler_ptr,y                ; C09F86 m0x0
    eor.w #$C000                           ; C09F89 m0x0
    jmp.w loc_C09EF9                       ; C09F8C m0x0

loc_C09F8F:
    lda.b camera_x                         ; C09F8F m0x0
    and.w #$01F8                           ; C09F91 m0x0
    lsr                                    ; C09F94 m0x0
    lsr                                    ; C09F95 m0x0
    tay                                    ; C09F96 m0x0
    and.w #$0006                           ; C09F97 m0x0
    tax                                    ; C09F9A m0x0
    clc                                    ; C09F9B m0x0
    adc.w #$0042                           ; C09F9C m0x0
    sta.b $1C                              ; C09F9F m0x0

loc_C09FA1:
    lda.w $06C0,x                          ; C09FA1 m0x0
    sta.w $0500,y                          ; C09FA4 m0x0
    tya                                    ; C09FA7 m0x0
    clc                                    ; C09FA8 m0x0
    adc.w #$0002                           ; C09FA9 m0x0
    and.w #$007E                           ; C09FAC m0x0
    tay                                    ; C09FAF m0x0
    inx                                    ; C09FB0 m0x0
    inx                                    ; C09FB1 m0x0
    cpx.b $1C                              ; C09FB2 m0x0
    bne loc_C09FA1                         ; C09FB4 m0x0
    rts                                    ; C09FB6 m0x0

build_metatile_column_580:
    lda.b layer_parallax_mode              ; C09FB7 m0x0
    bpl loc_C09FBF                         ; C09FB9 m0x0
    lda.b camera_x                         ; C09FBB m0x0
    bra loc_C09FC5                         ; C09FBD m0x0

loc_C09FBF:
    lda.b camera_x                         ; C09FBF m0x0
    clc                                    ; C09FC1 m0x0
    adc.w #$0100                           ; C09FC2 m0x0

loc_C09FC5:
    tay                                    ; C09FC5 m0x0
    and.w #$FFE0                           ; C09FC6 m0x0
    ldx.b $82                              ; C09FC9 m0x0
    bmi loc_C09FD2                         ; C09FCB m0x0
    beq loc_C09FD8                         ; C09FCD m0x0
    lsr                                    ; C09FCF m0x0
    bra loc_C09FD8                         ; C09FD0 m0x0

loc_C09FD2:
    sta.b ptr_04                           ; C09FD2 m0x0
    lsr                                    ; C09FD4 m0x0
    clc                                    ; C09FD5 m0x0
    adc.b ptr_04                           ; C09FD6 m0x0

loc_C09FD8:
    clc                                    ; C09FD8 m0x0
    adc.b tilemap_a_addr                   ; C09FD9 m0x0
    sta.b $18                              ; C09FDB m0x0
    lda.b tilemap_a_bank                   ; C09FDD m0x0
    sta.b $1A                              ; C09FDF m0x0
    lda.b camera_y                         ; C09FE1 m0x0
    and.w #$FFE0                           ; C09FE3 m0x0
    lsr                                    ; C09FE6 m0x0
    lsr                                    ; C09FE7 m0x0
    lsr                                    ; C09FE8 m0x0
    lsr                                    ; C09FE9 m0x0
    clc                                    ; C09FEA m0x0
    adc.b $18                              ; C09FEB m0x0
    sta.b $18                              ; C09FED m0x0
    tya                                    ; C09FEF m0x0
    and.w #$0018                           ; C09FF0 m0x0
    lsr                                    ; C09FF3 m0x0
    lsr                                    ; C09FF4 m0x0
    adc.b tilemap_b_addr                   ; C09FF5 m0x0
    sta.b $1C                              ; C09FF7 m0x0
    tya                                    ; C09FF9 m0x0
    and.w #$0018                           ; C09FFA m0x0
    eor.w #$0018                           ; C09FFD m0x0
    lsr                                    ; C0A000 m0x0
    lsr                                    ; C0A001 m0x0
    adc.b tilemap_b_addr                   ; C0A002 m0x0
    sta.b $1E                              ; C0A004 m0x0
    phk                                    ; C0A006 m0x0
    ldx.w #$06C0                           ; C0A007 m0x0

loc_C0A00A:
    lda.b metatile_data_bank               ; C0A00A m0x0
    pha                                    ; C0A00C m0x0
    plb                                    ; C0A00D m0x0
    plb                                    ; C0A00E m0x0
    lda.b [$18]                            ; C0A00F m0x0
    bmi loc_C0A073                         ; C0A011 m0x0
    bit.w #$4000                           ; C0A013 m0x0
    bne loc_C0A04B                         ; C0A016 m0x0
    asl                                    ; C0A018 m0x0
    asl                                    ; C0A019 m0x0
    asl                                    ; C0A01A m0x0
    asl                                    ; C0A01B m0x0
    asl                                    ; C0A01C m0x0
    adc.b $1C                              ; C0A01D m0x0
    tay                                    ; C0A01F m0x0
    lda.w nmi_handler_ptr,y                ; C0A020 m0x0
    sta.b nmi_handler_ptr,x                ; C0A023 m0x0
    lda.w $0008,y                          ; C0A025 m0x0
    sta.b dma_pending_mask,x               ; C0A028 m0x0
    lda.w $0010,y                          ; C0A02A m0x0
    sta.b ptr_04,x                         ; C0A02D m0x0
    lda.w $0018,y                          ; C0A02F m0x0

loc_C0A032:
    sta.b $06,x                            ; C0A032 m0x0
    lda.b $18                              ; C0A034 m0x0
    clc                                    ; C0A036 m0x0
    adc.w #$0002                           ; C0A037 m0x0
    sta.b $18                              ; C0A03A m0x0
    txa                                    ; C0A03C m0x0
    clc                                    ; C0A03D m0x0
    adc.w #$0008                           ; C0A03E m0x0
    tax                                    ; C0A041 m0x0
    cmp.w #$0708                           ; C0A042 m0x0
    bne loc_C0A00A                         ; C0A045 m0x0
    plb                                    ; C0A047 m0x0
    jmp.w loc_C0A0C9                       ; C0A048 m0x0

loc_C0A04B:
    asl                                    ; C0A04B m0x0
    asl                                    ; C0A04C m0x0
    asl                                    ; C0A04D m0x0
    asl                                    ; C0A04E m0x0
    asl                                    ; C0A04F m0x0
    adc.b $1E                              ; C0A050 m0x0
    tay                                    ; C0A052 m0x0
    lda.w nmi_handler_ptr,y                ; C0A053 m0x0
    eor.w #$4000                           ; C0A056 m0x0
    sta.b nmi_handler_ptr,x                ; C0A059 m0x0
    lda.w $0008,y                          ; C0A05B m0x0
    eor.w #$4000                           ; C0A05E m0x0
    sta.b dma_pending_mask,x               ; C0A061 m0x0
    lda.w $0010,y                          ; C0A063 m0x0
    eor.w #$4000                           ; C0A066 m0x0
    sta.b ptr_04,x                         ; C0A069 m0x0
    lda.w $0018,y                          ; C0A06B m0x0
    eor.w #$4000                           ; C0A06E m0x0
    bra loc_C0A032                         ; C0A071 m0x0

loc_C0A073:
    bit.w #$4000                           ; C0A073 m0x0
    bne loc_C0A0A0                         ; C0A076 m0x0
    asl                                    ; C0A078 m0x0
    asl                                    ; C0A079 m0x0
    asl                                    ; C0A07A m0x0
    asl                                    ; C0A07B m0x0
    asl                                    ; C0A07C m0x0
    adc.b $1C                              ; C0A07D m0x0
    tay                                    ; C0A07F m0x0
    lda.w $0018,y                          ; C0A080 m0x0
    eor.w #$8000                           ; C0A083 m0x0
    sta.b nmi_handler_ptr,x                ; C0A086 m0x0
    lda.w $0010,y                          ; C0A088 m0x0
    eor.w #$8000                           ; C0A08B m0x0
    sta.b dma_pending_mask,x               ; C0A08E m0x0
    lda.w $0008,y                          ; C0A090 m0x0
    eor.w #$8000                           ; C0A093 m0x0
    sta.b ptr_04,x                         ; C0A096 m0x0
    lda.w nmi_handler_ptr,y                ; C0A098 m0x0
    eor.w #$8000                           ; C0A09B m0x0
    bra loc_C0A032                         ; C0A09E m0x0

loc_C0A0A0:
    asl                                    ; C0A0A0 m0x0
    asl                                    ; C0A0A1 m0x0
    asl                                    ; C0A0A2 m0x0
    asl                                    ; C0A0A3 m0x0
    asl                                    ; C0A0A4 m0x0
    adc.b $1E                              ; C0A0A5 m0x0
    tay                                    ; C0A0A7 m0x0
    lda.w $0018,y                          ; C0A0A8 m0x0
    eor.w #$C000                           ; C0A0AB m0x0
    sta.b nmi_handler_ptr,x                ; C0A0AE m0x0
    lda.w $0010,y                          ; C0A0B0 m0x0
    eor.w #$C000                           ; C0A0B3 m0x0
    sta.b dma_pending_mask,x               ; C0A0B6 m0x0
    lda.w $0008,y                          ; C0A0B8 m0x0
    eor.w #$C000                           ; C0A0BB m0x0
    sta.b ptr_04,x                         ; C0A0BE m0x0
    lda.w nmi_handler_ptr,y                ; C0A0C0 m0x0
    eor.w #$C000                           ; C0A0C3 m0x0
    jmp.w loc_C0A032                       ; C0A0C6 m0x0

loc_C0A0C9:
    lda.b camera_y                         ; C0A0C9 m0x0
    and.w #$00F8                           ; C0A0CB m0x0
    lsr                                    ; C0A0CE m0x0
    lsr                                    ; C0A0CF m0x0
    tay                                    ; C0A0D0 m0x0
    and.w #$0006                           ; C0A0D1 m0x0
    tax                                    ; C0A0D4 m0x0
    clc                                    ; C0A0D5 m0x0
    adc.w #$0040                           ; C0A0D6 m0x0
    sta.b $1C                              ; C0A0D9 m0x0

loc_C0A0DB:
    lda.w $06C0,x                          ; C0A0DB m0x0
    sta.w $0580,y                          ; C0A0DE m0x0
    tya                                    ; C0A0E1 m0x0
    clc                                    ; C0A0E2 m0x0
    adc.w #$0002                           ; C0A0E3 m0x0
    and.w #$003E                           ; C0A0E6 m0x0
    tay                                    ; C0A0E9 m0x0
    inx                                    ; C0A0EA m0x0
    inx                                    ; C0A0EB m0x0
    cpx.b $1C                              ; C0A0EC m0x0
    bne loc_C0A0DB                         ; C0A0EE m0x0
    rts                                    ; C0A0F0 m0x0

vram_upload_column_580:
    sep.b #$20                             ; C0A0F1 m0x0
    lda.b #$81                             ; C0A0F3 m1x0
    sta.w VMAIN                            ; C0A0F5 m1x0
    rep.b #$20                             ; C0A0F8 m1x0
    lda.b layer_parallax_mode              ; C0A0FA m0x0
    bpl loc_C0A102                         ; C0A0FC m0x0
    lda.b camera_x                         ; C0A0FE m0x0
    bra loc_C0A108                         ; C0A100 m0x0

loc_C0A102:
    lda.b camera_x                         ; C0A102 m0x0
    clc                                    ; C0A104 m0x0
    adc.w #$0100                           ; C0A105 m0x0

loc_C0A108:
    lsr                                    ; C0A108 m0x0
    lsr                                    ; C0A109 m0x0
    lsr                                    ; C0A10A m0x0
    and.w #$003F                           ; C0A10B m0x0
    bit.w #$0020                           ; C0A10E m0x0
    clc                                    ; C0A111 m0x0
    beq loc_C0A117                         ; C0A112 m0x0
    adc.w #$03E0                           ; C0A114 m0x0

loc_C0A117:
    adc.w #$7800                           ; C0A117 m0x0
    sta.w VMADDL                           ; C0A11A m0x0
    lda.w #$0580                           ; C0A11D m0x0
    sta.w A1TL0                            ; C0A120 m0x0
    sta.w A2AL0                            ; C0A123 m0x0
    lda.w #$0040                           ; C0A126 m0x0
    sta.w DASL0                            ; C0A129 m0x0
    lda.w #$1801                           ; C0A12C m0x0
    sta.w DMAP0                            ; C0A12F m0x0
    sep.b #$20                             ; C0A132 m0x0
    stz.w A1B0                             ; C0A134 m1x0
    lda.b #$01                             ; C0A137 m1x0
    sta.w MDMAEN                           ; C0A139 m1x0
    rep.b #$20                             ; C0A13C m1x0
    sep.b #$20                             ; C0A13E m0x0
    lda.b #$80                             ; C0A140 m1x0
    sta.w VMAIN                            ; C0A142 m1x0
    rep.b #$20                             ; C0A145 m1x0
    rts                                    ; C0A147 m0x0

vram_upload_column_500:
    lda.b camera_y_lookahead               ; C0A148 m0x0
    bpl loc_C0A150                         ; C0A14A m0x0
    lda.b camera_y                         ; C0A14C m0x0
    bra loc_C0A156                         ; C0A14E m0x0

loc_C0A150:
    lda.b camera_y                         ; C0A150 m0x0
    clc                                    ; C0A152 m0x0
    adc.w #$00E0                           ; C0A153 m0x0

loc_C0A156:
    asl                                    ; C0A156 m0x0
    asl                                    ; C0A157 m0x0
    and.w #$03E0                           ; C0A158 m0x0
    clc                                    ; C0A15B m0x0
    adc.w #$7800                           ; C0A15C m0x0
    sta.b $18                              ; C0A15F m0x0
    sta.w VMADDL                           ; C0A161 m0x0
    lda.w #$0500                           ; C0A164 m0x0
    sta.w A1TL0                            ; C0A167 m0x0
    sta.w A2AL0                            ; C0A16A m0x0
    lda.w #$0040                           ; C0A16D m0x0
    sta.w DASL0                            ; C0A170 m0x0
    lda.w #$1801                           ; C0A173 m0x0
    sta.w DMAP0                            ; C0A176 m0x0
    sep.b #$20                             ; C0A179 m0x0
    stz.w A1B0                             ; C0A17B m1x0
    lda.b #$01                             ; C0A17E m1x0
    sta.w MDMAEN                           ; C0A180 m1x0
    rep.b #$20                             ; C0A183 m1x0
    lda.b $18                              ; C0A185 m0x0
    clc                                    ; C0A187 m0x0
    adc.w #$0400                           ; C0A188 m0x0
    sta.w VMADDL                           ; C0A18B m0x0
    lda.w #$0540                           ; C0A18E m0x0
    sta.w A1TL0                            ; C0A191 m0x0
    sta.w A2AL0                            ; C0A194 m0x0
    lda.w #$0040                           ; C0A197 m0x0
    sta.w DASL0                            ; C0A19A m0x0
    lda.w #$1801                           ; C0A19D m0x0
    sta.w DMAP0                            ; C0A1A0 m0x0
    sep.b #$20                             ; C0A1A3 m0x0
    stz.w A1B0                             ; C0A1A5 m1x0
    lda.b #$01                             ; C0A1A8 m1x0
    sta.w MDMAEN                           ; C0A1AA m1x0
    rep.b #$20                             ; C0A1AD m1x0
    rts                                    ; C0A1AF m0x0

camera_follow_player:
    lda.w entity_x                         ; C0A1B0 m0x0
    sec                                    ; C0A1B3 m0x0
    sbc.w #$0080                           ; C0A1B4 m0x0
    bpl loc_C0A1BC                         ; C0A1B7 m0x0
    lda.w #$0000                           ; C0A1B9 m0x0

loc_C0A1BC:
    cmp.b level_width_mask                 ; C0A1BC m0x0
    bcc loc_C0A1C2                         ; C0A1BE m0x0
    lda.b level_width_mask                 ; C0A1C0 m0x0

loc_C0A1C2:
    sec                                    ; C0A1C2 m0x0
    sbc.b camera_x                         ; C0A1C3 m0x0
    sta.b layer_parallax_mode              ; C0A1C5 m0x0
    clc                                    ; C0A1C7 m0x0
    adc.b camera_x                         ; C0A1C8 m0x0
    sta.b camera_x                         ; C0A1CA m0x0
    lda.b game_mode                        ; C0A1CC m0x0
    cmp.w #$0001                           ; C0A1CE m0x0
    beq loc_C0A1F2                         ; C0A1D1 m0x0
    lda.w entity_y                         ; C0A1D3 m0x0
    sec                                    ; C0A1D6 m0x0
    sbc.w #$0020                           ; C0A1D7 m0x0
    bpl loc_C0A1DF                         ; C0A1DA m0x0
    lda.w #$0000                           ; C0A1DC m0x0

loc_C0A1DF:
    cmp.b level_height_mask                ; C0A1DF m0x0
    bcc loc_C0A1E5                         ; C0A1E1 m0x0
    lda.b level_height_mask                ; C0A1E3 m0x0

loc_C0A1E5:
    sec                                    ; C0A1E5 m0x0
    sbc.b camera_y                         ; C0A1E6 m0x0
    clc                                    ; C0A1E8 m0x0
    adc.b $74                              ; C0A1E9 m0x0
    sta.b camera_y_lookahead               ; C0A1EB m0x0
    clc                                    ; C0A1ED m0x0
    adc.b camera_y                         ; C0A1EE m0x0
    sta.b camera_y                         ; C0A1F0 m0x0

loc_C0A1F2:
    rts                                    ; C0A1F2 m0x0

entity_vel_y_from_vel_x:
    lda.w entity_vel_x,x                   ; C0A1F3 m0x0
    eor.w #$FFFF                           ; C0A1F6 m0x0
    inc                                    ; C0A1F9 m0x0
    cmp.w #$8000                           ; C0A1FA m0x0
    ror                                    ; C0A1FD m0x0
    cmp.w #$8000                           ; C0A1FE m0x0
    ror                                    ; C0A201 m0x0
    ldy.w $0BAE                            ; C0A202 m0x0
    beq loc_C0A20E                         ; C0A205 m0x0
    bmi loc_C0A20A                         ; C0A207 m0x0
    tdc                                    ; C0A209 m0x0

loc_C0A20A:
    eor.w #$FFFF                           ; C0A20A m0x0
    inc                                    ; C0A20D m0x0

loc_C0A20E:
    sta.w entity_vel_y,x                   ; C0A20E m0x0
    rts                                    ; C0A211 m0x0

random_next:
    sep.b #$20                             ; C0A212 m0x0
    lda.b $9D                              ; C0A214 m1x0
    pha                                    ; C0A216 m1x0
    asl                                    ; C0A217 m1x0
    lda.b init_magic_FFFF                  ; C0A218 m1x0
    rol.b init_magic_FFFF                  ; C0A21A m1x0
    rol.b init_magic_FFFF                  ; C0A21C m1x0
    eor.b $9F                              ; C0A21E m1x0
    sta.b $9D                              ; C0A220 m1x0
    pla                                    ; C0A222 m1x0
    sta.b $9F                              ; C0A223 m1x0
    eor.b init_magic_FFFF                  ; C0A225 m1x0
    pha                                    ; C0A227 m1x0
    lda.b init_magic_AA55                  ; C0A228 m1x0
    sta.b init_magic_FFFF                  ; C0A22A m1x0
    pla                                    ; C0A22C m1x0
    sta.b init_magic_AA55                  ; C0A22D m1x0
    rep.b #$20                             ; C0A22F m1x0
    rts                                    ; C0A231 m0x0

entity_accelerate_velocity_x:
    lda.w entity_vel_x_target,x            ; C0A232 m0x0
    bne loc_C0A247                         ; C0A235 m0x0
    lda.w entity_vel_x,x                   ; C0A237 m0x0
    clc                                    ; C0A23A m0x0
    adc.w #$0100                           ; C0A23B m0x0
    cmp.w #$0200                           ; C0A23E m0x0
    bcs loc_C0A247                         ; C0A241 m0x0
    stz.w entity_vel_x,x                   ; C0A243 m0x0
    rts                                    ; C0A246 m0x0

loc_C0A247:
    lda.w entity_vel_x_target,x            ; C0A247 m0x0
    sec                                    ; C0A24A m0x0
    sbc.w entity_vel_x,x                   ; C0A24B m0x0
    beq loc_C0A263                         ; C0A24E m0x0
    bmi loc_C0A264                         ; C0A250 m0x0
    lsr                                    ; C0A252 m0x0
    lsr                                    ; C0A253 m0x0
    lsr                                    ; C0A254 m0x0

loc_C0A255:
    bne loc_C0A25C                         ; C0A255 m0x0
    lda.w entity_vel_x_target,x            ; C0A257 m0x0
    bra loc_C0A260                         ; C0A25A m0x0

loc_C0A25C:
    clc                                    ; C0A25C m0x0
    adc.w entity_vel_x,x                   ; C0A25D m0x0

loc_C0A260:
    sta.w entity_vel_x,x                   ; C0A260 m0x0

loc_C0A263:
    rts                                    ; C0A263 m0x0

loc_C0A264:
    sec                                    ; C0A264 m0x0
    ror                                    ; C0A265 m0x0
    sec                                    ; C0A266 m0x0
    ror                                    ; C0A267 m0x0
    sec                                    ; C0A268 m0x0
    ror                                    ; C0A269 m0x0
    cmp.w #$FFFF                           ; C0A26A m0x0
    bra loc_C0A255                         ; C0A26D m0x0

entity_apply_velocity_x:
    ldy.w #$0000                           ; C0A26F m0x0
    lda.w $0867,x                          ; C0A272 m0x0
    and.w #$FF00                           ; C0A275 m0x0
    clc                                    ; C0A278 m0x0
    adc.w entity_x_sub,x                   ; C0A279 m0x0
    sta.w entity_x_sub,x                   ; C0A27C m0x0
    lda.w $0869,x                          ; C0A27F m0x0
    and.w #$00FF                           ; C0A282 m0x0
    bit.w #$0080                           ; C0A285 m0x0
    beq loc_C0A28D                         ; C0A288 m0x0
    ora.w #$FF00                           ; C0A28A m0x0

loc_C0A28D:
    adc.w entity_x,x                       ; C0A28D m0x0
    sta.w entity_x,x                       ; C0A290 m0x0
    rts                                    ; C0A293 m0x0

unused_entity_apply_velocity_z:
    ldy.w #$0000                           ; C0A294 m0x0
    lda.w $0927,x                          ; C0A297 m0x0
    and.w #$FF00                           ; C0A29A m0x0
    clc                                    ; C0A29D m0x0
    adc.w $0908,x                          ; C0A29E m0x0
    sta.w $0908,x                          ; C0A2A1 m0x0
    lda.w $0929,x                          ; C0A2A4 m0x0
    and.w #$00FF                           ; C0A2A7 m0x0
    bit.w #$0080                           ; C0A2AA m0x0
    beq loc_C0A2B2                         ; C0A2AD m0x0
    ora.w #$FF00                           ; C0A2AF m0x0

loc_C0A2B2:
    adc.w $08E8,x                          ; C0A2B2 m0x0
    sta.w $08E8,x                          ; C0A2B5 m0x0
    rts                                    ; C0A2B8 m0x0

entity_apply_velocity_y:
    ldy.w #$0000                           ; C0A2B9 m0x0
    lda.w $0947,x                          ; C0A2BC m0x0
    and.w #$FF00                           ; C0A2BF m0x0
    clc                                    ; C0A2C2 m0x0
    adc.w entity_y_sub,x                   ; C0A2C3 m0x0
    sta.w entity_y_sub,x                   ; C0A2C6 m0x0
    lda.w $0949,x                          ; C0A2C9 m0x0
    and.w #$00FF                           ; C0A2CC m0x0
    bit.w #$0080                           ; C0A2CF m0x0
    beq loc_C0A2D7                         ; C0A2D2 m0x0
    ora.w #$FF00                           ; C0A2D4 m0x0

loc_C0A2D7:
    adc.w entity_y,x                       ; C0A2D7 m0x0
    sta.w entity_y,x                       ; C0A2DA m0x0
    rts                                    ; C0A2DD m0x0

read_joypads:
    sep.b #$20                             ; C0A2DE m0x0
    lda.b #$01                             ; C0A2E0 m1x0

loc_C0A2E2:
    bit.w HVBJOY                           ; C0A2E2 m1x0
    bne loc_C0A2E2                         ; C0A2E5 m1x0
    rep.b #$20                             ; C0A2E7 m1x0
    lda.w JOY1L                            ; C0A2E9 m0x0
    eor.b $8A                              ; C0A2EC m0x0
    and.w JOY1L                            ; C0A2EE m0x0
    sta.b $8C                              ; C0A2F1 m0x0
    lda.w JOY1L                            ; C0A2F3 m0x0
    sta.b $8A                              ; C0A2F6 m0x0
    lda.w JOY2L                            ; C0A2F8 m0x0
    eor.b $8E                              ; C0A2FB m0x0
    and.w JOY2L                            ; C0A2FD m0x0
    sta.b $90                              ; C0A300 m0x0
    lda.w JOY2L                            ; C0A302 m0x0
    sta.b $8E                              ; C0A305 m0x0
    lda.b $8A                              ; C0A307 m0x0
    and.w #$0007                           ; C0A309 m0x0
    beq loc_C0A321                         ; C0A30C m0x0
    sep.b #$20                             ; C0A30E m0x0
    ldy.w #$0010                           ; C0A310 m1x0

loc_C0A313:
    lda.w JOYSER0                          ; C0A313 m1x0
    dey                                    ; C0A316 m1x0
    bne loc_C0A313                         ; C0A317 m1x0
    rep.b #$20                             ; C0A319 m1x0
    stz.b $8A                              ; C0A31B m0x0
    stz.b $8C                              ; C0A31D m0x0
    bra loc_C0A331                         ; C0A31F m0x0

loc_C0A321:
    sep.b #$20                             ; C0A321 m0x0
    lda.w JOYSER0                          ; C0A323 m1x0
    rep.b #$20                             ; C0A326 m1x0
    bit.w #$0001                           ; C0A328 m0x0
    bne loc_C0A331                         ; C0A32B m0x0
    stz.b $8A                              ; C0A32D m0x0
    stz.b $8C                              ; C0A32F m0x0

loc_C0A331:
    lda.b $8E                              ; C0A331 m0x0
    and.w #$0007                           ; C0A333 m0x0
    beq loc_C0A34B                         ; C0A336 m0x0
    sep.b #$20                             ; C0A338 m0x0
    ldy.w #$0010                           ; C0A33A m1x0

loc_C0A33D:
    lda.w JOYSER1                          ; C0A33D m1x0
    dey                                    ; C0A340 m1x0
    bne loc_C0A33D                         ; C0A341 m1x0
    rep.b #$20                             ; C0A343 m1x0
    stz.b $8E                              ; C0A345 m0x0
    stz.b $90                              ; C0A347 m0x0
    bra loc_C0A35B                         ; C0A349 m0x0

loc_C0A34B:
    sep.b #$20                             ; C0A34B m0x0
    lda.w JOYSER1                          ; C0A34D m1x0
    rep.b #$20                             ; C0A350 m1x0
    bit.w #$0001                           ; C0A352 m0x0
    bne loc_C0A35B                         ; C0A355 m0x0
    stz.b $8E                              ; C0A357 m0x0
    stz.b $90                              ; C0A359 m0x0

loc_C0A35B:
    rts                                    ; C0A35B m0x0

unused_wram_clear_full:
    ldx.w #$00FE                           ; C0A35C m0x0
    lda.w #$0000                           ; C0A35F m0x0
    tcd                                    ; C0A362 m0x0

loc_C0A363:
    sta.b nmi_handler_ptr,x                ; C0A363 m0x0
    dex                                    ; C0A365 m0x0
    dex                                    ; C0A366 m0x0
    bpl loc_C0A363                         ; C0A367 m0x0
    ldx.w #$0200                           ; C0A369 m0x0

loc_C0A36C:
    stz.b nmi_handler_ptr,x                ; C0A36C m0x0
    inx                                    ; C0A36E m0x0
    inx                                    ; C0A36F m0x0
    cpx.w #$2000                           ; C0A370 m0x0
    bne loc_C0A36C                         ; C0A373 m0x0
    lda.w #$0000                           ; C0A375 m0x0
    tax                                    ; C0A378 m0x0

loc_C0A379:
    sta.l $7E2000,x                        ; C0A379 m0x0
    inx                                    ; C0A37D m0x0
    inx                                    ; C0A37E m0x0
    cpx.w #$E000                           ; C0A37F m0x0
    bpl loc_C0A379                         ; C0A382 m0x0
    rts                                    ; C0A384 m0x0

ppu_init:
    rep.b #$30                             ; C0A385 m0x0
    lda.w #$0000                           ; C0A387 m0x0
    ldx.w #$0000                           ; C0A38A m0x0
    ldy.w #$0000                           ; C0A38D m0x0
    stz.w OBSEL                            ; C0A390 m0x0
    stz.w BGMODE                           ; C0A393 m0x0
    stz.w BG1SC                            ; C0A396 m0x0
    stz.w BG3SC                            ; C0A399 m0x0
    stz.w BG12NBA                          ; C0A39C m0x0
    stz.w BG1HOFS                          ; C0A39F m0x0
    stz.w BG1HOFS                          ; C0A3A2 m0x0
    stz.w BG2HOFS                          ; C0A3A5 m0x0
    stz.w BG2HOFS                          ; C0A3A8 m0x0
    stz.w BG3HOFS                          ; C0A3AB m0x0
    stz.w BG3HOFS                          ; C0A3AE m0x0
    stz.w BG4HOFS                          ; C0A3B1 m0x0
    stz.w BG4HOFS                          ; C0A3B4 m0x0
    stz.w VMADDL                           ; C0A3B7 m0x0
    stz.w W12SEL                           ; C0A3BA m0x0
    stz.w WOBJSEL                          ; C0A3BD m0x0
    stz.w WH1                              ; C0A3C0 m0x0
    stz.w WH3                              ; C0A3C3 m0x0
    stz.w WOBJLOG                          ; C0A3C6 m0x0
    stz.w TS                               ; C0A3C9 m0x0
    stz.w WRMPYA                           ; C0A3CC m0x0
    stz.w WRDIVL                           ; C0A3CF m0x0
    stz.w WRDIVB                           ; C0A3D2 m0x0
    stz.w HTIMEH                           ; C0A3D5 m0x0
    stz.w VTIMEH                           ; C0A3D8 m0x0
    sep.b #$30                             ; C0A3DB m0x0
    lda.b #$8F                             ; C0A3DD m1x1
    sta.w INIDISP                          ; C0A3DF m1x1
    stz.w OAMADDH                          ; C0A3E2 m1x1
    lda.b #$80                             ; C0A3E5 m1x1
    sta.w VMAIN                            ; C0A3E7 m1x1
    stz.w M7SEL                            ; C0A3EA m1x1
    stz.w M7A                              ; C0A3ED m1x1
    lda.b #$01                             ; C0A3F0 m1x1
    sta.w M7A                              ; C0A3F2 m1x1
    stz.w M7B                              ; C0A3F5 m1x1
    stz.w M7B                              ; C0A3F8 m1x1
    stz.w M7C                              ; C0A3FB m1x1
    stz.w M7C                              ; C0A3FE m1x1
    stz.w M7D                              ; C0A401 m1x1
    sta.w M7D                              ; C0A404 m1x1
    stz.w M7X                              ; C0A407 m1x1
    stz.w M7X                              ; C0A40A m1x1
    stz.w M7Y                              ; C0A40D m1x1
    stz.w M7Y                              ; C0A410 m1x1
    stz.w CGADD                            ; C0A413 m1x1
    stz.w TSW                              ; C0A416 m1x1
    lda.b #$30                             ; C0A419 m1x1
    sta.w CGWSEL                           ; C0A41B m1x1
    stz.w CGADSUB                          ; C0A41E m1x1
    lda.b #$E0                             ; C0A421 m1x1
    sta.w COLDATA                          ; C0A423 m1x1
    stz.w SETINI                           ; C0A426 m1x1
    stz.w NMITIMEN                         ; C0A429 m1x1
    lda.b #$FF                             ; C0A42C m1x1
    sta.w WRIO                             ; C0A42E m1x1
    stz.w HDMAEN                           ; C0A431 m1x1
    lda.b #$01                             ; C0A434 m1x1
    sta.w MEMSEL                           ; C0A436 m1x1
    lda.w RDNMI                            ; C0A439 m1x1
    lda.w TIMEUP                           ; C0A43C m1x1
    rep.b #$30                             ; C0A43F m1x1
    rts                                    ; C0A441 m0x0

unused_vec:
    rti                                    ; C0A442 m1x1

dma_fill_zero_word:
    incbin "../data/01.bin":$2443..$2445      ; 2 bytes

dma_fill_vram_zero:
    sta.w VMADDL                           ; C0A445 m0x0
    lda.w #$A443                           ; C0A448 m0x0
    sta.w A1TL0                            ; C0A44B m0x0
    sta.w A2AL0                            ; C0A44E m0x0
    lda.w #$0800                           ; C0A451 m0x0
    sta.w DASL0                            ; C0A454 m0x0
    lda.w #$1809                           ; C0A457 m0x0
    sta.w DMAP0                            ; C0A45A m0x0
    sep.b #$20                             ; C0A45D m0x0
    stz.w A1B0                             ; C0A45F m1x0
    lda.b #$01                             ; C0A462 m1x0
    sta.w MDMAEN                           ; C0A464 m1x0
    rep.b #$20                             ; C0A467 m1x0
    rts                                    ; C0A469 m0x0

dma_upload_to_vram:
    sta.w A1TL0                            ; C0A46A m0x0
    sty.w DASL0                            ; C0A46D m0x0
    lda.w #$1801                           ; C0A470 m0x0
    sta.w DMAP0                            ; C0A473 m0x0
    sep.b #$30                             ; C0A476 m0x0
    stx.w A1B0                             ; C0A478 m1x1
    lda.b #$01                             ; C0A47B m1x1
    sta.w MDMAEN                           ; C0A47D m1x1
    rep.b #$30                             ; C0A480 m1x1
    rts                                    ; C0A482 m0x0

dma_upload_to_cgram:
    sta.w A1TL0                            ; C0A483 m0x0
    txa                                    ; C0A486 m0x0
    asl                                    ; C0A487 m0x0
    asl                                    ; C0A488 m0x0
    asl                                    ; C0A489 m0x0
    sta.w DASL0                            ; C0A48A m0x0
    lda.w #$2200                           ; C0A48D m0x0
    sta.w DMAP0                            ; C0A490 m0x0
    sep.b #$20                             ; C0A493 m0x0
    lda.b #$C4                             ; C0A495 m1x0
    sta.w A1B0                             ; C0A497 m1x0
    tya                                    ; C0A49A m1x0
    sta.w CGADD                            ; C0A49B m1x0
    lda.b #$01                             ; C0A49E m1x0
    sta.w MDMAEN                           ; C0A4A0 m1x0
    rep.b #$20                             ; C0A4A3 m1x0
    rts                                    ; C0A4A5 m0x0

set_bg_scroll_prep:
    sep.b #$20                             ; C0A4A6 m0x0

set_bg_scroll:
    stz.w BG1HOFS                          ; C0A4A8 m1x0
    stz.w BG1HOFS                          ; C0A4AB m1x0
    stz.w BG2HOFS                          ; C0A4AE m1x0
    stz.w BG2HOFS                          ; C0A4B1 m1x0
    stz.w BG3HOFS                          ; C0A4B4 m1x0
    stz.w BG3HOFS                          ; C0A4B7 m1x0
    lda.b #$FF                             ; C0A4BA m1x0
    sta.w BG1VOFS                          ; C0A4BC m1x0
    sta.w BG1VOFS                          ; C0A4BF m1x0
    sta.w BG2VOFS                          ; C0A4C2 m1x0
    sta.w BG2VOFS                          ; C0A4C5 m1x0
    sta.w BG3VOFS                          ; C0A4C8 m1x0
    sta.w BG3VOFS                          ; C0A4CB m1x0
    rep.b #$20                             ; C0A4CE m1x0
    rts                                    ; C0A4D0 m0x0

nmi:
    jml.l $800000+(loc_C0A4D5&$FFFF)       ; C0A4D1 m1x1

loc_C0A4D5:
    rep.b #$30                             ; C0A4D5 m1x1
    pha                                    ; C0A4D7 m0x0
    phx                                    ; C0A4D8 m0x0
    phy                                    ; C0A4D9 m0x0
    sep.b #$20                             ; C0A4DA m0x0
    lda.w RDNMI                            ; C0A4DC m1x0
    lda.b #$8F                             ; C0A4DF m1x0
    sta.w INIDISP                          ; C0A4E1 m1x0
    rep.b #$20                             ; C0A4E4 m1x0
    jmp.w ($0000)                          ; C0A4E6 m0x0

loc_C0A4E9:
    sta.b nmi_handler_ptr                  ; C0A4E9 m0x1
    sep.b #$20                             ; C0A4EB m0x1
    lda.w RDNMI                            ; C0A4ED m1x1

loc_C0A4F0:
    lda.w RDNMI                            ; C0A4F0 m1x1
    bmi loc_C0A4F0                         ; C0A4F3 m1x1

loc_C0A4F5:
    lda.b nmitimen_shadow                  ; C0A4F5 m1x1
    sta.w NMITIMEN                         ; C0A4F7 m1x1
    stz.w JOYSER0                          ; C0A4FA m1x1

loc_C0A4FD:
    wai                                    ; C0A4FD m1x1
    bra loc_C0A4FD                         ; C0A4FE m1x1

clear_sprite_table:
    stz.w oam_buffer_upper                 ; C0A500 m0x0
    stz.w $0402                            ; C0A503 m0x0
    stz.w $0404                            ; C0A506 m0x0
    stz.w $0406                            ; C0A509 m0x0
    stz.w $0408                            ; C0A50C m0x0
    stz.w $040A                            ; C0A50F m0x0
    stz.w $040C                            ; C0A512 m0x0
    stz.w $040E                            ; C0A515 m0x0
    stz.w $0410                            ; C0A518 m0x0
    stz.w $0412                            ; C0A51B m0x0
    stz.w $0414                            ; C0A51E m0x0
    stz.w $0416                            ; C0A521 m0x0
    stz.w $0418                            ; C0A524 m0x0
    stz.w $041A                            ; C0A527 m0x0
    stz.w $041C                            ; C0A52A m0x0
    stz.w $041E                            ; C0A52D m0x0
    lda.w #$0200                           ; C0A530 m0x0
    sta.b oam_write_ptr                    ; C0A533 m0x0
    stz.b entity_render_index              ; C0A535 m0x0
    rts                                    ; C0A537 m0x0

entity_build_oam_frame:
    lda.w #$0400                           ; C0A538 m0x0
    sta.b oam_entry_ptr                    ; C0A53B m0x0

loc_C0A53D:
    lda.b oam_write_ptr                    ; C0A53D m0x0
    cmp.w #$0400                           ; C0A53F m0x0
    bne loc_C0A553                         ; C0A542 m0x0
    sep.b #$20                             ; C0A544 m0x0
    lda.b #$07                             ; C0A546 m1x0
    sta.w INIDISP                          ; C0A548 m1x0
    rep.b #$20                             ; C0A54B m1x0
    pea.w $8080                            ; C0A54D m0x0
    plb                                    ; C0A550 m0x0
    plb                                    ; C0A551 m0x0
    rtl                                    ; C0A552 m0x0

loc_C0A553:
    phk                                    ; C0A553 m0x0
    plb                                    ; C0A554 m0x0
    ldy.b entity_render_index              ; C0A555 m0x0
    lda.w entity_render_order,y            ; C0A557 m0x0
    tay                                    ; C0A55A m0x0
    lda.w $07A8,y                          ; C0A55B m0x0
    bne loc_C0A563                         ; C0A55E m0x0

loc_C0A560:
    jmp.w loc_C0A6BF                       ; C0A560 m0x0

loc_C0A563:
    ldx.w entity_frame_id,y                ; C0A563 m0x0
    txa                                    ; C0A566 m0x0
    sta.w $07E8,y                          ; C0A567 m0x0
    beq loc_C0A560                         ; C0A56A m0x0
    lda.l data_C40000,x                    ; C0A56C m0x0
    sta.b sprite_frame_ptr                 ; C0A570 m0x0
    inc                                    ; C0A572 m0x0
    sta.b sprite_frame_ptr2                ; C0A573 m0x0
    lda.l data_C40002,x                    ; C0A575 m0x0
    sta.b sprite_frame_bank                ; C0A579 m0x0
    sta.b sprite_frame_bank2               ; C0A57B m0x0
    xba                                    ; C0A57D m0x0
    and.w #$00FF                           ; C0A57E m0x0
    clc                                    ; C0A581 m0x0
    adc.w entity_y,y                       ; C0A582 m0x0
    sta.w entity_depth_key,y               ; C0A585 m0x0
    lda.w $08E8,y                          ; C0A588 m0x0
    clc                                    ; C0A58B m0x0
    adc.w entity_y,y                       ; C0A58C m0x0
    sec                                    ; C0A58F m0x0
    sbc.w #$0100                           ; C0A590 m0x0
    sec                                    ; C0A593 m0x0
    sbc.b camera_y                         ; C0A594 m0x0
    sec                                    ; C0A596 m0x0
    sbc.b $76                              ; C0A597 m0x0
    clc                                    ; C0A599 m0x0
    adc.b $92                              ; C0A59A m0x0
    sta.b entity_screen_y                  ; C0A59C m0x0
    adc.w #$0090                           ; C0A59E m0x0
    cmp.w #$0130                           ; C0A5A1 m0x0
    bcs loc_C0A560                         ; C0A5A4 m0x0
    lda.w entity_x,y                       ; C0A5A6 m0x0
    sbc.b camera_x                         ; C0A5A9 m0x0
    sta.b entity_screen_x                  ; C0A5AB m0x0
    clc                                    ; C0A5AD m0x0
    adc.w #$0030                           ; C0A5AE m0x0
    cmp.w #$0160                           ; C0A5B1 m0x0
    bcc loc_C0A5B9                         ; C0A5B4 m0x0
    jmp.w loc_C0A6BF                       ; C0A5B6 m0x0

loc_C0A5B9:
    lda.w entity_flags,y                   ; C0A5B9 m0x0
    sta.b $18                              ; C0A5BC m0x0
    sta.b $1A                              ; C0A5BE m0x0
    bit.w #$8000                           ; C0A5C0 m0x0
    bne loc_C0A60A                         ; C0A5C3 m0x0
    bit.w #$4000                           ; C0A5C5 m0x0
    bne loc_C0A5EA                         ; C0A5C8 m0x0
    lda.b entity_screen_x                  ; C0A5CA m0x0
    sec                                    ; C0A5CC m0x0
    sbc.w #$0080                           ; C0A5CD m0x0
    sta.b entity_screen_x                  ; C0A5D0 m0x0
    lda.b entity_screen_y                  ; C0A5D2 m0x0
    clc                                    ; C0A5D4 m0x0
    adc.w #$0010                           ; C0A5D5 m0x0
    sta.b entity_screen_y                  ; C0A5D8 m0x0
    cpx.w #$0004                           ; C0A5DA m0x0
    bcs loc_C0A5E5                         ; C0A5DD m0x0
    jsr.w oam_emit_frame_1row              ; C0A5DF m0x0
    jmp.w loc_C0A6BF                       ; C0A5E2 m0x0

loc_C0A5E5:
    jsr.w oam_emit_frame_2row              ; C0A5E5 m0x0
    bra loc_C0A62B                         ; C0A5E8 m0x0

loc_C0A5EA:
    lda.b entity_screen_x                  ; C0A5EA m0x0
    sec                                    ; C0A5EC m0x0
    sbc.w #$008F                           ; C0A5ED m0x0
    sta.b entity_screen_x                  ; C0A5F0 m0x0
    lda.b entity_screen_y                  ; C0A5F2 m0x0
    clc                                    ; C0A5F4 m0x0
    adc.w #$0010                           ; C0A5F5 m0x0
    sta.b entity_screen_y                  ; C0A5F8 m0x0
    cpx.w #$0004                           ; C0A5FA m0x0
    bcs loc_C0A605                         ; C0A5FD m0x0
    jsr.w oam_emit_frame_1row_flip         ; C0A5FF m0x0
    jmp.w loc_C0A6BF                       ; C0A602 m0x0

loc_C0A605:
    jsr.w oam_emit_frame_2row_flip         ; C0A605 m0x0
    bra loc_C0A62B                         ; C0A608 m0x0

loc_C0A60A:
    bit.w #$4000                           ; C0A60A m0x0
    bne loc_C0A61E                         ; C0A60D m0x0
    lda.b entity_screen_x                  ; C0A60F m0x0
    sec                                    ; C0A611 m0x0
    sbc.w #$0080                           ; C0A612 m0x0
    sta.b entity_screen_x                  ; C0A615 m0x0
    inc.b entity_screen_y                  ; C0A617 m0x0
    jsr.w oam_emit_frame_3row              ; C0A619 m0x0
    bra loc_C0A62B                         ; C0A61C m0x0

loc_C0A61E:
    lda.b entity_screen_x                  ; C0A61E m0x0
    sec                                    ; C0A620 m0x0
    sbc.w #$008F                           ; C0A621 m0x0
    sta.b entity_screen_x                  ; C0A624 m0x0
    inc.b entity_screen_y                  ; C0A626 m0x0
    jsr.w oam_emit_frame_3row_flip         ; C0A628 m0x0

loc_C0A62B:
    ldx.b entity_render_index              ; C0A62B m0x0
    lda.w entity_render_order,x            ; C0A62D m0x0
    tax                                    ; C0A630 m0x0
    lda.w entity_frame_id,x                ; C0A631 m0x0
    cmp.w $0808,x                          ; C0A634 m0x0
    bne loc_C0A63C                         ; C0A637 m0x0
    jmp.w loc_C0A6BF                       ; C0A639 m0x0

loc_C0A63C:
    sta.w $0808,x                          ; C0A63C m0x0
    ldx.w $0A88                            ; C0A63F m0x0
    tya                                    ; C0A642 m0x0
    clc                                    ; C0A643 m0x0
    adc.b sprite_frame_ptr                 ; C0A644 m0x0
    tay                                    ; C0A646 m0x0
    sta.w $0A8E,x                          ; C0A647 m0x0
    lda.b $21                              ; C0A64A m0x0
    and.w #$00FF                           ; C0A64C m0x0
    asl                                    ; C0A64F m0x0
    asl                                    ; C0A650 m0x0
    asl                                    ; C0A651 m0x0
    asl                                    ; C0A652 m0x0
    asl                                    ; C0A653 m0x0
    sta.w $0A8A,x                          ; C0A654 m0x0
    sta.b $52                              ; C0A657 m0x0
    tya                                    ; C0A659 m0x0
    clc                                    ; C0A65A m0x0
    adc.b $52                              ; C0A65B m0x0
    tay                                    ; C0A65D m0x0
    lda.b $18                              ; C0A65E m0x0
    and.w #$01FF                           ; C0A660 m0x0
    asl                                    ; C0A663 m0x0
    asl                                    ; C0A664 m0x0
    asl                                    ; C0A665 m0x0
    asl                                    ; C0A666 m0x0
    sta.w $0A8C,x                          ; C0A667 m0x0
    lda.b sprite_frame_bank                ; C0A66A m0x0
    ora.w #$FF00                           ; C0A66C m0x0
    sta.w $0A90,x                          ; C0A66F m0x0
    txa                                    ; C0A672 m0x0
    clc                                    ; C0A673 m0x0
    adc.w #$0008                           ; C0A674 m0x0
    tax                                    ; C0A677 m0x0
    stz.w $0A90,x                          ; C0A678 m0x0
    lda.b $23                              ; C0A67B m0x0
    and.w #$000F                           ; C0A67D m0x0
    cmp.w #$0000                           ; C0A680 m0x0
    beq loc_C0A6B9                         ; C0A683 m0x0
    asl                                    ; C0A685 m0x0
    asl                                    ; C0A686 m0x0
    asl                                    ; C0A687 m0x0
    asl                                    ; C0A688 m0x0
    asl                                    ; C0A689 m0x0
    sta.w $0A8A,x                          ; C0A68A m0x0
    lda.b $22                              ; C0A68D m0x0
    and.w #$00FF                           ; C0A68F m0x0
    asl                                    ; C0A692 m0x0
    asl                                    ; C0A693 m0x0
    asl                                    ; C0A694 m0x0
    asl                                    ; C0A695 m0x0
    sta.b $52                              ; C0A696 m0x0
    lda.b $18                              ; C0A698 m0x0
    and.w #$01FF                           ; C0A69A m0x0
    asl                                    ; C0A69D m0x0
    asl                                    ; C0A69E m0x0
    asl                                    ; C0A69F m0x0
    asl                                    ; C0A6A0 m0x0
    clc                                    ; C0A6A1 m0x0
    adc.b $52                              ; C0A6A2 m0x0
    sta.w $0A8C,x                          ; C0A6A4 m0x0
    tya                                    ; C0A6A7 m0x0
    sta.w $0A8E,x                          ; C0A6A8 m0x0
    lda.b sprite_frame_bank                ; C0A6AB m0x0
    ora.w #$FF00                           ; C0A6AD m0x0
    sta.w $0A90,x                          ; C0A6B0 m0x0
    txa                                    ; C0A6B3 m0x0
    clc                                    ; C0A6B4 m0x0
    adc.w #$0008                           ; C0A6B5 m0x0
    tax                                    ; C0A6B8 m0x0

loc_C0A6B9:
    stx.w $0A88                            ; C0A6B9 m0x0
    stz.w $0A90,x                          ; C0A6BC m0x0

loc_C0A6BF:
    inc.b entity_render_index              ; C0A6BF m0x0
    inc.b entity_render_index              ; C0A6C1 m0x0
    lda.b entity_render_index              ; C0A6C3 m0x0
    cmp.w #$0020                           ; C0A6C5 m0x0
    beq loc_C0A6CD                         ; C0A6C8 m0x0
    jmp.w loc_C0A53D                       ; C0A6CA m0x0

loc_C0A6CD:
    pea.w $8080                            ; C0A6CD m0x0
    plb                                    ; C0A6D0 m0x0
    plb                                    ; C0A6D1 m0x0
    rtl                                    ; C0A6D2 m0x0

oam_size_bit_mask_table:
    incbin "../data/01.bin":$26D3..$26D7      ; 4 bytes

oam_size_preset_table:
    incbin "../data/01.bin":$26D7..$2757      ; 128 bytes

oam_emit_frame_1row:
    ldy.w #$0000                           ; C0A757 m0x0
    lda.b [sprite_frame_ptr],y             ; C0A75A m0x0
    sta.b $1C                              ; C0A75C m0x0
    ldy.w #$0002                           ; C0A75E m0x0
    lda.b [sprite_frame_ptr],y             ; C0A761 m0x0
    sta.b $1E                              ; C0A763 m0x0
    ldy.w #$0004                           ; C0A765 m0x0
    lda.b [sprite_frame_ptr],y             ; C0A768 m0x0
    sta.b $20                              ; C0A76A m0x0
    ldy.w #$0005                           ; C0A76C m0x0
    jmp.w loc_C0A791                       ; C0A76F m0x0

oam_emit_frame_2row:
    ldy.w #$0000                           ; C0A772 m0x0
    lda.b [sprite_frame_ptr],y             ; C0A775 m0x0
    sta.b $1C                              ; C0A777 m0x0
    ldy.w #$0002                           ; C0A779 m0x0
    lda.b [sprite_frame_ptr],y             ; C0A77C m0x0
    sta.b $1E                              ; C0A77E m0x0
    ldy.w #$0004                           ; C0A780 m0x0
    lda.b [sprite_frame_ptr],y             ; C0A783 m0x0
    sta.b $20                              ; C0A785 m0x0
    ldy.w #$0006                           ; C0A787 m0x0
    lda.b [sprite_frame_ptr],y             ; C0A78A m0x0
    sta.b $22                              ; C0A78C m0x0
    ldy.w #$0008                           ; C0A78E m0x0

loc_C0A791:
    lda.b oam_write_ptr                    ; C0A791 m0x0
    lsr                                    ; C0A793 m0x0
    lsr                                    ; C0A794 m0x0
    sep.b #$20                             ; C0A795 m0x0
    tax                                    ; C0A797 m1x0
    adc.b $1C                              ; C0A798 m1x0
    bmi loc_C0A7A2                         ; C0A79A m1x0
    rep.b #$20                             ; C0A79C m1x0
    pla                                    ; C0A79E m0x0
    jmp.w loc_C0A6CD                       ; C0A79F m0x0

loc_C0A7A2:
    txa                                    ; C0A7A2 m1x0
    lsr                                    ; C0A7A3 m1x0
    lsr                                    ; C0A7A4 m1x0
    and.b #$1F                             ; C0A7A5 m1x0
    sta.b oam_entry_ptr                    ; C0A7A7 m1x0
    txa                                    ; C0A7A9 m1x0
    and.b #$03                             ; C0A7AA m1x0
    tax                                    ; C0A7AC m1x0
    lda.w oam_size_bit_mask_table,x        ; C0A7AD m1x0
    sta.b $24                              ; C0A7B0 m1x0
    ldx.b oam_write_ptr                    ; C0A7B2 m1x0
    clc                                    ; C0A7B4 m1x0

loc_C0A7B5:
    dec.b $1C                              ; C0A7B5 m1x0
    bmi loc_C0A813                         ; C0A7B7 m1x0
    lda.b [sprite_frame_ptr2],y            ; C0A7B9 m1x0
    rep.b #$20                             ; C0A7BB m1x0
    and.w #$00FF                           ; C0A7BD m0x0
    adc.b entity_screen_y                  ; C0A7C0 m0x0
    cmp.w #$00F0                           ; C0A7C2 m0x0
    bcs loc_C0A7FE                         ; C0A7C5 m0x0
    sbc.w #$000F                           ; C0A7C7 m0x0
    sta.b $01,x                            ; C0A7CA m0x0
    lda.b [sprite_frame_ptr],y             ; C0A7CC m0x0
    and.w #$00FF                           ; C0A7CE m0x0
    clc                                    ; C0A7D1 m0x0
    adc.b entity_screen_x                  ; C0A7D2 m0x0
    cmp.w #$0100                           ; C0A7D4 m0x0
    sep.b #$20                             ; C0A7D7 m0x0
    sta.b nmi_handler_ptr,x                ; C0A7D9 m1x0
    lda.b $24                              ; C0A7DB m1x0
    bcs loc_C0A7E1                         ; C0A7DD m1x0
    and.b #$AA                             ; C0A7DF m1x0

loc_C0A7E1:
    ora.b (oam_entry_ptr)                  ; C0A7E1 m1x0
    sta.b (oam_entry_ptr)                  ; C0A7E3 m1x0
    lda.b $24                              ; C0A7E5 m1x0
    bpl loc_C0A7F0                         ; C0A7E7 m1x0
    inc.b oam_entry_ptr                    ; C0A7E9 m1x0
    lda.b #$03                             ; C0A7EB m1x0
    clc                                    ; C0A7ED m1x0
    bra loc_C0A7F2                         ; C0A7EE m1x0

loc_C0A7F0:
    asl                                    ; C0A7F0 m1x0
    asl                                    ; C0A7F1 m1x0

loc_C0A7F2:
    sta.b $24                              ; C0A7F2 m1x0
    rep.b #$20                             ; C0A7F4 m1x0
    lda.b $1A                              ; C0A7F6 m0x0
    sta.b dma_pending_mask,x               ; C0A7F8 m0x0
    inx                                    ; C0A7FA m0x0
    inx                                    ; C0A7FB m0x0
    inx                                    ; C0A7FC m0x0
    inx                                    ; C0A7FD m0x0

loc_C0A7FE:
    clc                                    ; C0A7FE m0x0
    lda.b $1A                              ; C0A7FF m0x0
    inc                                    ; C0A801 m0x0
    inc                                    ; C0A802 m0x0
    bit.w #$0010                           ; C0A803 m0x0
    beq loc_C0A80B                         ; C0A806 m0x0
    adc.w #$0010                           ; C0A808 m0x0

loc_C0A80B:
    sta.b $1A                              ; C0A80B m0x0
    sep.b #$20                             ; C0A80D m0x0
    iny                                    ; C0A80F m1x0
    iny                                    ; C0A810 m1x0
    bra loc_C0A7B5                         ; C0A811 m1x0

loc_C0A813:
    rep.b #$20                             ; C0A813 m1x0
    txa                                    ; C0A815 m0x0
    lsr                                    ; C0A816 m0x0
    lsr                                    ; C0A817 m0x0
    sep.b #$20                             ; C0A818 m0x0
    adc.b $1D                              ; C0A81A m1x0
    bmi loc_C0A826                         ; C0A81C m1x0
    rep.b #$20                             ; C0A81E m1x0
    stx.b oam_write_ptr                    ; C0A820 m0x0
    pla                                    ; C0A822 m0x0
    jmp.w loc_C0A6CD                       ; C0A823 m0x0

loc_C0A826:
    lda.b $1E                              ; C0A826 m1x0
    clc                                    ; C0A828 m1x0
    adc.b $18                              ; C0A829 m1x0
    sta.b $1A                              ; C0A82B m1x0

loc_C0A82D:
    dec.b $1D                              ; C0A82D m1x0
    bmi loc_C0A882                         ; C0A82F m1x0
    lda.b [sprite_frame_ptr2],y            ; C0A831 m1x0
    rep.b #$20                             ; C0A833 m1x0
    and.w #$00FF                           ; C0A835 m0x0
    clc                                    ; C0A838 m0x0
    adc.b entity_screen_y                  ; C0A839 m0x0
    cmp.w #$00F0                           ; C0A83B m0x0
    bcs loc_C0A87A                         ; C0A83E m0x0
    sbc.w #$000F                           ; C0A840 m0x0
    sta.b $01,x                            ; C0A843 m0x0
    lda.b [sprite_frame_ptr],y             ; C0A845 m0x0
    and.w #$00FF                           ; C0A847 m0x0
    clc                                    ; C0A84A m0x0
    adc.b entity_screen_x                  ; C0A84B m0x0
    bit.w #$0100                           ; C0A84D m0x0
    sep.b #$20                             ; C0A850 m0x0
    sta.b nmi_handler_ptr,x                ; C0A852 m1x0
    beq loc_C0A870                         ; C0A854 m1x0
    stx.b $52                              ; C0A856 m1x0
    rep.b #$20                             ; C0A858 m1x0
    txa                                    ; C0A85A m0x0
    and.w #$01FC                           ; C0A85B m0x0
    lsr                                    ; C0A85E m0x0
    lsr                                    ; C0A85F m0x0
    tax                                    ; C0A860 m0x0
    lsr                                    ; C0A861 m0x0
    lsr                                    ; C0A862 m0x0
    sep.b #$20                             ; C0A863 m0x0
    sta.b oam_entry_ptr                    ; C0A865 m1x0
    lda.w oam_size_preset_table,x          ; C0A867 m1x0
    ora.b (oam_entry_ptr)                  ; C0A86A m1x0
    sta.b (oam_entry_ptr)                  ; C0A86C m1x0
    ldx.b $52                              ; C0A86E m1x0

loc_C0A870:
    rep.b #$20                             ; C0A870 m1x0
    lda.b $1A                              ; C0A872 m0x0
    sta.b dma_pending_mask,x               ; C0A874 m0x0
    inx                                    ; C0A876 m0x0
    inx                                    ; C0A877 m0x0
    inx                                    ; C0A878 m0x0
    inx                                    ; C0A879 m0x0

loc_C0A87A:
    inc.b $1A                              ; C0A87A m0x0
    sep.b #$20                             ; C0A87C m0x0
    iny                                    ; C0A87E m1x0
    iny                                    ; C0A87F m1x0
    bra loc_C0A82D                         ; C0A880 m1x0

loc_C0A882:
    rep.b #$20                             ; C0A882 m1x0
    txa                                    ; C0A884 m0x0
    lsr                                    ; C0A885 m0x0
    lsr                                    ; C0A886 m0x0
    sep.b #$20                             ; C0A887 m0x0
    adc.b $1F                              ; C0A889 m1x0
    bmi loc_C0A895                         ; C0A88B m1x0
    rep.b #$20                             ; C0A88D m1x0
    stx.b oam_write_ptr                    ; C0A88F m0x0
    pla                                    ; C0A891 m0x0
    jmp.w loc_C0A6CD                       ; C0A892 m0x0

loc_C0A895:
    lda.b $20                              ; C0A895 m1x0
    clc                                    ; C0A897 m1x0
    adc.b $18                              ; C0A898 m1x0
    sta.b $1A                              ; C0A89A m1x0

loc_C0A89C:
    dec.b $1F                              ; C0A89C m1x0
    bmi loc_C0A8F1                         ; C0A89E m1x0
    lda.b [sprite_frame_ptr2],y            ; C0A8A0 m1x0
    rep.b #$20                             ; C0A8A2 m1x0
    and.w #$00FF                           ; C0A8A4 m0x0
    clc                                    ; C0A8A7 m0x0
    adc.b entity_screen_y                  ; C0A8A8 m0x0
    cmp.w #$00F0                           ; C0A8AA m0x0
    bcs loc_C0A8E9                         ; C0A8AD m0x0
    sbc.w #$000F                           ; C0A8AF m0x0
    sta.b $01,x                            ; C0A8B2 m0x0
    lda.b [sprite_frame_ptr],y             ; C0A8B4 m0x0
    and.w #$00FF                           ; C0A8B6 m0x0
    clc                                    ; C0A8B9 m0x0
    adc.b entity_screen_x                  ; C0A8BA m0x0
    bit.w #$0100                           ; C0A8BC m0x0
    sep.b #$20                             ; C0A8BF m0x0
    sta.b nmi_handler_ptr,x                ; C0A8C1 m1x0
    beq loc_C0A8DF                         ; C0A8C3 m1x0
    stx.b $52                              ; C0A8C5 m1x0
    rep.b #$20                             ; C0A8C7 m1x0
    txa                                    ; C0A8C9 m0x0
    and.w #$01FC                           ; C0A8CA m0x0
    lsr                                    ; C0A8CD m0x0
    lsr                                    ; C0A8CE m0x0
    tax                                    ; C0A8CF m0x0
    lsr                                    ; C0A8D0 m0x0
    lsr                                    ; C0A8D1 m0x0
    sep.b #$20                             ; C0A8D2 m0x0
    sta.b oam_entry_ptr                    ; C0A8D4 m1x0
    lda.w oam_size_preset_table,x          ; C0A8D6 m1x0
    ora.b (oam_entry_ptr)                  ; C0A8D9 m1x0
    sta.b (oam_entry_ptr)                  ; C0A8DB m1x0
    ldx.b $52                              ; C0A8DD m1x0

loc_C0A8DF:
    rep.b #$20                             ; C0A8DF m1x0
    lda.b $1A                              ; C0A8E1 m0x0
    sta.b dma_pending_mask,x               ; C0A8E3 m0x0
    inx                                    ; C0A8E5 m0x0
    inx                                    ; C0A8E6 m0x0
    inx                                    ; C0A8E7 m0x0
    inx                                    ; C0A8E8 m0x0

loc_C0A8E9:
    inc.b $1A                              ; C0A8E9 m0x0
    sep.b #$20                             ; C0A8EB m0x0
    iny                                    ; C0A8ED m1x0
    iny                                    ; C0A8EE m1x0
    bra loc_C0A89C                         ; C0A8EF m1x0

loc_C0A8F1:
    rep.b #$20                             ; C0A8F1 m1x0
    stx.b oam_write_ptr                    ; C0A8F3 m0x0
    rts                                    ; C0A8F5 m0x0

oam_emit_frame_1row_flip:
    ldy.w #$0000                           ; C0A8F6 m0x0
    lda.b [sprite_frame_ptr],y             ; C0A8F9 m0x0
    sta.b $1C                              ; C0A8FB m0x0
    ldy.w #$0002                           ; C0A8FD m0x0
    lda.b [sprite_frame_ptr],y             ; C0A900 m0x0
    sta.b $1E                              ; C0A902 m0x0
    ldy.w #$0004                           ; C0A904 m0x0
    lda.b [sprite_frame_ptr],y             ; C0A907 m0x0
    sta.b $20                              ; C0A909 m0x0
    ldy.w #$0005                           ; C0A90B m0x0
    jmp.w loc_C0A930                       ; C0A90E m0x0

oam_emit_frame_2row_flip:
    ldy.w #$0000                           ; C0A911 m0x0
    lda.b [sprite_frame_ptr],y             ; C0A914 m0x0
    sta.b $1C                              ; C0A916 m0x0
    ldy.w #$0002                           ; C0A918 m0x0
    lda.b [sprite_frame_ptr],y             ; C0A91B m0x0
    sta.b $1E                              ; C0A91D m0x0
    ldy.w #$0004                           ; C0A91F m0x0
    lda.b [sprite_frame_ptr],y             ; C0A922 m0x0
    sta.b $20                              ; C0A924 m0x0
    ldy.w #$0006                           ; C0A926 m0x0
    lda.b [sprite_frame_ptr],y             ; C0A929 m0x0
    sta.b $22                              ; C0A92B m0x0
    ldy.w #$0008                           ; C0A92D m0x0

loc_C0A930:
    lda.b oam_write_ptr                    ; C0A930 m0x0
    lsr                                    ; C0A932 m0x0
    lsr                                    ; C0A933 m0x0
    sep.b #$20                             ; C0A934 m0x0
    tax                                    ; C0A936 m1x0
    adc.b $1C                              ; C0A937 m1x0
    bmi loc_C0A941                         ; C0A939 m1x0
    rep.b #$20                             ; C0A93B m1x0
    pla                                    ; C0A93D m0x0
    jmp.w loc_C0A6CD                       ; C0A93E m0x0

loc_C0A941:
    txa                                    ; C0A941 m1x0
    lsr                                    ; C0A942 m1x0
    lsr                                    ; C0A943 m1x0
    and.b #$1F                             ; C0A944 m1x0
    sta.b oam_entry_ptr                    ; C0A946 m1x0
    txa                                    ; C0A948 m1x0
    and.b #$03                             ; C0A949 m1x0
    tax                                    ; C0A94B m1x0
    lda.w oam_size_bit_mask_table,x        ; C0A94C m1x0
    sta.b $24                              ; C0A94F m1x0
    ldx.b oam_write_ptr                    ; C0A951 m1x0
    clc                                    ; C0A953 m1x0

loc_C0A954:
    dec.b $1C                              ; C0A954 m1x0
    bmi loc_C0A9B5                         ; C0A956 m1x0
    lda.b [sprite_frame_ptr2],y            ; C0A958 m1x0
    rep.b #$20                             ; C0A95A m1x0
    and.w #$00FF                           ; C0A95C m0x0
    adc.b entity_screen_y                  ; C0A95F m0x0
    cmp.w #$00F0                           ; C0A961 m0x0
    bcs loc_C0A9A0                         ; C0A964 m0x0
    sbc.w #$000F                           ; C0A966 m0x0
    sta.b $01,x                            ; C0A969 m0x0
    lda.b [sprite_frame_ptr],y             ; C0A96B m0x0
    eor.w #$00FF                           ; C0A96D m0x0
    and.w #$00FF                           ; C0A970 m0x0
    clc                                    ; C0A973 m0x0
    adc.b entity_screen_x                  ; C0A974 m0x0
    cmp.w #$0100                           ; C0A976 m0x0
    sep.b #$20                             ; C0A979 m0x0
    sta.b nmi_handler_ptr,x                ; C0A97B m1x0
    lda.b $24                              ; C0A97D m1x0
    bcs loc_C0A983                         ; C0A97F m1x0
    and.b #$AA                             ; C0A981 m1x0

loc_C0A983:
    ora.b (oam_entry_ptr)                  ; C0A983 m1x0
    sta.b (oam_entry_ptr)                  ; C0A985 m1x0
    lda.b $24                              ; C0A987 m1x0
    bpl loc_C0A992                         ; C0A989 m1x0
    inc.b oam_entry_ptr                    ; C0A98B m1x0
    lda.b #$03                             ; C0A98D m1x0
    clc                                    ; C0A98F m1x0
    bra loc_C0A994                         ; C0A990 m1x0

loc_C0A992:
    asl                                    ; C0A992 m1x0
    asl                                    ; C0A993 m1x0

loc_C0A994:
    sta.b $24                              ; C0A994 m1x0
    rep.b #$20                             ; C0A996 m1x0
    lda.b $1A                              ; C0A998 m0x0
    sta.b dma_pending_mask,x               ; C0A99A m0x0
    inx                                    ; C0A99C m0x0
    inx                                    ; C0A99D m0x0
    inx                                    ; C0A99E m0x0
    inx                                    ; C0A99F m0x0

loc_C0A9A0:
    clc                                    ; C0A9A0 m0x0
    lda.b $1A                              ; C0A9A1 m0x0
    inc                                    ; C0A9A3 m0x0
    inc                                    ; C0A9A4 m0x0
    bit.w #$0010                           ; C0A9A5 m0x0
    beq loc_C0A9AD                         ; C0A9A8 m0x0
    adc.w #$0010                           ; C0A9AA m0x0

loc_C0A9AD:
    sta.b $1A                              ; C0A9AD m0x0
    sep.b #$20                             ; C0A9AF m0x0
    iny                                    ; C0A9B1 m1x0
    iny                                    ; C0A9B2 m1x0
    bra loc_C0A954                         ; C0A9B3 m1x0

loc_C0A9B5:
    rep.b #$20                             ; C0A9B5 m1x0
    txa                                    ; C0A9B7 m0x0
    lsr                                    ; C0A9B8 m0x0
    lsr                                    ; C0A9B9 m0x0
    sep.b #$20                             ; C0A9BA m0x0
    adc.b $1D                              ; C0A9BC m1x0
    bmi loc_C0A9C8                         ; C0A9BE m1x0
    rep.b #$20                             ; C0A9C0 m1x0
    stx.b oam_write_ptr                    ; C0A9C2 m0x0
    pla                                    ; C0A9C4 m0x0
    jmp.w loc_C0A6CD                       ; C0A9C5 m0x0

loc_C0A9C8:
    lda.b $1E                              ; C0A9C8 m1x0
    clc                                    ; C0A9CA m1x0
    adc.b $18                              ; C0A9CB m1x0
    sta.b $1A                              ; C0A9CD m1x0
    rep.b #$20                             ; C0A9CF m1x0
    lda.b entity_screen_x                  ; C0A9D1 m0x0
    clc                                    ; C0A9D3 m0x0
    adc.w #$0008                           ; C0A9D4 m0x0
    sta.b entity_screen_x                  ; C0A9D7 m0x0
    sep.b #$20                             ; C0A9D9 m0x0

loc_C0A9DB:
    dec.b $1D                              ; C0A9DB m1x0
    bmi loc_C0AA33                         ; C0A9DD m1x0
    lda.b [sprite_frame_ptr2],y            ; C0A9DF m1x0
    rep.b #$20                             ; C0A9E1 m1x0
    and.w #$00FF                           ; C0A9E3 m0x0
    clc                                    ; C0A9E6 m0x0
    adc.b entity_screen_y                  ; C0A9E7 m0x0
    cmp.w #$00F0                           ; C0A9E9 m0x0
    bcs loc_C0AA2B                         ; C0A9EC m0x0
    sbc.w #$000F                           ; C0A9EE m0x0
    sta.b $01,x                            ; C0A9F1 m0x0
    lda.b [sprite_frame_ptr],y             ; C0A9F3 m0x0
    eor.w #$00FF                           ; C0A9F5 m0x0
    and.w #$00FF                           ; C0A9F8 m0x0
    clc                                    ; C0A9FB m0x0
    adc.b entity_screen_x                  ; C0A9FC m0x0
    bit.w #$0100                           ; C0A9FE m0x0
    sep.b #$20                             ; C0AA01 m0x0
    sta.b nmi_handler_ptr,x                ; C0AA03 m1x0
    beq loc_C0AA21                         ; C0AA05 m1x0
    stx.b $52                              ; C0AA07 m1x0
    rep.b #$20                             ; C0AA09 m1x0
    txa                                    ; C0AA0B m0x0
    and.w #$01FC                           ; C0AA0C m0x0
    lsr                                    ; C0AA0F m0x0
    lsr                                    ; C0AA10 m0x0
    tax                                    ; C0AA11 m0x0
    lsr                                    ; C0AA12 m0x0
    lsr                                    ; C0AA13 m0x0
    sep.b #$20                             ; C0AA14 m0x0
    sta.b oam_entry_ptr                    ; C0AA16 m1x0
    lda.w oam_size_preset_table,x          ; C0AA18 m1x0
    ora.b (oam_entry_ptr)                  ; C0AA1B m1x0
    sta.b (oam_entry_ptr)                  ; C0AA1D m1x0
    ldx.b $52                              ; C0AA1F m1x0

loc_C0AA21:
    rep.b #$20                             ; C0AA21 m1x0
    lda.b $1A                              ; C0AA23 m0x0
    sta.b dma_pending_mask,x               ; C0AA25 m0x0
    inx                                    ; C0AA27 m0x0
    inx                                    ; C0AA28 m0x0
    inx                                    ; C0AA29 m0x0
    inx                                    ; C0AA2A m0x0

loc_C0AA2B:
    inc.b $1A                              ; C0AA2B m0x0
    sep.b #$20                             ; C0AA2D m0x0
    iny                                    ; C0AA2F m1x0
    iny                                    ; C0AA30 m1x0
    bra loc_C0A9DB                         ; C0AA31 m1x0

loc_C0AA33:
    rep.b #$20                             ; C0AA33 m1x0
    txa                                    ; C0AA35 m0x0
    lsr                                    ; C0AA36 m0x0
    lsr                                    ; C0AA37 m0x0
    sep.b #$20                             ; C0AA38 m0x0
    adc.b $1F                              ; C0AA3A m1x0
    bmi loc_C0AA46                         ; C0AA3C m1x0
    rep.b #$20                             ; C0AA3E m1x0
    stx.b oam_write_ptr                    ; C0AA40 m0x0
    pla                                    ; C0AA42 m0x0
    jmp.w loc_C0A6CD                       ; C0AA43 m0x0

loc_C0AA46:
    lda.b $20                              ; C0AA46 m1x0
    clc                                    ; C0AA48 m1x0
    adc.b $18                              ; C0AA49 m1x0
    sta.b $1A                              ; C0AA4B m1x0

loc_C0AA4D:
    dec.b $1F                              ; C0AA4D m1x0
    bmi loc_C0AAA5                         ; C0AA4F m1x0
    lda.b [sprite_frame_ptr2],y            ; C0AA51 m1x0
    rep.b #$20                             ; C0AA53 m1x0
    and.w #$00FF                           ; C0AA55 m0x0
    clc                                    ; C0AA58 m0x0
    adc.b entity_screen_y                  ; C0AA59 m0x0
    cmp.w #$00F0                           ; C0AA5B m0x0
    bcs loc_C0AA9D                         ; C0AA5E m0x0
    sbc.w #$000F                           ; C0AA60 m0x0
    sta.b $01,x                            ; C0AA63 m0x0
    lda.b [sprite_frame_ptr],y             ; C0AA65 m0x0
    eor.w #$00FF                           ; C0AA67 m0x0
    and.w #$00FF                           ; C0AA6A m0x0
    clc                                    ; C0AA6D m0x0
    adc.b entity_screen_x                  ; C0AA6E m0x0
    bit.w #$0100                           ; C0AA70 m0x0
    sep.b #$20                             ; C0AA73 m0x0
    sta.b nmi_handler_ptr,x                ; C0AA75 m1x0
    beq loc_C0AA93                         ; C0AA77 m1x0
    stx.b $52                              ; C0AA79 m1x0
    rep.b #$20                             ; C0AA7B m1x0
    txa                                    ; C0AA7D m0x0
    and.w #$01FC                           ; C0AA7E m0x0
    lsr                                    ; C0AA81 m0x0
    lsr                                    ; C0AA82 m0x0
    tax                                    ; C0AA83 m0x0
    lsr                                    ; C0AA84 m0x0
    lsr                                    ; C0AA85 m0x0
    sep.b #$20                             ; C0AA86 m0x0
    sta.b oam_entry_ptr                    ; C0AA88 m1x0
    lda.w oam_size_preset_table,x          ; C0AA8A m1x0
    ora.b (oam_entry_ptr)                  ; C0AA8D m1x0
    sta.b (oam_entry_ptr)                  ; C0AA8F m1x0
    ldx.b $52                              ; C0AA91 m1x0

loc_C0AA93:
    rep.b #$20                             ; C0AA93 m1x0
    lda.b $1A                              ; C0AA95 m0x0
    sta.b dma_pending_mask,x               ; C0AA97 m0x0
    inx                                    ; C0AA99 m0x0
    inx                                    ; C0AA9A m0x0
    inx                                    ; C0AA9B m0x0
    inx                                    ; C0AA9C m0x0

loc_C0AA9D:
    inc.b $1A                              ; C0AA9D m0x0
    sep.b #$20                             ; C0AA9F m0x0
    iny                                    ; C0AAA1 m1x0
    iny                                    ; C0AAA2 m1x0
    bra loc_C0AA4D                         ; C0AAA3 m1x0

loc_C0AAA5:
    rep.b #$20                             ; C0AAA5 m1x0
    stx.b oam_write_ptr                    ; C0AAA7 m0x0
    rts                                    ; C0AAA9 m0x0

oam_emit_frame_3row:
    ldy.w #$0000                           ; C0AAAA m0x0
    lda.b [sprite_frame_ptr],y             ; C0AAAD m0x0
    sta.b $1C                              ; C0AAAF m0x0
    ldy.w #$0002                           ; C0AAB1 m0x0
    lda.b [sprite_frame_ptr],y             ; C0AAB4 m0x0
    sta.b $1E                              ; C0AAB6 m0x0
    ldy.w #$0004                           ; C0AAB8 m0x0
    lda.b [sprite_frame_ptr],y             ; C0AABB m0x0
    sta.b $20                              ; C0AABD m0x0
    ldy.w #$0006                           ; C0AABF m0x0
    lda.b [sprite_frame_ptr],y             ; C0AAC2 m0x0
    sta.b $22                              ; C0AAC4 m0x0
    ldy.w #$0008                           ; C0AAC6 m0x0
    lda.b oam_write_ptr                    ; C0AAC9 m0x0
    lsr                                    ; C0AACB m0x0
    lsr                                    ; C0AACC m0x0
    sep.b #$20                             ; C0AACD m0x0
    tax                                    ; C0AACF m1x0
    adc.b $1C                              ; C0AAD0 m1x0
    bmi loc_C0AADA                         ; C0AAD2 m1x0
    rep.b #$20                             ; C0AAD4 m1x0
    pla                                    ; C0AAD6 m0x0
    jmp.w loc_C0A6CD                       ; C0AAD7 m0x0

loc_C0AADA:
    txa                                    ; C0AADA m1x0
    lsr                                    ; C0AADB m1x0
    lsr                                    ; C0AADC m1x0
    and.b #$1F                             ; C0AADD m1x0
    sta.b oam_entry_ptr                    ; C0AADF m1x0
    txa                                    ; C0AAE1 m1x0
    and.b #$03                             ; C0AAE2 m1x0
    tax                                    ; C0AAE4 m1x0
    lda.w oam_size_bit_mask_table,x        ; C0AAE5 m1x0
    sta.b $24                              ; C0AAE8 m1x0
    ldx.b oam_write_ptr                    ; C0AAEA m1x0
    clc                                    ; C0AAEC m1x0

loc_C0AAED:
    dec.b $1C                              ; C0AAED m1x0
    bmi loc_C0AB4D                         ; C0AAEF m1x0
    lda.b [sprite_frame_ptr2],y            ; C0AAF1 m1x0
    eor.b #$FF                             ; C0AAF3 m1x0
    rep.b #$20                             ; C0AAF5 m1x0
    and.w #$00FF                           ; C0AAF7 m0x0
    adc.b entity_screen_y                  ; C0AAFA m0x0
    cmp.w #$00F0                           ; C0AAFC m0x0
    bcs loc_C0AB38                         ; C0AAFF m0x0
    sbc.w #$000F                           ; C0AB01 m0x0
    sta.b $01,x                            ; C0AB04 m0x0
    lda.b [sprite_frame_ptr],y             ; C0AB06 m0x0
    and.w #$00FF                           ; C0AB08 m0x0
    clc                                    ; C0AB0B m0x0
    adc.b entity_screen_x                  ; C0AB0C m0x0
    cmp.w #$0100                           ; C0AB0E m0x0
    sep.b #$20                             ; C0AB11 m0x0
    sta.b nmi_handler_ptr,x                ; C0AB13 m1x0
    lda.b $24                              ; C0AB15 m1x0
    bcs loc_C0AB1B                         ; C0AB17 m1x0
    and.b #$AA                             ; C0AB19 m1x0

loc_C0AB1B:
    ora.b (oam_entry_ptr)                  ; C0AB1B m1x0
    sta.b (oam_entry_ptr)                  ; C0AB1D m1x0
    lda.b $24                              ; C0AB1F m1x0
    bpl loc_C0AB2A                         ; C0AB21 m1x0
    inc.b oam_entry_ptr                    ; C0AB23 m1x0
    lda.b #$03                             ; C0AB25 m1x0
    clc                                    ; C0AB27 m1x0
    bra loc_C0AB2C                         ; C0AB28 m1x0

loc_C0AB2A:
    asl                                    ; C0AB2A m1x0
    asl                                    ; C0AB2B m1x0

loc_C0AB2C:
    sta.b $24                              ; C0AB2C m1x0
    rep.b #$20                             ; C0AB2E m1x0
    lda.b $1A                              ; C0AB30 m0x0
    sta.b dma_pending_mask,x               ; C0AB32 m0x0
    inx                                    ; C0AB34 m0x0
    inx                                    ; C0AB35 m0x0
    inx                                    ; C0AB36 m0x0
    inx                                    ; C0AB37 m0x0

loc_C0AB38:
    clc                                    ; C0AB38 m0x0
    lda.b $1A                              ; C0AB39 m0x0
    inc                                    ; C0AB3B m0x0
    inc                                    ; C0AB3C m0x0
    bit.w #$0010                           ; C0AB3D m0x0
    beq loc_C0AB45                         ; C0AB40 m0x0
    adc.w #$0010                           ; C0AB42 m0x0

loc_C0AB45:
    sta.b $1A                              ; C0AB45 m0x0
    sep.b #$20                             ; C0AB47 m0x0
    iny                                    ; C0AB49 m1x0
    iny                                    ; C0AB4A m1x0
    bra loc_C0AAED                         ; C0AB4B m1x0

loc_C0AB4D:
    rep.b #$20                             ; C0AB4D m1x0
    txa                                    ; C0AB4F m0x0
    lsr                                    ; C0AB50 m0x0
    lsr                                    ; C0AB51 m0x0
    sep.b #$20                             ; C0AB52 m0x0
    adc.b $1D                              ; C0AB54 m1x0
    bmi loc_C0AB60                         ; C0AB56 m1x0
    rep.b #$20                             ; C0AB58 m1x0
    stx.b oam_write_ptr                    ; C0AB5A m0x0
    pla                                    ; C0AB5C m0x0
    jmp.w loc_C0A6CD                       ; C0AB5D m0x0

loc_C0AB60:
    lda.b $1E                              ; C0AB60 m1x0
    clc                                    ; C0AB62 m1x0
    adc.b $18                              ; C0AB63 m1x0
    sta.b $1A                              ; C0AB65 m1x0
    rep.b #$20                             ; C0AB67 m1x0
    lda.b entity_screen_y                  ; C0AB69 m0x0
    clc                                    ; C0AB6B m0x0
    adc.w #$0008                           ; C0AB6C m0x0
    sta.b entity_screen_y                  ; C0AB6F m0x0
    sep.b #$20                             ; C0AB71 m0x0

loc_C0AB73:
    dec.b $1D                              ; C0AB73 m1x0
    bmi loc_C0ABCA                         ; C0AB75 m1x0
    lda.b [sprite_frame_ptr2],y            ; C0AB77 m1x0
    eor.b #$FF                             ; C0AB79 m1x0
    rep.b #$20                             ; C0AB7B m1x0
    and.w #$00FF                           ; C0AB7D m0x0
    clc                                    ; C0AB80 m0x0
    adc.b entity_screen_y                  ; C0AB81 m0x0
    cmp.w #$00F0                           ; C0AB83 m0x0
    bcs loc_C0ABC2                         ; C0AB86 m0x0
    sbc.w #$000F                           ; C0AB88 m0x0
    sta.b $01,x                            ; C0AB8B m0x0
    lda.b [sprite_frame_ptr],y             ; C0AB8D m0x0
    and.w #$00FF                           ; C0AB8F m0x0
    clc                                    ; C0AB92 m0x0
    adc.b entity_screen_x                  ; C0AB93 m0x0
    bit.w #$0100                           ; C0AB95 m0x0
    sep.b #$20                             ; C0AB98 m0x0
    sta.b nmi_handler_ptr,x                ; C0AB9A m1x0
    beq loc_C0ABB8                         ; C0AB9C m1x0
    stx.b $52                              ; C0AB9E m1x0
    rep.b #$20                             ; C0ABA0 m1x0
    txa                                    ; C0ABA2 m0x0
    and.w #$01FC                           ; C0ABA3 m0x0
    lsr                                    ; C0ABA6 m0x0
    lsr                                    ; C0ABA7 m0x0
    tax                                    ; C0ABA8 m0x0
    lsr                                    ; C0ABA9 m0x0
    lsr                                    ; C0ABAA m0x0
    sep.b #$20                             ; C0ABAB m0x0
    sta.b oam_entry_ptr                    ; C0ABAD m1x0
    lda.w oam_size_preset_table,x          ; C0ABAF m1x0
    ora.b (oam_entry_ptr)                  ; C0ABB2 m1x0
    sta.b (oam_entry_ptr)                  ; C0ABB4 m1x0
    ldx.b $52                              ; C0ABB6 m1x0

loc_C0ABB8:
    rep.b #$20                             ; C0ABB8 m1x0
    lda.b $1A                              ; C0ABBA m0x0
    sta.b dma_pending_mask,x               ; C0ABBC m0x0
    inx                                    ; C0ABBE m0x0
    inx                                    ; C0ABBF m0x0
    inx                                    ; C0ABC0 m0x0
    inx                                    ; C0ABC1 m0x0

loc_C0ABC2:
    inc.b $1A                              ; C0ABC2 m0x0
    sep.b #$20                             ; C0ABC4 m0x0
    iny                                    ; C0ABC6 m1x0
    iny                                    ; C0ABC7 m1x0
    bra loc_C0AB73                         ; C0ABC8 m1x0

loc_C0ABCA:
    rep.b #$20                             ; C0ABCA m1x0
    txa                                    ; C0ABCC m0x0
    lsr                                    ; C0ABCD m0x0
    lsr                                    ; C0ABCE m0x0
    sep.b #$20                             ; C0ABCF m0x0
    adc.b $1F                              ; C0ABD1 m1x0
    bmi loc_C0ABDD                         ; C0ABD3 m1x0
    rep.b #$20                             ; C0ABD5 m1x0
    stx.b oam_write_ptr                    ; C0ABD7 m0x0
    pla                                    ; C0ABD9 m0x0
    jmp.w loc_C0A6CD                       ; C0ABDA m0x0

loc_C0ABDD:
    lda.b $20                              ; C0ABDD m1x0
    clc                                    ; C0ABDF m1x0
    adc.b $18                              ; C0ABE0 m1x0
    sta.b $1A                              ; C0ABE2 m1x0

loc_C0ABE4:
    dec.b $1F                              ; C0ABE4 m1x0
    bmi loc_C0AC3B                         ; C0ABE6 m1x0
    lda.b [sprite_frame_ptr2],y            ; C0ABE8 m1x0
    eor.b #$FF                             ; C0ABEA m1x0
    rep.b #$20                             ; C0ABEC m1x0
    and.w #$00FF                           ; C0ABEE m0x0
    clc                                    ; C0ABF1 m0x0
    adc.b entity_screen_y                  ; C0ABF2 m0x0
    cmp.w #$00F0                           ; C0ABF4 m0x0
    bcs loc_C0AC33                         ; C0ABF7 m0x0
    sbc.w #$000F                           ; C0ABF9 m0x0
    sta.b $01,x                            ; C0ABFC m0x0
    lda.b [sprite_frame_ptr],y             ; C0ABFE m0x0
    and.w #$00FF                           ; C0AC00 m0x0
    clc                                    ; C0AC03 m0x0
    adc.b entity_screen_x                  ; C0AC04 m0x0
    bit.w #$0100                           ; C0AC06 m0x0
    sep.b #$20                             ; C0AC09 m0x0
    sta.b nmi_handler_ptr,x                ; C0AC0B m1x0
    beq loc_C0AC29                         ; C0AC0D m1x0
    stx.b $52                              ; C0AC0F m1x0
    rep.b #$20                             ; C0AC11 m1x0
    txa                                    ; C0AC13 m0x0
    and.w #$01FC                           ; C0AC14 m0x0
    lsr                                    ; C0AC17 m0x0
    lsr                                    ; C0AC18 m0x0
    tax                                    ; C0AC19 m0x0
    lsr                                    ; C0AC1A m0x0
    lsr                                    ; C0AC1B m0x0
    sep.b #$20                             ; C0AC1C m0x0
    sta.b oam_entry_ptr                    ; C0AC1E m1x0
    lda.w oam_size_preset_table,x          ; C0AC20 m1x0
    ora.b (oam_entry_ptr)                  ; C0AC23 m1x0
    sta.b (oam_entry_ptr)                  ; C0AC25 m1x0
    ldx.b $52                              ; C0AC27 m1x0

loc_C0AC29:
    rep.b #$20                             ; C0AC29 m1x0
    lda.b $1A                              ; C0AC2B m0x0
    sta.b dma_pending_mask,x               ; C0AC2D m0x0
    inx                                    ; C0AC2F m0x0
    inx                                    ; C0AC30 m0x0
    inx                                    ; C0AC31 m0x0
    inx                                    ; C0AC32 m0x0

loc_C0AC33:
    inc.b $1A                              ; C0AC33 m0x0
    sep.b #$20                             ; C0AC35 m0x0
    iny                                    ; C0AC37 m1x0
    iny                                    ; C0AC38 m1x0
    bra loc_C0ABE4                         ; C0AC39 m1x0

loc_C0AC3B:
    rep.b #$20                             ; C0AC3B m1x0
    stx.b oam_write_ptr                    ; C0AC3D m0x0
    rts                                    ; C0AC3F m0x0

oam_emit_frame_3row_flip:
    ldy.w #$0000                           ; C0AC40 m0x0
    lda.b [sprite_frame_ptr],y             ; C0AC43 m0x0
    sta.b $1C                              ; C0AC45 m0x0
    ldy.w #$0002                           ; C0AC47 m0x0
    lda.b [sprite_frame_ptr],y             ; C0AC4A m0x0
    sta.b $1E                              ; C0AC4C m0x0
    ldy.w #$0004                           ; C0AC4E m0x0
    lda.b [sprite_frame_ptr],y             ; C0AC51 m0x0
    sta.b $20                              ; C0AC53 m0x0
    ldy.w #$0006                           ; C0AC55 m0x0
    lda.b [sprite_frame_ptr],y             ; C0AC58 m0x0
    sta.b $22                              ; C0AC5A m0x0
    ldy.w #$0008                           ; C0AC5C m0x0
    lda.b oam_write_ptr                    ; C0AC5F m0x0
    lsr                                    ; C0AC61 m0x0
    lsr                                    ; C0AC62 m0x0
    sep.b #$20                             ; C0AC63 m0x0
    tax                                    ; C0AC65 m1x0
    adc.b $1C                              ; C0AC66 m1x0
    bmi loc_C0AC70                         ; C0AC68 m1x0
    rep.b #$20                             ; C0AC6A m1x0
    pla                                    ; C0AC6C m0x0
    jmp.w loc_C0A6CD                       ; C0AC6D m0x0

loc_C0AC70:
    txa                                    ; C0AC70 m1x0
    lsr                                    ; C0AC71 m1x0
    lsr                                    ; C0AC72 m1x0
    and.b #$1F                             ; C0AC73 m1x0
    sta.b oam_entry_ptr                    ; C0AC75 m1x0
    txa                                    ; C0AC77 m1x0
    and.b #$03                             ; C0AC78 m1x0
    tax                                    ; C0AC7A m1x0
    lda.w oam_size_bit_mask_table,x        ; C0AC7B m1x0
    sta.b $24                              ; C0AC7E m1x0
    ldx.b oam_write_ptr                    ; C0AC80 m1x0
    clc                                    ; C0AC82 m1x0

loc_C0AC83:
    dec.b $1C                              ; C0AC83 m1x0
    bmi loc_C0ACE6                         ; C0AC85 m1x0
    lda.b [sprite_frame_ptr2],y            ; C0AC87 m1x0
    eor.b #$FF                             ; C0AC89 m1x0
    rep.b #$20                             ; C0AC8B m1x0
    and.w #$00FF                           ; C0AC8D m0x0
    adc.b entity_screen_y                  ; C0AC90 m0x0
    cmp.w #$00F0                           ; C0AC92 m0x0
    bcs loc_C0ACD1                         ; C0AC95 m0x0
    sbc.w #$000F                           ; C0AC97 m0x0
    sta.b $01,x                            ; C0AC9A m0x0
    lda.b [sprite_frame_ptr],y             ; C0AC9C m0x0
    eor.w #$00FF                           ; C0AC9E m0x0
    and.w #$00FF                           ; C0ACA1 m0x0
    clc                                    ; C0ACA4 m0x0
    adc.b entity_screen_x                  ; C0ACA5 m0x0
    cmp.w #$0100                           ; C0ACA7 m0x0
    sep.b #$20                             ; C0ACAA m0x0
    sta.b nmi_handler_ptr,x                ; C0ACAC m1x0
    lda.b $24                              ; C0ACAE m1x0
    bcs loc_C0ACB4                         ; C0ACB0 m1x0
    and.b #$AA                             ; C0ACB2 m1x0

loc_C0ACB4:
    ora.b (oam_entry_ptr)                  ; C0ACB4 m1x0
    sta.b (oam_entry_ptr)                  ; C0ACB6 m1x0
    lda.b $24                              ; C0ACB8 m1x0
    bpl loc_C0ACC3                         ; C0ACBA m1x0
    inc.b oam_entry_ptr                    ; C0ACBC m1x0
    lda.b #$03                             ; C0ACBE m1x0
    clc                                    ; C0ACC0 m1x0
    bra loc_C0ACC5                         ; C0ACC1 m1x0

loc_C0ACC3:
    asl                                    ; C0ACC3 m1x0
    asl                                    ; C0ACC4 m1x0

loc_C0ACC5:
    sta.b $24                              ; C0ACC5 m1x0
    rep.b #$20                             ; C0ACC7 m1x0
    lda.b $1A                              ; C0ACC9 m0x0
    sta.b dma_pending_mask,x               ; C0ACCB m0x0
    inx                                    ; C0ACCD m0x0
    inx                                    ; C0ACCE m0x0
    inx                                    ; C0ACCF m0x0
    inx                                    ; C0ACD0 m0x0

loc_C0ACD1:
    clc                                    ; C0ACD1 m0x0
    lda.b $1A                              ; C0ACD2 m0x0
    inc                                    ; C0ACD4 m0x0
    inc                                    ; C0ACD5 m0x0
    bit.w #$0010                           ; C0ACD6 m0x0
    beq loc_C0ACDE                         ; C0ACD9 m0x0
    adc.w #$0010                           ; C0ACDB m0x0

loc_C0ACDE:
    sta.b $1A                              ; C0ACDE m0x0
    sep.b #$20                             ; C0ACE0 m0x0
    iny                                    ; C0ACE2 m1x0
    iny                                    ; C0ACE3 m1x0
    bra loc_C0AC83                         ; C0ACE4 m1x0

loc_C0ACE6:
    rep.b #$20                             ; C0ACE6 m1x0
    txa                                    ; C0ACE8 m0x0
    lsr                                    ; C0ACE9 m0x0
    lsr                                    ; C0ACEA m0x0
    sep.b #$20                             ; C0ACEB m0x0
    adc.b $1D                              ; C0ACED m1x0
    bmi loc_C0ACF9                         ; C0ACEF m1x0
    rep.b #$20                             ; C0ACF1 m1x0
    stx.b oam_write_ptr                    ; C0ACF3 m0x0
    pla                                    ; C0ACF5 m0x0
    jmp.w loc_C0A6CD                       ; C0ACF6 m0x0

loc_C0ACF9:
    lda.b $1E                              ; C0ACF9 m1x0
    clc                                    ; C0ACFB m1x0
    adc.b $18                              ; C0ACFC m1x0
    sta.b $1A                              ; C0ACFE m1x0
    rep.b #$20                             ; C0AD00 m1x0
    lda.b entity_screen_x                  ; C0AD02 m0x0
    clc                                    ; C0AD04 m0x0
    adc.w #$0008                           ; C0AD05 m0x0
    sta.b entity_screen_x                  ; C0AD08 m0x0
    lda.b entity_screen_y                  ; C0AD0A m0x0
    clc                                    ; C0AD0C m0x0
    adc.w #$0008                           ; C0AD0D m0x0
    sta.b entity_screen_y                  ; C0AD10 m0x0
    sep.b #$20                             ; C0AD12 m0x0

loc_C0AD14:
    dec.b $1D                              ; C0AD14 m1x0
    bmi loc_C0AD6E                         ; C0AD16 m1x0
    lda.b [sprite_frame_ptr2],y            ; C0AD18 m1x0
    eor.b #$FF                             ; C0AD1A m1x0
    rep.b #$20                             ; C0AD1C m1x0
    and.w #$00FF                           ; C0AD1E m0x0
    clc                                    ; C0AD21 m0x0
    adc.b entity_screen_y                  ; C0AD22 m0x0
    cmp.w #$00F0                           ; C0AD24 m0x0
    bcs loc_C0AD66                         ; C0AD27 m0x0
    sbc.w #$000F                           ; C0AD29 m0x0
    sta.b $01,x                            ; C0AD2C m0x0
    lda.b [sprite_frame_ptr],y             ; C0AD2E m0x0
    eor.w #$00FF                           ; C0AD30 m0x0
    and.w #$00FF                           ; C0AD33 m0x0
    clc                                    ; C0AD36 m0x0
    adc.b entity_screen_x                  ; C0AD37 m0x0
    bit.w #$0100                           ; C0AD39 m0x0
    sep.b #$20                             ; C0AD3C m0x0
    sta.b nmi_handler_ptr,x                ; C0AD3E m1x0
    beq loc_C0AD5C                         ; C0AD40 m1x0
    stx.b $52                              ; C0AD42 m1x0
    rep.b #$20                             ; C0AD44 m1x0
    txa                                    ; C0AD46 m0x0
    and.w #$01FC                           ; C0AD47 m0x0
    lsr                                    ; C0AD4A m0x0
    lsr                                    ; C0AD4B m0x0
    tax                                    ; C0AD4C m0x0
    lsr                                    ; C0AD4D m0x0
    lsr                                    ; C0AD4E m0x0
    sep.b #$20                             ; C0AD4F m0x0
    sta.b oam_entry_ptr                    ; C0AD51 m1x0
    lda.w oam_size_preset_table,x          ; C0AD53 m1x0
    ora.b (oam_entry_ptr)                  ; C0AD56 m1x0
    sta.b (oam_entry_ptr)                  ; C0AD58 m1x0
    ldx.b $52                              ; C0AD5A m1x0

loc_C0AD5C:
    rep.b #$20                             ; C0AD5C m1x0
    lda.b $1A                              ; C0AD5E m0x0
    sta.b dma_pending_mask,x               ; C0AD60 m0x0
    inx                                    ; C0AD62 m0x0
    inx                                    ; C0AD63 m0x0
    inx                                    ; C0AD64 m0x0
    inx                                    ; C0AD65 m0x0

loc_C0AD66:
    inc.b $1A                              ; C0AD66 m0x0
    sep.b #$20                             ; C0AD68 m0x0
    iny                                    ; C0AD6A m1x0
    iny                                    ; C0AD6B m1x0
    bra loc_C0AD14                         ; C0AD6C m1x0

loc_C0AD6E:
    rep.b #$20                             ; C0AD6E m1x0
    txa                                    ; C0AD70 m0x0
    lsr                                    ; C0AD71 m0x0
    lsr                                    ; C0AD72 m0x0
    sep.b #$20                             ; C0AD73 m0x0
    adc.b $1F                              ; C0AD75 m1x0
    bmi loc_C0AD81                         ; C0AD77 m1x0
    rep.b #$20                             ; C0AD79 m1x0
    stx.b oam_write_ptr                    ; C0AD7B m0x0
    pla                                    ; C0AD7D m0x0
    jmp.w loc_C0A6CD                       ; C0AD7E m0x0

loc_C0AD81:
    lda.b $20                              ; C0AD81 m1x0
    clc                                    ; C0AD83 m1x0
    adc.b $18                              ; C0AD84 m1x0
    sta.b $1A                              ; C0AD86 m1x0

loc_C0AD88:
    dec.b $1F                              ; C0AD88 m1x0
    bmi loc_C0ADE2                         ; C0AD8A m1x0
    lda.b [sprite_frame_ptr2],y            ; C0AD8C m1x0
    eor.b #$FF                             ; C0AD8E m1x0
    rep.b #$20                             ; C0AD90 m1x0
    and.w #$00FF                           ; C0AD92 m0x0
    clc                                    ; C0AD95 m0x0
    adc.b entity_screen_y                  ; C0AD96 m0x0
    cmp.w #$00F0                           ; C0AD98 m0x0
    bcs loc_C0ADDA                         ; C0AD9B m0x0
    sbc.w #$000F                           ; C0AD9D m0x0
    sta.b $01,x                            ; C0ADA0 m0x0
    lda.b [sprite_frame_ptr],y             ; C0ADA2 m0x0
    eor.w #$00FF                           ; C0ADA4 m0x0
    and.w #$00FF                           ; C0ADA7 m0x0
    clc                                    ; C0ADAA m0x0
    adc.b entity_screen_x                  ; C0ADAB m0x0
    bit.w #$0100                           ; C0ADAD m0x0
    sep.b #$20                             ; C0ADB0 m0x0
    sta.b nmi_handler_ptr,x                ; C0ADB2 m1x0
    beq loc_C0ADD0                         ; C0ADB4 m1x0
    stx.b $52                              ; C0ADB6 m1x0
    rep.b #$20                             ; C0ADB8 m1x0
    txa                                    ; C0ADBA m0x0
    and.w #$01FC                           ; C0ADBB m0x0
    lsr                                    ; C0ADBE m0x0
    lsr                                    ; C0ADBF m0x0
    tax                                    ; C0ADC0 m0x0
    lsr                                    ; C0ADC1 m0x0
    lsr                                    ; C0ADC2 m0x0
    sep.b #$20                             ; C0ADC3 m0x0
    sta.b oam_entry_ptr                    ; C0ADC5 m1x0
    lda.w oam_size_preset_table,x          ; C0ADC7 m1x0
    ora.b (oam_entry_ptr)                  ; C0ADCA m1x0
    sta.b (oam_entry_ptr)                  ; C0ADCC m1x0
    ldx.b $52                              ; C0ADCE m1x0

loc_C0ADD0:
    rep.b #$20                             ; C0ADD0 m1x0
    lda.b $1A                              ; C0ADD2 m0x0
    sta.b dma_pending_mask,x               ; C0ADD4 m0x0
    inx                                    ; C0ADD6 m0x0
    inx                                    ; C0ADD7 m0x0
    inx                                    ; C0ADD8 m0x0
    inx                                    ; C0ADD9 m0x0

loc_C0ADDA:
    inc.b $1A                              ; C0ADDA m0x0
    sep.b #$20                             ; C0ADDC m0x0
    iny                                    ; C0ADDE m1x0
    iny                                    ; C0ADDF m1x0
    bra loc_C0AD88                         ; C0ADE0 m1x0

loc_C0ADE2:
    rep.b #$20                             ; C0ADE2 m1x0
    stx.b oam_write_ptr                    ; C0ADE4 m0x0
    rts                                    ; C0ADE6 m0x0

oam_hide_unused_sprites:
    ldx.b oam_write_ptr                    ; C0ADE7 m0x0
    cpx.w #$0400                           ; C0ADE9 m0x0
    beq loc_C0ADFC                         ; C0ADEC m0x0
    lda.w #$F0FF                           ; C0ADEE m0x0

loc_C0ADF1:
    sta.b nmi_handler_ptr,x                ; C0ADF1 m0x0
    inx                                    ; C0ADF3 m0x0
    inx                                    ; C0ADF4 m0x0
    inx                                    ; C0ADF5 m0x0
    inx                                    ; C0ADF6 m0x0
    cpx.w #$0400                           ; C0ADF7 m0x0
    bne loc_C0ADF1                         ; C0ADFA m0x0

loc_C0ADFC:
    rts                                    ; C0ADFC m0x0

oam_dma_upload:
    lda.w #$0200                           ; C0ADFD m0x0
    sta.w A1TL0                            ; C0AE00 m0x0
    sta.w A2AL0                            ; C0AE03 m0x0
    lda.w #$0220                           ; C0AE06 m0x0
    sta.w DASL0                            ; C0AE09 m0x0
    lda.w #$0400                           ; C0AE0C m0x0
    sta.w DMAP0                            ; C0AE0F m0x0
    sep.b #$20                             ; C0AE12 m0x0
    stz.w A1B0                             ; C0AE14 m1x0
    rep.b #$20                             ; C0AE17 m1x0
    lda.w #$0001                           ; C0AE19 m0x0
    sta.b dma_pending_mask                 ; C0AE1C m0x0
    rts                                    ; C0AE1E m0x0

entity_sort_draw_order:
    lda.w entity_render_order              ; C0AE1F m0x0
    tay                                    ; C0AE22 m0x0
    lda.w entity_depth_key,y               ; C0AE23 m0x0
    sta.b $52                              ; C0AE26 m0x0
    ldx.w #$09A6                           ; C0AE28 m0x0

loc_C0AE2B:
    ldy.b ptr_04,x                         ; C0AE2B m0x0
    lda.w entity_depth_key,y               ; C0AE2D m0x0
    cmp.b $52                              ; C0AE30 m0x0
    sta.b $52                              ; C0AE32 m0x0
    bcc loc_C0AE3E                         ; C0AE34 m0x0
    beq loc_C0AE5E                         ; C0AE36 m0x0

loc_C0AE38:
    lda.b dma_pending_mask,x               ; C0AE38 m0x0
    sta.b ptr_04,x                         ; C0AE3A m0x0
    sty.b dma_pending_mask,x               ; C0AE3C m0x0

loc_C0AE3E:
    inx                                    ; C0AE3E m0x0
    inx                                    ; C0AE3F m0x0
    cpx.w #$09C4                           ; C0AE40 m0x0
    bne loc_C0AE2B                         ; C0AE43 m0x0

loc_C0AE45:
    ldy.b nmi_handler_ptr,x                ; C0AE45 m0x0
    lda.w entity_depth_key,y               ; C0AE47 m0x0
    cmp.b $52                              ; C0AE4A m0x0
    sta.b $52                              ; C0AE4C m0x0
    bcs loc_C0AE56                         ; C0AE4E m0x0
    lda.b dma_pending_mask,x               ; C0AE50 m0x0
    sta.b nmi_handler_ptr,x                ; C0AE52 m0x0
    sty.b dma_pending_mask,x               ; C0AE54 m0x0

loc_C0AE56:
    dex                                    ; C0AE56 m0x0
    dex                                    ; C0AE57 m0x0
    cpx.w #$09A6                           ; C0AE58 m0x0
    bne loc_C0AE45                         ; C0AE5B m0x0
    rts                                    ; C0AE5D m0x0

loc_C0AE5E:
    lda.w $07A8,y                          ; C0AE5E m0x0
    cmp.w #$0004                           ; C0AE61 m0x0
    beq loc_C0AE6B                         ; C0AE64 m0x0
    cmp.w #$0002                           ; C0AE66 m0x0
    bne loc_C0AE3E                         ; C0AE69 m0x0

loc_C0AE6B:
    ldy.b dma_pending_mask,x               ; C0AE6B m0x0
    lda.w $07A8,y                          ; C0AE6D m0x0
    ldy.b ptr_04,x                         ; C0AE70 m0x0
    cmp.w #$0004                           ; C0AE72 m0x0
    beq loc_C0AE3E                         ; C0AE75 m0x0
    cmp.w #$0002                           ; C0AE77 m0x0
    beq loc_C0AE3E                         ; C0AE7A m0x0
    bra loc_C0AE38                         ; C0AE7C m0x0

entity_upload_pending_tiles:
    lda.w #$1801                           ; C0AE7E m0x0
    sta.w DMAP0                            ; C0AE81 m0x0
    sep.b #$10                             ; C0AE84 m0x0
    ldy.b #$80                             ; C0AE86 m0x1
    sty.w VMAIN                            ; C0AE88 m0x1
    ldy.b #$01                             ; C0AE8B m0x1
    tdc                                    ; C0AE8D m0x1
    clc                                    ; C0AE8E m0x1

loc_C0AE8F:
    tax                                    ; C0AE8F m0x1
    lda.w $0A90,x                          ; C0AE90 m0x1
    bpl loc_C0AEB6                         ; C0AE93 m0x1
    sta.w A1B0                             ; C0AE95 m0x1
    lda.w $0A8A,x                          ; C0AE98 m0x1
    sta.w DASL0                            ; C0AE9B m0x1
    lda.w $0A8C,x                          ; C0AE9E m0x1
    sta.w VMADDL                           ; C0AEA1 m0x1
    lda.w $0A8E,x                          ; C0AEA4 m0x1
    sta.w A1TL0                            ; C0AEA7 m0x1
    sty.w MDMAEN                           ; C0AEAA m0x1
    stz.w $0A90,x                          ; C0AEAD m0x1
    txa                                    ; C0AEB0 m0x1
    adc.w #$0008                           ; C0AEB1 m0x1
    bra loc_C0AE8F                         ; C0AEB4 m0x1

loc_C0AEB6:
    rep.b #$10                             ; C0AEB6 m0x1
    rts                                    ; C0AEB8 m0x0

entity_render_order_reset:
    ldx.w #$0000                           ; C0AEB9 m0x0

loc_C0AEBC:
    txa                                    ; C0AEBC m0x0
    sta.w entity_render_order,x            ; C0AEBD m0x0
    inx                                    ; C0AEC0 m0x0
    inx                                    ; C0AEC1 m0x0
    cpx.w #$0020                           ; C0AEC2 m0x0
    bne loc_C0AEBC                         ; C0AEC5 m0x0
    rts                                    ; C0AEC7 m0x0

anim_update:
    stz.b $52                              ; C0AEC8 m0x0
    lda.w entity_anim_id,x                 ; C0AECA m0x0
    beq loc_C0AF04                         ; C0AECD m0x0
    cmp.w $0A48,x                          ; C0AECF m0x0
    beq loc_C0AF05                         ; C0AED2 m0x0
    stz.w $0A08,x                          ; C0AED4 m0x0
    sta.w $0A48,x                          ; C0AED7 m0x0
    txy                                    ; C0AEDA m0x0
    tax                                    ; C0AEDB m0x0
    lda.l anim_script_table,x              ; C0AEDC m0x0
    tyx                                    ; C0AEE0 m0x0
    sta.b $A0                              ; C0AEE1 m0x0
    lda.w #$00C4                           ; C0AEE3 m0x0
    sta.b $A2                              ; C0AEE6 m0x0
    ldy.w #$0006                           ; C0AEE8 m0x0
    lda.b [$A0],y                          ; C0AEEB m0x0
    beq loc_C0AF01                         ; C0AEED m0x0
    ldy.w #$0004                           ; C0AEEF m0x0
    lda.b [$A0],y                          ; C0AEF2 m0x0
    bne loc_C0AEF9                         ; C0AEF4 m0x0
    jmp.w loc_C0AF9B                       ; C0AEF6 m0x0

loc_C0AEF9:
    sta.w $0A28,x                          ; C0AEF9 m0x0
    lda.w #$0000                           ; C0AEFC m0x0
    bra loc_C0AF30                         ; C0AEFF m0x0

loc_C0AF01:
    sta.w entity_frame_id,x                ; C0AF01 m0x0

loc_C0AF04:
    rtl                                    ; C0AF04 m0x0

loc_C0AF05:
    txy                                    ; C0AF05 m0x0
    tax                                    ; C0AF06 m0x0
    lda.l anim_script_table,x              ; C0AF07 m0x0
    tyx                                    ; C0AF0B m0x0
    sta.b $A0                              ; C0AF0C m0x0
    lda.w #$00C4                           ; C0AF0E m0x0
    sta.b $A2                              ; C0AF11 m0x0
    ldy.w #$0006                           ; C0AF13 m0x0
    lda.b [$A0],y                          ; C0AF16 m0x0
    beq loc_C0AF01                         ; C0AF18 m0x0
    ldy.w #$0004                           ; C0AF1A m0x0
    lda.b [$A0],y                          ; C0AF1D m0x0
    bne loc_C0AF24                         ; C0AF1F m0x0
    jmp.w loc_C0AF9B                       ; C0AF21 m0x0

loc_C0AF24:
    dec.w $0A28,x                          ; C0AF24 m0x0
    bpl loc_C0AF44                         ; C0AF27 m0x0
    lda.w $0A08,x                          ; C0AF29 m0x0
    clc                                    ; C0AF2C m0x0
    adc.w #$0008                           ; C0AF2D m0x0

loc_C0AF30:
    sta.w $0A08,x                          ; C0AF30 m0x0
    lda.w $0A08,x                          ; C0AF33 m0x0
    ora.w #$0004                           ; C0AF36 m0x0
    tay                                    ; C0AF39 m0x0
    lda.b [$A0],y                          ; C0AF3A m0x0
    sta.w $0A28,x                          ; C0AF3C m0x0
    lda.w #$0001                           ; C0AF3F m0x0
    sta.b $52                              ; C0AF42 m0x0

loc_C0AF44:
    lda.w $0A08,x                          ; C0AF44 m0x0
    and.w #$0FF8                           ; C0AF47 m0x0
    ora.w #$0004                           ; C0AF4A m0x0
    tay                                    ; C0AF4D m0x0
    lda.b [$A0],y                          ; C0AF4E m0x0
    iny                                    ; C0AF50 m0x0
    iny                                    ; C0AF51 m0x0
    sec                                    ; C0AF52 m0x0
    sbc.w #$FFFE                           ; C0AF53 m0x0
    bcs loc_C0AF5D                         ; C0AF56 m0x0
    lda.b [$A0],y                          ; C0AF58 m0x0
    sta.w entity_frame_id,x                ; C0AF5A m0x0

loc_C0AF5D:
    dey                                    ; C0AF5D m0x0
    dey                                    ; C0AF5E m0x0
    dey                                    ; C0AF5F m0x0
    dey                                    ; C0AF60 m0x0
    lda.b [$A0],y                          ; C0AF61 m0x0
    beq loc_C0AF7C                         ; C0AF63 m0x0
    cmp.w #$0002                           ; C0AF65 m0x0
    beq loc_C0AF73                         ; C0AF68 m0x0
    cmp.w #$0001                           ; C0AF6A m0x0
    bne loc_C0AF7C                         ; C0AF6D m0x0
    lda.b $52                              ; C0AF6F m0x0
    beq loc_C0AF7C                         ; C0AF71 m0x0

loc_C0AF73:
    dey                                    ; C0AF73 m0x0
    dey                                    ; C0AF74 m0x0
    lda.b [$A0],y                          ; C0AF75 m0x0
    sta.b ptr_04                           ; C0AF77 m0x0
    jsr.w anim_callback_dispatch           ; C0AF79 m0x0

loc_C0AF7C:
    lda.w $0A08,x                          ; C0AF7C m0x0
    and.w #$0FF8                           ; C0AF7F m0x0
    ora.w #$0004                           ; C0AF82 m0x0
    tay                                    ; C0AF85 m0x0
    lda.b [$A0],y                          ; C0AF86 m0x0
    sec                                    ; C0AF88 m0x0
    sbc.w #$FFFE                           ; C0AF89 m0x0
    beq loc_C0AF30                         ; C0AF8C m0x0
    bcc loc_C0AF9A                         ; C0AF8E m0x0
    iny                                    ; C0AF90 m0x0
    iny                                    ; C0AF91 m0x0
    lda.b [$A0],y                          ; C0AF92 m0x0
    sta.w entity_anim_id,x                 ; C0AF94 m0x0
    jmp.w anim_update                      ; C0AF97 m0x0

loc_C0AF9A:
    rtl                                    ; C0AF9A m0x0

loc_C0AF9B:
    lda.w entity_anim_rate,x               ; C0AF9B m0x0
    cmp.w #$0100                           ; C0AF9E m0x0
    bcc loc_C0AFA6                         ; C0AFA1 m0x0
    lda.w #$0100                           ; C0AFA3 m0x0

loc_C0AFA6:
    clc                                    ; C0AFA6 m0x0
    adc.w $0A28,x                          ; C0AFA7 m0x0
    sta.b $18                              ; C0AFAA m0x0
    and.w #$00FF                           ; C0AFAC m0x0
    sta.w $0A28,x                          ; C0AFAF m0x0
    lda.w $0A08,x                          ; C0AFB2 m0x0
    sta.b $52                              ; C0AFB5 m0x0
    lda.b $19                              ; C0AFB7 m0x0
    and.w #$00FF                           ; C0AFB9 m0x0
    clc                                    ; C0AFBC m0x0
    adc.w $0A08,x                          ; C0AFBD m0x0
    sta.w $0A08,x                          ; C0AFC0 m0x0
    sec                                    ; C0AFC3 m0x0
    sbc.b $52                              ; C0AFC4 m0x0
    sta.b $52                              ; C0AFC6 m0x0
    lda.w $0A08,x                          ; C0AFC8 m0x0
    asl                                    ; C0AFCB m0x0
    asl                                    ; C0AFCC m0x0
    asl                                    ; C0AFCD m0x0
    and.w #$0FF8                           ; C0AFCE m0x0
    ora.w #$0002                           ; C0AFD1 m0x0
    tay                                    ; C0AFD4 m0x0
    lda.b [$A0],y                          ; C0AFD5 m0x0
    beq loc_C0AFF0                         ; C0AFD7 m0x0
    cmp.w #$0002                           ; C0AFD9 m0x0
    beq loc_C0AFE7                         ; C0AFDC m0x0
    cmp.w #$0001                           ; C0AFDE m0x0
    bne loc_C0AFF0                         ; C0AFE1 m0x0
    lda.b $52                              ; C0AFE3 m0x0
    beq loc_C0AFF0                         ; C0AFE5 m0x0

loc_C0AFE7:
    dey                                    ; C0AFE7 m0x0
    dey                                    ; C0AFE8 m0x0
    lda.b [$A0],y                          ; C0AFE9 m0x0
    sta.b ptr_04                           ; C0AFEB m0x0
    jsr.w anim_callback_dispatch           ; C0AFED m0x0

loc_C0AFF0:
    lda.w $0A08,x                          ; C0AFF0 m0x0
    asl                                    ; C0AFF3 m0x0
    asl                                    ; C0AFF4 m0x0
    asl                                    ; C0AFF5 m0x0
    and.w #$0FF8                           ; C0AFF6 m0x0
    ora.w #$0004                           ; C0AFF9 m0x0
    tay                                    ; C0AFFC m0x0
    lda.b [$A0],y                          ; C0AFFD m0x0
    sec                                    ; C0AFFF m0x0
    sbc.w #$FFFE                           ; C0B000 m0x0
    beq loc_C0B011                         ; C0B003 m0x0
    bcc loc_C0B01A                         ; C0B005 m0x0
    iny                                    ; C0B007 m0x0
    iny                                    ; C0B008 m0x0
    lda.b [$A0],y                          ; C0B009 m0x0
    sta.w entity_anim_id,x                 ; C0B00B m0x0
    jmp.w anim_update                      ; C0B00E m0x0

loc_C0B011:
    ldy.w #$0004                           ; C0B011 m0x0
    stz.w $0A28,x                          ; C0B014 m0x0
    stz.w $0A08,x                          ; C0B017 m0x0

loc_C0B01A:
    iny                                    ; C0B01A m0x0
    iny                                    ; C0B01B m0x0
    lda.b [$A0],y                          ; C0B01C m0x0
    sta.w entity_frame_id,x                ; C0B01E m0x0
    rtl                                    ; C0B021 m0x0

anim_callback_dispatch:
    jmp.w ($0004)                          ; C0B022 m0x0

anim_cb_sfx_0704:
    lda.b game_mode                        ; C0B025 m0x0
    cmp.w #$0002                           ; C0B027 m0x0
    bne loc_C0B039                         ; C0B02A m0x0
    lda.w entity_x,x                       ; C0B02C m0x0
    cmp.w #$0748                           ; C0B02F m0x0
    bcc loc_C0B048                         ; C0B032 m0x0
    cmp.w #$07B0                           ; C0B034 m0x0
    bcc loc_C0B04D                         ; C0B037 m0x0

loc_C0B039:
    and.w #$FFFF                           ; C0B039 m0x0
    bne loc_C0B048                         ; C0B03C m0x0
    lda.w entity_x,x                       ; C0B03E m0x0
    bmi loc_C0B04D                         ; C0B041 m0x0
    cmp.w #$0090                           ; C0B043 m0x0
    bcc loc_C0B04D                         ; C0B046 m0x0

loc_C0B048:
    lda.w #$0704                           ; C0B048 m0x0
    bra play_sound_effect                  ; C0B04B m0x0

loc_C0B04D:
    lda.w #$0705                           ; C0B04D m0x0
    bra play_sound_effect                  ; C0B050 m0x0

anim_cb_sfx_0506:
    lda.w #$0506                           ; C0B052 m0x0
    bra play_sound_effect                  ; C0B055 m0x0

anim_cb_sfx_0606:
    lda.w #$0606                           ; C0B057 m0x0
    bra play_sound_effect                  ; C0B05A m0x0

anim_cb_sfx_0602:
    lda.w #$0602                           ; C0B05C m0x0
    bra play_sound_effect                  ; C0B05F m0x0

anim_cb_sfx_0507:
    lda.w #$0507                           ; C0B061 m0x0
    bra play_sound_effect                  ; C0B064 m0x0

anim_cb_sfx_050C:
    lda.w #$050C                           ; C0B066 m0x0
    bra play_sound_effect                  ; C0B069 m0x0

anim_cb_sfx_0508:
    lda.w #$0508                           ; C0B06B m0x0
    bra play_sound_effect                  ; C0B06E m0x0

anim_cb_sfx_0709:
    lda.w #$0709                           ; C0B070 m0x0
    bra play_sound_effect                  ; C0B073 m0x0

play_footstep_sound:
    lda.b walk_cycle_parity                ; C0B075 m0x0
    beq loc_C0B084                         ; C0B077 m0x0
    lda.w #$050A                           ; C0B079 m0x0
    jsr.w play_sound_effect                ; C0B07C m0x0
    lda.w #$0710                           ; C0B07F m0x0
    bra play_sound_effect                  ; C0B082 m0x0

loc_C0B084:
    lda.w #$0712                           ; C0B084 m0x0
    jsr.w play_sound_effect                ; C0B087 m0x0
    lda.w #$050B                           ; C0B08A m0x0
    bra play_sound_effect                  ; C0B08D m0x0

anim_cb_sfx_060E:
    lda.b game_mode                        ; C0B08F m0x0
    cmp.w #$0002                           ; C0B091 m0x0
    bne loc_C0B0A5                         ; C0B094 m0x0
    lda.w entity_x,x                       ; C0B096 m0x0
    cmp.w #$0748                           ; C0B099 m0x0
    bcc loc_C0B0B2                         ; C0B09C m0x0
    cmp.w #$07B0                           ; C0B09E m0x0
    bcc loc_C0B0B7                         ; C0B0A1 m0x0
    bra loc_C0B0B2                         ; C0B0A3 m0x0

loc_C0B0A5:
    and.w #$FFFF                           ; C0B0A5 m0x0
    bne loc_C0B0B2                         ; C0B0A8 m0x0
    lda.w entity_x,x                       ; C0B0AA m0x0
    cmp.w #$0090                           ; C0B0AD m0x0
    bcc loc_C0B0B7                         ; C0B0B0 m0x0

loc_C0B0B2:
    lda.w #$060E                           ; C0B0B2 m0x0
    bra play_sound_effect                  ; C0B0B5 m0x0

loc_C0B0B7:
    lda.w #$060D                           ; C0B0B7 m0x0
    bra play_sound_effect                  ; C0B0BA m0x0

play_zone_transition_sound:
    lda.w #$050F                           ; C0B0BC m0x0
    jsr.w play_sound_effect                ; C0B0BF m0x0
    lda.w #$0611                           ; C0B0C2 m0x0

play_sound_effect:
    phx                                    ; C0B0C5 m0x0
    phy                                    ; C0B0C6 m0x0
    jsl.l $810000+(sfx_command_dispatch&$FFFF)   ; C0B0C7 m0x0
    ply                                    ; C0B0CB m0x0
    plx                                    ; C0B0CC m0x0
    rts                                    ; C0B0CD m0x0

anim_cb_hit_player:
    lda.w entity_hitstun_timer             ; C0B0CE m0x0
    bne loc_C0B0EC                         ; C0B0D1 m0x0
    ldy.w #$0000                           ; C0B0D3 m0x0
    bit.w entity_flags,x                   ; C0B0D6 m0x0
    bvc loc_C0B0ED                         ; C0B0D9 m0x0
    lda.w entity_x                         ; C0B0DB m0x0
    sec                                    ; C0B0DE m0x0
    sbc.w entity_x,x                       ; C0B0DF m0x0
    bpl loc_C0B0EC                         ; C0B0E2 m0x0
    cmp.w #$FFC0                           ; C0B0E4 m0x0
    bcc loc_C0B0EC                         ; C0B0E7 m0x0
    jmp.w entity_hit_react                 ; C0B0E9 m0x0

loc_C0B0EC:
    rts                                    ; C0B0EC m0x0

loc_C0B0ED:
    lda.w entity_x                         ; C0B0ED m0x0
    sec                                    ; C0B0F0 m0x0
    sbc.w entity_x,x                       ; C0B0F1 m0x0
    bmi loc_C0B0EC                         ; C0B0F4 m0x0
    cmp.w #$0040                           ; C0B0F6 m0x0
    bcs loc_C0B0EC                         ; C0B0F9 m0x0
    jmp.w entity_hit_react                 ; C0B0FB m0x0

anim_cb_hit_enemies:
    lda.w #$0703                           ; C0B0FE m0x0
    jsr.w play_sound_effect                ; C0B101 m0x0
    ldy.w #$0004                           ; C0B104 m0x0
    stx.b ptr_04                           ; C0B107 m0x0
    lda.w entity_x,x                       ; C0B109 m0x0
    sta.b $06                              ; C0B10C m0x0
    bit.w entity_flags,x                   ; C0B10E m0x0
    bvc loc_C0B142                         ; C0B111 m0x0

loc_C0B113:
    cpy.b ptr_04                           ; C0B113 m0x0
    beq loc_C0B13B                         ; C0B115 m0x0
    lda.w entity_hitstun_timer,x           ; C0B117 m0x0
    bne loc_C0B13B                         ; C0B11A m0x0
    lda.w entity_type,y                    ; C0B11C m0x0
    cmp.w #$000E                           ; C0B11F m0x0
    bcc loc_C0B13B                         ; C0B122 m0x0
    cmp.w #$0012                           ; C0B124 m0x0
    bcs loc_C0B13B                         ; C0B127 m0x0
    lda.w entity_x,y                       ; C0B129 m0x0
    sec                                    ; C0B12C m0x0
    sbc.b $06                              ; C0B12D m0x0
    bpl loc_C0B13B                         ; C0B12F m0x0
    cmp.w #$FFB8                           ; C0B131 m0x0
    bcc loc_C0B13B                         ; C0B134 m0x0
    phy                                    ; C0B136 m0x0
    jsr.w entity_hit_react                 ; C0B137 m0x0
    ply                                    ; C0B13A m0x0

loc_C0B13B:
    iny                                    ; C0B13B m0x0
    iny                                    ; C0B13C m0x0
    cpy.b $A6                              ; C0B13D m0x0
    bcc loc_C0B113                         ; C0B13F m0x0
    rts                                    ; C0B141 m0x0

loc_C0B142:
    cpy.b ptr_04                           ; C0B142 m0x0
    beq loc_C0B16A                         ; C0B144 m0x0
    lda.w entity_hitstun_timer,x           ; C0B146 m0x0
    bne loc_C0B16A                         ; C0B149 m0x0
    lda.w entity_type,y                    ; C0B14B m0x0
    cmp.w #$000E                           ; C0B14E m0x0
    bcc loc_C0B16A                         ; C0B151 m0x0
    cmp.w #$0012                           ; C0B153 m0x0
    bcs loc_C0B16A                         ; C0B156 m0x0
    lda.w entity_x,y                       ; C0B158 m0x0
    sec                                    ; C0B15B m0x0
    sbc.b $06                              ; C0B15C m0x0
    bmi loc_C0B16A                         ; C0B15E m0x0
    cmp.w #$0048                           ; C0B160 m0x0
    bcs loc_C0B16A                         ; C0B163 m0x0
    phy                                    ; C0B165 m0x0
    jsr.w entity_hit_react                 ; C0B166 m0x0
    ply                                    ; C0B169 m0x0

loc_C0B16A:
    iny                                    ; C0B16A m0x0
    iny                                    ; C0B16B m0x0
    cpy.b $A6                              ; C0B16C m0x0
    bcc loc_C0B142                         ; C0B16E m0x0
    rts                                    ; C0B170 m0x0

entity_hit_react:
    phx                                    ; C0B171 m0x0
    tyx                                    ; C0B172 m0x0
    lda.w entity_state,x                   ; C0B173 m0x0
    and.w #$FFFC                           ; C0B176 m0x0
    cmp.w #$0010                           ; C0B179 m0x0
    beq loc_C0B1AC                         ; C0B17C m0x0
    cmp.w #$0014                           ; C0B17E m0x0
    beq loc_C0B1AC                         ; C0B181 m0x0
    inc.w $0748,x                          ; C0B183 m0x0
    lda.w $0748,x                          ; C0B186 m0x0
    cmp.w #$0004                           ; C0B189 m0x0
    bne loc_C0B19E                         ; C0B18C m0x0
    stz.w $0748,x                          ; C0B18E m0x0
    lda.w #$0014                           ; C0B191 m0x0
    bit.w entity_flags,x                   ; C0B194 m0x0
    bvs loc_C0B1A9                         ; C0B197 m0x0
    lda.w #$0016                           ; C0B199 m0x0
    bra loc_C0B1A9                         ; C0B19C m0x0

loc_C0B19E:
    lda.w #$0010                           ; C0B19E m0x0
    bit.w entity_flags,x                   ; C0B1A1 m0x0
    bvs loc_C0B1A9                         ; C0B1A4 m0x0
    lda.w #$0012                           ; C0B1A6 m0x0

loc_C0B1A9:
    jsr.w set_entity_state                 ; C0B1A9 m0x0

loc_C0B1AC:
    plx                                    ; C0B1AC m0x0
    rts                                    ; C0B1AD m0x0

anim_cb_reset_state:
    lda.w #$0000                           ; C0B1AE m0x0

set_entity_state:
    sta.b $18                              ; C0B1B1 m0x0
    jsr.w entity_ground_y_lookup           ; C0B1B3 m0x0
    txy                                    ; C0B1B6 m0x0
    lda.w entity_flags,y                   ; C0B1B7 m0x0
    cmp.w #$4000                           ; C0B1BA m0x0
    rol                                    ; C0B1BD m0x0
    asl                                    ; C0B1BE m0x0
    and.w #$0002                           ; C0B1BF m0x0
    ora.w $0BAE                            ; C0B1C2 m0x0
    and.w #$000E                           ; C0B1C5 m0x0
    asl                                    ; C0B1C8 m0x0
    tax                                    ; C0B1C9 m0x0
    lda.w entity_flags,y                   ; C0B1CA m0x0
    and.w #$BFFF                           ; C0B1CD m0x0
    ora.l $800000+(facing_flag_table&$FFFF),x   ; C0B1D0 m0x0
    sta.w entity_flags,y                   ; C0B1D4 m0x0
    lda.b $18                              ; C0B1D7 m0x0
    ora.l $800000+(facing_state_bits_table&$FFFF),x   ; C0B1D9 m0x0
    sta.w entity_state,y                   ; C0B1DD m0x0
    tyx                                    ; C0B1E0 m0x0
    stz.w $0A08,x                          ; C0B1E1 m0x0
    stz.w $0A28,x                          ; C0B1E4 m0x0
    ldx.w entity_type,y                    ; C0B1E7 m0x0
    lda.w entity_state,y                   ; C0B1EA m0x0
    adc.w $0BAC                            ; C0B1ED m0x0
    adc.l $800000+(entity_state_anim_table&$FFFF),x   ; C0B1F0 m0x0
    tax                                    ; C0B1F4 m0x0
    lda.l $800000+(entity_state_anim_table&$FFFF),x   ; C0B1F5 m0x0
    sta.w entity_anim_id,y                 ; C0B1F9 m0x0
    tyx                                    ; C0B1FC m0x0
    rts                                    ; C0B1FD m0x0

entity_clear_anim_unused:
    stz.w $07A8,x                          ; C0B1FE m0x0
    stz.w $0808,x                          ; C0B201 m0x0
    stz.w $0A48,x                          ; C0B204 m0x0
    rts                                    ; C0B207 m0x0

vram_stream_desc_table:
    incbin "../data/01.bin":$3208..$320A      ; 2 bytes

vram_stream_desc_addr:
    incbin "../data/01.bin":$320A..$320C      ; 2 bytes

vram_stream_desc_bank:
    incbin "../data/01.bin":$320C..$320E      ; 2 bytes

vram_stream_desc_payload:
    incbin "../data/01.bin":$320E..$324C      ; 62 bytes

stream_zone_index_table:
    incbin "../data/01.bin":$324C..$326C      ; 32 bytes

particle_spawn_list_x:
    incbin "../data/01.bin":$326C..$326E      ; 2 bytes

particle_spawn_list_y:
    incbin "../data/01.bin":$326E..$32F6      ; 136 bytes

ground_y_lookup_threshold:
    incbin "../data/01.bin":$32F6..$32F8      ; 2 bytes

ground_y_lookup_default:
    incbin "../data/01.bin":$32F8..$32FA      ; 2 bytes

entity_state_ground_y_table:
    incbin "../data/01.bin":$32FA..$34A4      ; 426 bytes

entity_init_table:
    incbin "../data/01.bin":$34A4..$34A6      ; 2 bytes

entity_init_state:
    incbin "../data/01.bin":$34A6..$34A8      ; 2 bytes

entity_init_unk_07a8:
    incbin "../data/01.bin":$34A8..$34AA      ; 2 bytes

entity_init_x:
    incbin "../data/01.bin":$34AA..$34AC      ; 2 bytes

entity_init_y:
    incbin "../data/01.bin":$34AC..$34AE      ; 2 bytes

entity_init_unk_08e8:
    incbin "../data/01.bin":$34AE..$34B0      ; 2 bytes

entity_init_flags:
    incbin "../data/01.bin":$34B0..$34B2      ; 2 bytes

entity_init_parent:
    incbin "../data/01.bin":$34B2..$34B4      ; 2 bytes

entity_init_hitstun:
    incbin "../data/01.bin":$34B4..$3652      ; 414 bytes

facing_flag_table:
    incbin "../data/01.bin":$3652..$3654      ; 2 bytes

facing_state_bits_table:
    incbin "../data/01.bin":$3654..$366A      ; 22 bytes

anim_rate_fn_table:
    incbin "../data/01.bin":$366A..$36DC      ; 114 bytes

entity_state_velocity_table:
    incbin "../data/01.bin":$36DC..$37AE      ; 210 bytes

entity_state_anim_table:
    incbin "../data/01.bin":$37AE..$3B81      ; 979 bytes

loc_C0BB81:
    stz.w $0F43                            ; C0BB81 m0x0
    stz.w $0F45                            ; C0BB84 m0x0
    stz.w $0F47                            ; C0BB87 m0x0
    stz.w $0F49                            ; C0BB8A m0x0
    stz.w $0F4B                            ; C0BB8D m0x0
    stz.w $0F4D                            ; C0BB90 m0x0
    stz.w $0F4F                            ; C0BB93 m0x0
    stz.w $0F51                            ; C0BB96 m0x0
    stz.w $0F53                            ; C0BB99 m0x0
    stz.w $0F55                            ; C0BB9C m0x0
    stz.w $0F57                            ; C0BB9F m0x0
    stz.w $0F59                            ; C0BBA2 m0x0
    lda.w #$0F41                           ; C0BBA5 m0x0
    sta.w $0F41                            ; C0BBA8 m0x0
    stz.b $E3                              ; C0BBAB m0x0
    lda.w #$0002                           ; C0BBAD m0x0
    sta.l $7F0F86                          ; C0BBB0 m0x0
    lda.w #$0008                           ; C0BBB4 m0x0
    sta.l $7F0F88                          ; C0BBB7 m0x0
    lda.w #$0040                           ; C0BBBB m0x0
    sta.l $7F0F8A                          ; C0BBBE m0x0
    lda.w #$0070                           ; C0BBC2 m0x0
    sta.l $7F0F8C                          ; C0BBC5 m0x0
    lda.w #$0070                           ; C0BBC9 m0x0
    sta.l $7F0F8E                          ; C0BBCC m0x0
    lda.w #$0000                           ; C0BBD0 m0x0
    sta.w MDMAEN                           ; C0BBD3 m0x0
    sep.b #$30                             ; C0BBD6 m0x0
    lda.b #$28                             ; C0BBD8 m1x1
    sta.l $7F0F90                          ; C0BBDA m1x1
    lda.b #$00                             ; C0BBDE m1x1
    sta.l $7F1195                          ; C0BBE0 m1x1
    lda.b #$80                             ; C0BBE4 m1x1
    sta.w INIDISP                          ; C0BBE6 m1x1
    sta.w VMAIN                            ; C0BBE9 m1x1
    lda.b #$03                             ; C0BBEC m1x1
    sta.w BGMODE                           ; C0BBEE m1x1
    lda.b #$60                             ; C0BBF1 m1x1
    sta.w BG1SC                            ; C0BBF3 m1x1
    stz.w BG12NBA                          ; C0BBF6 m1x1
    stz.w BG1HOFS                          ; C0BBF9 m1x1
    stz.w BG1HOFS                          ; C0BBFC m1x1
    stz.w BG1VOFS                          ; C0BBFF m1x1
    stz.w BG1VOFS                          ; C0BC02 m1x1
    stz.w W12SEL                           ; C0BC05 m1x1
    stz.w WOBJSEL                          ; C0BC08 m1x1
    lda.b #$01                             ; C0BC0B m1x1
    sta.w TM                               ; C0BC0D m1x1
    stz.w TS                               ; C0BC10 m1x1
    stz.w TMW                              ; C0BC13 m1x1
    stz.w TSW                              ; C0BC16 m1x1
    stz.w CGWSEL                           ; C0BC19 m1x1
    stz.w SETINI                           ; C0BC1C m1x1
    stz.w CGADD                            ; C0BC1F m1x1
    rep.b #$10                             ; C0BC22 m1x1
    ldx.w #$0000                           ; C0BC24 m1x0
    ldy.w #$0200                           ; C0BC27 m1x0

loc_C0BC2A:
    lda.l data_C6A36B,x                    ; C0BC2A m1x0
    sta.w CGDATA                           ; C0BC2E m1x0
    sta.l $7F0F91,x                        ; C0BC31 m1x0
    inx                                    ; C0BC35 m1x0
    dey                                    ; C0BC36 m1x0
    bne loc_C0BC2A                         ; C0BC37 m1x0
    rep.b #$30                             ; C0BC39 m1x0
    lda.w #$0600                           ; C0BC3B m0x0
    sta.w VMADDL                           ; C0BC3E m0x0
    ldx.w #$0000                           ; C0BC41 m0x0
    ldy.w #$9CC0                           ; C0BC44 m0x0

loc_C0BC47:
    lda.l data_C6002B,x                    ; C0BC47 m0x0
    sta.w VMDATAL                          ; C0BC4B m0x0
    inx                                    ; C0BC4E m0x0
    inx                                    ; C0BC4F m0x0
    dey                                    ; C0BC50 m0x0
    dey                                    ; C0BC51 m0x0
    bne loc_C0BC47                         ; C0BC52 m0x0
    lda.w #$6000                           ; C0BC54 m0x0
    sta.w VMADDL                           ; C0BC57 m0x0
    ldx.w #$1000                           ; C0BC5A m0x0
    lda.w #$0030                           ; C0BC5D m0x0

loc_C0BC60:
    sta.w VMDATAL                          ; C0BC60 m0x0
    dex                                    ; C0BC63 m0x0
    bne loc_C0BC60                         ; C0BC64 m0x0
    ldx.w #$0000                           ; C0BC66 m0x0
    lda.w #$6500                           ; C0BC69 m0x0
    sta.w VMADDL                           ; C0BC6C m0x0
    ldy.w #$0340                           ; C0BC6F m0x0

loc_C0BC72:
    lda.l data_C69CEB,x                    ; C0BC72 m0x0
    clc                                    ; C0BC76 m0x0
    adc.w #$0030                           ; C0BC77 m0x0
    inx                                    ; C0BC7A m0x0
    inx                                    ; C0BC7B m0x0
    sta.w VMDATAL                          ; C0BC7C m0x0
    dey                                    ; C0BC7F m0x0
    dey                                    ; C0BC80 m0x0
    bne loc_C0BC72                         ; C0BC81 m0x0
    ldx.w #$0000                           ; C0BC83 m0x0
    lda.w #$6960                           ; C0BC86 m0x0
    sta.w VMADDL                           ; C0BC89 m0x0
    ldy.w #$01C0                           ; C0BC8C m0x0

loc_C0BC8F:
    lda.l data_C6A02B,x                    ; C0BC8F m0x0
    adc.w #$0030                           ; C0BC93 m0x0
    inx                                    ; C0BC96 m0x0
    inx                                    ; C0BC97 m0x0
    sta.w VMDATAL                          ; C0BC98 m0x0
    dey                                    ; C0BC9B m0x0
    dey                                    ; C0BC9C m0x0
    bne loc_C0BC8F                         ; C0BC9D m0x0
    ldx.w #$0000                           ; C0BC9F m0x0
    lda.w #$61C0                           ; C0BCA2 m0x0
    sta.w VMADDL                           ; C0BCA5 m0x0
    ldy.w #$0080                           ; C0BCA8 m0x0

loc_C0BCAB:
    lda.l data_C6A2EB,x                    ; C0BCAB m0x0
    adc.w #$0030                           ; C0BCAF m0x0
    inx                                    ; C0BCB2 m0x0
    inx                                    ; C0BCB3 m0x0
    sta.w VMDATAL                          ; C0BCB4 m0x0
    dey                                    ; C0BCB7 m0x0
    dey                                    ; C0BCB8 m0x0
    bne loc_C0BCAB                         ; C0BCB9 m0x0
    ldx.w #$0000                           ; C0BCBB m0x0
    lda.w #$6DA0                           ; C0BCBE m0x0
    sta.w VMADDL                           ; C0BCC1 m0x0
    ldy.w #$0100                           ; C0BCC4 m0x0

loc_C0BCC7:
    lda.l data_C6A1EB,x                    ; C0BCC7 m0x0
    adc.w #$0030                           ; C0BCCB m0x0
    inx                                    ; C0BCCE m0x0
    inx                                    ; C0BCCF m0x0
    sta.w VMDATAL                          ; C0BCD0 m0x0
    dey                                    ; C0BCD3 m0x0
    dey                                    ; C0BCD4 m0x0
    bne loc_C0BCC7                         ; C0BCD5 m0x0
    jsr.w vram_upload_shared_tileset_c5    ; C0BCD7 m0x0
    sep.b #$30                             ; C0BCDA m0x0
    lda.b #$80                             ; C0BCDC m1x1
    sta.w A1B1                             ; C0BCDE m1x1
    lda.b #$7F                             ; C0BCE1 m1x1
    sta.w DASB1                            ; C0BCE3 m1x1
    lda.b #$E0                             ; C0BCE6 m1x1
    sta.w NTRL1                            ; C0BCE8 m1x1
    rep.b #$20                             ; C0BCEB m1x1
    lda.w #$0048                           ; C0BCED m0x1
    sta.w DMAP1                            ; C0BCF0 m0x1
    lda.w #$C00D                           ; C0BCF3 m0x1
    sta.w A1TL1                            ; C0BCF6 m0x1
    sep.b #$20                             ; C0BCF9 m0x1
    lda.b #$02                             ; C0BCFB m1x1
    stz.w HDMAEN                           ; C0BCFD m1x1
    stz.w INIDISP                          ; C0BD00 m1x1
    rep.b #$20                             ; C0BD03 m1x1
    sep.b #$20                             ; C0BD05 m0x1
    lda.w TIMEUP                           ; C0BD07 m1x1
    lda.b #$81                             ; C0BD0A m1x1
    sta.b nmitimen_shadow                  ; C0BD0C m1x1
    lda.b #$80                             ; C0BD0E m1x1
    sta.w OAMADDH                          ; C0BD10 m1x1
    lda.b #$01                             ; C0BD13 m1x1
    sta.w MEMSEL                           ; C0BD15 m1x1
    rep.b #$20                             ; C0BD18 m1x1
    lda.w #$BD20                           ; C0BD1A m0x1
    jmp.w loc_C0A4E9                       ; C0BD1D m0x1

nmi_handler_title_fade:
    rep.b #$10                             ; C0BD20 m0x0
    ldx.w #$01FF                           ; C0BD22 m0x0
    txs                                    ; C0BD25 m0x0
    ldx.w #$0000                           ; C0BD26 m0x0
    stx.w OAMADDL                          ; C0BD29 m0x0
    sep.b #$10                             ; C0BD2C m0x0
    rep.b #$20                             ; C0BD2E m0x1
    lda.w #$2200                           ; C0BD30 m0x1
    sta.w DMAP2                            ; C0BD33 m0x1
    lda.w #$0F91                           ; C0BD36 m0x1
    sta.w A1TL2                            ; C0BD39 m0x1
    sep.b #$20                             ; C0BD3C m0x1
    stz.w CGADD                            ; C0BD3E m1x1
    lda.b #$7F                             ; C0BD41 m1x1
    sta.w A1B2                             ; C0BD43 m1x1
    stz.w DASL2                            ; C0BD46 m1x1
    lda.b #$02                             ; C0BD49 m1x1
    sta.w DASH2                            ; C0BD4B m1x1
    lda.b #$04                             ; C0BD4E m1x1
    ora.b dma_pending_mask                 ; C0BD50 m1x1
    sta.w MDMAEN                           ; C0BD52 m1x1
    lda.w $0B8A                            ; C0BD55 m1x1
    beq loc_C0BD67                         ; C0BD58 m1x1
    cmp.w $0B8B                            ; C0BD5A m1x1
    beq loc_C0BD67                         ; C0BD5D m1x1
    sta.w $0B8B                            ; C0BD5F m1x1
    lda.b #$11                             ; C0BD62 m1x1
    sta.w TM                               ; C0BD64 m1x1

loc_C0BD67:
    sep.b #$20                             ; C0BD67 m1x1
    stz.w NMITIMEN                         ; C0BD69 m1x1
    rep.b #$30                             ; C0BD6C m1x1
    jsr.w read_joypads                     ; C0BD6E m0x0
    lda.b $8A                              ; C0BD71 m0x0
    ora.b $8E                              ; C0BD73 m0x0
    and.w #$1000                           ; C0BD75 m0x0
    beq loc_C0BD7D                         ; C0BD78 m0x0
    jmp.w loc_C0BEA0                       ; C0BD7A m0x0

loc_C0BD7D:
    sep.b #$30                             ; C0BD7D m0x0
    lda.w $0F43                            ; C0BD7F m1x1
    bne loc_C0BD92                         ; C0BD82 m1x1
    lda.w $0F4D                            ; C0BD84 m1x1
    inc                                    ; C0BD87 m1x1
    sta.w $0F4D                            ; C0BD88 m1x1
    cmp.b #$D7                             ; C0BD8B m1x1
    beq loc_C0BDB0                         ; C0BD8D m1x1
    jmp.w loc_C0C008                       ; C0BD8F m1x1

loc_C0BD92:
    dec                                    ; C0BD92 m1x1
    bne loc_C0BDBC                         ; C0BD93 m1x1

loc_C0BD95:
    rep.b #$20                             ; C0BD95 m1x1
    lda.w $0F44                            ; C0BD97 m0x1
    clc                                    ; C0BD9A m0x1
    adc.w #$0080                           ; C0BD9B m0x1
    sta.w $0F44                            ; C0BD9E m0x1
    sep.b #$20                             ; C0BDA1 m0x1
    lda.w $0F45                            ; C0BDA3 m1x1
    sta.w INIDISP                          ; C0BDA6 m1x1
    cmp.b #$0F                             ; C0BDA9 m1x1
    beq loc_C0BDB0                         ; C0BDAB m1x1
    jmp.w loc_C0C008                       ; C0BDAD m1x1

loc_C0BDB0:
    sep.b #$20                             ; C0BDB0 m1x1
    lda.w $0F43                            ; C0BDB2 m1x1
    inc                                    ; C0BDB5 m1x1
    sta.w $0F43                            ; C0BDB6 m1x1
    jmp.w loc_C0C008                       ; C0BDB9 m1x1

loc_C0BDBC:
    dec                                    ; C0BDBC m1x1
    bne loc_C0BDD3                         ; C0BDBD m1x1

loc_C0BDBF:
    lda.w $0F45                            ; C0BDBF m1x1
    sta.w INIDISP                          ; C0BDC2 m1x1
    lda.w $0F46                            ; C0BDC5 m1x1
    inc                                    ; C0BDC8 m1x1
    sta.w $0F46                            ; C0BDC9 m1x1
    cmp.b #$C0                             ; C0BDCC m1x1
    beq loc_C0BDB0                         ; C0BDCE m1x1
    jmp.w loc_C0C008                       ; C0BDD0 m1x1

loc_C0BDD3:
    dec                                    ; C0BDD3 m1x1
    bne loc_C0BDF1                         ; C0BDD4 m1x1

loc_C0BDD6:
    rep.b #$20                             ; C0BDD6 m1x1
    lda.w $0F44                            ; C0BDD8 m0x1
    sec                                    ; C0BDDB m0x1
    sbc.w #$0080                           ; C0BDDC m0x1
    sta.w $0F44                            ; C0BDDF m0x1
    sep.b #$20                             ; C0BDE2 m0x1
    lda.w $0F45                            ; C0BDE4 m1x1
    sta.w INIDISP                          ; C0BDE7 m1x1
    cmp.b #$00                             ; C0BDEA m1x1
    beq loc_C0BDB0                         ; C0BDEC m1x1
    jmp.w loc_C0C008                       ; C0BDEE m1x1

loc_C0BDF1:
    dec                                    ; C0BDF1 m1x1
    bne loc_C0BE07                         ; C0BDF2 m1x1
    rep.b #$20                             ; C0BDF4 m1x1
    lda.w $0F50                            ; C0BDF6 m0x1
    inc                                    ; C0BDF9 m0x1
    sta.w $0F50                            ; C0BDFA m0x1
    cmp.w #$0055                           ; C0BDFD m0x1
    beq loc_C0BDB0                         ; C0BE00 m0x1
    sep.b #$20                             ; C0BE02 m0x1
    jmp.w loc_C0C008                       ; C0BE04 m1x1

loc_C0BE07:
    dec                                    ; C0BE07 m1x1
    bne loc_C0BE14                         ; C0BE08 m1x1
    lda.b #$6C                             ; C0BE0A m1x1
    sta.w BG1SC                            ; C0BE0C m1x1
    stz.w $0F46                            ; C0BE0F m1x1
    bra loc_C0BD95                         ; C0BE12 m1x1

loc_C0BE14:
    dec                                    ; C0BE14 m1x1
    beq loc_C0BDBF                         ; C0BE15 m1x1
    dec                                    ; C0BE17 m1x1
    beq loc_C0BDD6                         ; C0BE18 m1x1
    dec                                    ; C0BE1A m1x1
    bne loc_C0BE2B                         ; C0BE1B m1x1
    lda.w $0F54                            ; C0BE1D m1x1
    inc                                    ; C0BE20 m1x1
    sta.w $0F54                            ; C0BE21 m1x1
    cmp.b #$28                             ; C0BE24 m1x1
    beq loc_C0BDB0                         ; C0BE26 m1x1
    jmp.w loc_C0C008                       ; C0BE28 m1x1

loc_C0BE2B:
    dec                                    ; C0BE2B m1x1
    bne loc_C0BE39                         ; C0BE2C m1x1
    lda.b #$68                             ; C0BE2E m1x1
    sta.w BG1SC                            ; C0BE30 m1x1
    stz.w $0F46                            ; C0BE33 m1x1
    jmp.w loc_C0BD95                       ; C0BE36 m1x1

loc_C0BE39:
    dec                                    ; C0BE39 m1x1
    bne loc_C0BE58                         ; C0BE3A m1x1
    lda.w $0F45                            ; C0BE3C m1x1
    sta.w INIDISP                          ; C0BE3F m1x1
    rep.b #$20                             ; C0BE42 m1x1
    lda.w $0F58                            ; C0BE44 m0x1
    inc                                    ; C0BE47 m0x1
    sta.w $0F58                            ; C0BE48 m0x1
    cmp.w #$0564                           ; C0BE4B m0x1
    sep.b #$20                             ; C0BE4E m0x1
    beq loc_C0BE55                         ; C0BE50 m1x1
    jmp.w loc_C0C008                       ; C0BE52 m1x1

loc_C0BE55:
    jmp.w loc_C0BDB0                       ; C0BE55 m1x1

loc_C0BE58:
    sta.w $0F4B                            ; C0BE58 m1x1
    sep.b #$20                             ; C0BE5B m1x1
    lda.w $0F45                            ; C0BE5D m1x1
    sta.w INIDISP                          ; C0BE60 m1x1
    lda.l $7F1195                          ; C0BE63 m1x1
    beq loc_C0BEC0                         ; C0BE67 m1x1
    cmp.b #$01                             ; C0BE69 m1x1
    bne loc_C0BE70                         ; C0BE6B m1x1
    jmp.w loc_C0BF57                       ; C0BE6D m1x1

loc_C0BE70:
    sep.b #$20                             ; C0BE70 m1x1
    lda.b #$01                             ; C0BE72 m1x1
    sta.w $0B8A                            ; C0BE74 m1x1
    rep.b #$20                             ; C0BE77 m1x1
    lda.w $0F55                            ; C0BE79 m0x1
    cmp.w #$0F00                           ; C0BE7C m0x1
    beq loc_C0BE9B                         ; C0BE7F m0x1
    inc                                    ; C0BE81 m0x1
    sta.w $0F55                            ; C0BE82 m0x1
    cmp.w #$0F00                           ; C0BE85 m0x1
    bne loc_C0BE91                         ; C0BE88 m0x1
    sep.b #$20                             ; C0BE8A m0x1
    lda.b #$0C                             ; C0BE8C m1x1
    sta.w $0F43                            ; C0BE8E m1x1

loc_C0BE91:
    rep.b #$30                             ; C0BE91 m1x1
    jsr.w mode1_reset_particles_and_oam    ; C0BE93 m0x0
    sep.b #$20                             ; C0BE96 m0x0
    jmp.w loc_C0C008                       ; C0BE98 m1x0

loc_C0BE9B:
    sep.b #$20                             ; C0BE9B m0x1
    lda.w $0F4B                            ; C0BE9D m1x1

loc_C0BEA0:
    sep.b #$20                             ; C0BEA0 m0x0
    lda.b #$01                             ; C0BEA2 m1x0
    sta.w NMITIMEN                         ; C0BEA4 m1x0
    lda.b #$80                             ; C0BEA7 m1x0
    sta.w INIDISP                          ; C0BEA9 m1x0
    rep.b #$30                             ; C0BEAC m1x0
    stz.b $8C                              ; C0BEAE m0x0
    stz.b $90                              ; C0BEB0 m0x0
    stz.b dma_pending_mask                 ; C0BEB2 m0x0
    jmp.w loc_C08042                       ; C0BEB4 m0x0

unused_title_fade_jmp_stub:
    incbin "../data/01.bin":$3EB7..$3EC0      ; 9 bytes

loc_C0BEC0:
    sep.b #$20                             ; C0BEC0 m1x1
    lda.b #$68                             ; C0BEC2 m1x1
    sta.w BG1SC                            ; C0BEC4 m1x1
    rep.b #$30                             ; C0BEC7 m1x1
    sep.b #$20                             ; C0BEC9 m0x0
    lda.b #$FF                             ; C0BECB m1x0
    sta.l $7F1193                          ; C0BECD m1x0
    lda.b #$7F                             ; C0BED1 m1x0
    sta.l $7F1194                          ; C0BED3 m1x0
    ldx.w #$0000                           ; C0BED7 m1x0

loc_C0BEDA:
    lda.l $7F0F91,x                        ; C0BEDA m1x0
    and.b #$1F                             ; C0BEDE m1x0
    cmp.b #$1F                             ; C0BEE0 m1x0
    beq loc_C0BEE5                         ; C0BEE2 m1x0
    inc                                    ; C0BEE4 m1x0

loc_C0BEE5:
    cmp.b #$1F                             ; C0BEE5 m1x0
    beq loc_C0BEEA                         ; C0BEE7 m1x0
    inc                                    ; C0BEE9 m1x0

loc_C0BEEA:
    sta.l $7F1191                          ; C0BEEA m1x0
    lda.l $7F0F92,x                        ; C0BEEE m1x0
    and.b #$7C                             ; C0BEF2 m1x0
    cmp.b #$7C                             ; C0BEF4 m1x0
    beq loc_C0BEFB                         ; C0BEF6 m1x0
    clc                                    ; C0BEF8 m1x0
    adc.b #$04                             ; C0BEF9 m1x0

loc_C0BEFB:
    cmp.b #$7C                             ; C0BEFB m1x0
    beq loc_C0BF02                         ; C0BEFD m1x0
    clc                                    ; C0BEFF m1x0
    adc.b #$04                             ; C0BF00 m1x0

loc_C0BF02:
    sta.l $7F1192                          ; C0BF02 m1x0
    rep.b #$20                             ; C0BF06 m1x0
    lda.l $7F0F91,x                        ; C0BF08 m0x0
    and.w #$03E0                           ; C0BF0C m0x0
    cmp.w #$03E0                           ; C0BF0F m0x0
    beq loc_C0BF18                         ; C0BF12 m0x0
    clc                                    ; C0BF14 m0x0
    adc.w #$0020                           ; C0BF15 m0x0

loc_C0BF18:
    cmp.w #$03E0                           ; C0BF18 m0x0
    beq loc_C0BF21                         ; C0BF1B m0x0
    clc                                    ; C0BF1D m0x0
    adc.w #$0020                           ; C0BF1E m0x0

loc_C0BF21:
    ora.l $7F1191                          ; C0BF21 m0x0
    sta.l $7F0F91,x                        ; C0BF25 m0x0
    and.l $7F1193                          ; C0BF29 m0x0
    sta.l $7F1193                          ; C0BF2D m0x0
    sep.b #$20                             ; C0BF31 m0x0
    inx                                    ; C0BF33 m1x0
    inx                                    ; C0BF34 m1x0
    cpx.w #$0200                           ; C0BF35 m1x0
    bne loc_C0BEDA                         ; C0BF38 m1x0
    rep.b #$20                             ; C0BF3A m1x0
    lda.l $7F1193                          ; C0BF3C m0x0
    cmp.w #$7FFF                           ; C0BF40 m0x0
    bne loc_C0BF52                         ; C0BF43 m0x0
    sep.b #$20                             ; C0BF45 m0x0
    lda.b #$64                             ; C0BF47 m1x0
    sta.w BG1SC                            ; C0BF49 m1x0
    lda.b #$01                             ; C0BF4C m1x0
    sta.l $7F1195                          ; C0BF4E m1x0

loc_C0BF52:
    sep.b #$30                             ; C0BF52 m1x0
    jmp.w loc_C0C008                       ; C0BF54 m1x1

loc_C0BF57:
    rep.b #$10                             ; C0BF57 m1x1
    ldx.w #$0000                           ; C0BF59 m1x0
    lda.b #$00                             ; C0BF5C m1x0
    sta.l $7F1193                          ; C0BF5E m1x0
    sta.l $7F1194                          ; C0BF62 m1x0

loc_C0BF66:
    lda.l data_C6A36B,x                    ; C0BF66 m1x0
    and.b #$1F                             ; C0BF6A m1x0
    sta.l $7F1196                          ; C0BF6C m1x0
    lda.l $7F0F91,x                        ; C0BF70 m1x0
    and.b #$1F                             ; C0BF74 m1x0
    cmp.l $7F1196                          ; C0BF76 m1x0
    beq loc_C0BF7D                         ; C0BF7A m1x0
    dec                                    ; C0BF7C m1x0

loc_C0BF7D:
    cmp.l $7F1196                          ; C0BF7D m1x0
    beq loc_C0BF84                         ; C0BF81 m1x0
    dec                                    ; C0BF83 m1x0

loc_C0BF84:
    sta.l $7F1191                          ; C0BF84 m1x0
    lda.l data_C6A36C,x                    ; C0BF88 m1x0
    and.b #$7C                             ; C0BF8C m1x0
    sta.l $7F1196                          ; C0BF8E m1x0
    lda.l $7F0F92,x                        ; C0BF92 m1x0
    and.b #$7C                             ; C0BF96 m1x0
    cmp.l $7F1196                          ; C0BF98 m1x0
    beq loc_C0BFA1                         ; C0BF9C m1x0
    sec                                    ; C0BF9E m1x0
    sbc.b #$04                             ; C0BF9F m1x0

loc_C0BFA1:
    cmp.l $7F1196                          ; C0BFA1 m1x0
    beq loc_C0BFAA                         ; C0BFA5 m1x0
    sec                                    ; C0BFA7 m1x0
    sbc.b #$04                             ; C0BFA8 m1x0

loc_C0BFAA:
    sta.l $7F1192                          ; C0BFAA m1x0
    rep.b #$20                             ; C0BFAE m1x0
    lda.l data_C6A36B,x                    ; C0BFB0 m0x0
    and.w #$03E0                           ; C0BFB4 m0x0
    sta.l $7F1196                          ; C0BFB7 m0x0
    lda.l $7F0F91,x                        ; C0BFBB m0x0
    and.w #$03E0                           ; C0BFBF m0x0
    cmp.l $7F1196                          ; C0BFC2 m0x0
    beq loc_C0BFCC                         ; C0BFC6 m0x0
    sec                                    ; C0BFC8 m0x0
    sbc.w #$0020                           ; C0BFC9 m0x0

loc_C0BFCC:
    cmp.l $7F1196                          ; C0BFCC m0x0
    beq loc_C0BFD6                         ; C0BFD0 m0x0
    sec                                    ; C0BFD2 m0x0
    sbc.w #$0020                           ; C0BFD3 m0x0

loc_C0BFD6:
    ora.l $7F1191                          ; C0BFD6 m0x0
    sta.l $7F0F91,x                        ; C0BFDA m0x0
    eor.l data_C6A36B,x                    ; C0BFDE m0x0
    ora.l $7F1193                          ; C0BFE2 m0x0
    sta.l $7F1193                          ; C0BFE6 m0x0
    sep.b #$20                             ; C0BFEA m0x0
    inx                                    ; C0BFEC m1x0
    inx                                    ; C0BFED m1x0
    cpx.w #$0200                           ; C0BFEE m1x0
    beq loc_C0BFF6                         ; C0BFF1 m1x0
    jmp.w loc_C0BF66                       ; C0BFF3 m1x0

loc_C0BFF6:
    sep.b #$10                             ; C0BFF6 m1x0
    lda.l $7F1193                          ; C0BFF8 m1x1
    ora.l $7F1194                          ; C0BFFC m1x1
    bne loc_C0C008                         ; C0C000 m1x1
    lda.b #$02                             ; C0C002 m1x1
    sta.l $7F1195                          ; C0C004 m1x1

loc_C0C008:
    rep.b #$30                             ; C0C008 m1x1
    jmp.w loc_C0A4F5                       ; C0C00A m0x0
    incbin "../data/hdma/hdma_inidisp_table.bin"                        ; 71 bytes (whole asset)
    incbin "../data/filler/filler_0637.bin"                        ; 16256 bytes (whole asset)
    incbin "../data/filler/header_remnants.bin"                        ; 16 bytes (whole asset)
    incbin "../data/filler/cpu_vectors.bin"                        ; 28 bytes (whole asset)
