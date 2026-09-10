; Project Dream (Rare, 1995) SPC700 sound driver, traced by tools/trace_spc700.py
; Source: DREAM.sfc file 0x20000 (loader, 0x88 bytes -> $04D8) and 0x20088
;         (driver, 0x699 words -> $0560).  Data runs (tables, the sample remap
;         block, unreached bytes) are not inlined: they are incbin ranges into
;         data/04.bin (file 0x020000-0x027FFF, produced by tools/extract.py from
;         your own ROM; ranges are relative to that file, end exclusive).
; Assemble-as-image: `asar` with norom + org writes each block at its SPC address.
norom
arch spc700

; SPC700 I/O registers
!TEST      = $F0
!CONTROL   = $F1
!DSPADDR   = $F2
!DSPDATA   = $F3
!CPUIO0    = $F4
!CPUIO1    = $F5
!CPUIO2    = $F6
!CPUIO3    = $F7
!AUXIO4    = $F8
!AUXIO5    = $F9
!T0TARGET  = $FA
!T1TARGET  = $FB
!T2TARGET  = $FC
!T0OUT     = $FD
!T1OUT     = $FE
!T2OUT     = $FF


; ---- IPL loader (spc_ipl_upload_loader): file 0x20000, SPC $04D8-$055F ----
org $04D8

spc_loader: ; IPL-uploaded loader; entered by the IPL jump to $04D8
    clrp                         ; 04D8
    mov x,#$FF                   ; 04D9
    mov sp,x                     ; 04DB
    inc x                        ; 04DC
    mov !CPUIO0,x                ; 04DD
    inc x                        ; 04DF
    mov $E9,x                    ; 04E0 port_counter
    mov a,#$00                   ; 04E2
    mov $00,a                    ; 04E4 tmp0
    mov $01,#$D0                 ; 04E6 tmp1
loc_04E9:
    mov y,a                      ; 04E9
loc_04EA:
    mov ($00)+y,a                ; 04EA tmp0
    inc y                        ; 04EC
    bne loc_04EA                 ; 04ED
    inc $01                      ; 04EF tmp1
    bne loc_04E9                 ; 04F1
loader_reset_dsp: ; FLG=$FF EDL=0 ESA=$FF, then block loop; driver cmd 7 jumps back here
    mov x,#$FF                   ; 04F3
    mov !DSPADDR,#$6C            ; 04F5 DSP FLG
    mov !DSPDATA,x               ; 04F8
    mov !DSPADDR,#$7D            ; 04FA DSP EDL
    mov !DSPDATA,#$00            ; 04FD
    mov !DSPADDR,#$6D            ; 0500 DSP ESA
    mov !DSPDATA,x               ; 0503
    mov $04B7,x                  ; 0505 esa_value
    mov x,$E9                    ; 0508 port_counter
loader_block_loop: ; handshake: wait port0==counter, movw ya,$F5 = dest address
    cmp x,!CPUIO0                ; 050A
    bne loader_block_loop        ; 050C
    movw ya,!CPUIO1              ; 050E
    mov !CPUIO0,x                ; 0510
    inc x                        ; 0512
    mov $0539,a                  ; 0513 loader_dest (self-modified operand)
    mov $0542,a                  ; 0516 loader_dest2 (self-modified operand)
    mov $053A,y                  ; 0519 loader_dest+1
    mov $0543,y                  ; 051C loader_dest2+1
loc_051F:
    cmp x,!CPUIO0                ; 051F
    bne loc_051F                 ; 0521
    movw ya,!CPUIO1              ; 0523
    mov !CPUIO0,x                ; 0525
    inc x                        ; 0527
    mov $EA,a                    ; 0528 loader_count
    mov $EB,y                    ; 052A
    decw $EA                     ; 052C loader_count
    bmi loader_jump              ; 052E
    mov y,#$00                   ; 0530
loc_0532:
    cmp x,!CPUIO0                ; 0532
    bne loc_0532                 ; 0534
    mov a,!CPUIO1                ; 0536
    mov $0000+y,a                ; 0538 tmp0
    mov a,!CPUIO2                ; 053B
    mov !CPUIO0,x                ; 053D
    inc x                        ; 053F
    inc y                        ; 0540
    mov $0000+y,a                ; 0541 tmp0
    inc y                        ; 0544
    beq loc_054E                 ; 0545
loc_0547:
    decw $EA                     ; 0547 loader_count
    bpl loc_0532                 ; 0549
    jmp loader_block_loop        ; 054B

loc_054E:
    inc $053A                    ; 054E loader_dest+1
    inc $0543                    ; 0551 loader_dest2+1
    bra loc_0547                 ; 0554

loader_jump: ; word count 0: save counter, jmp (dest)
    mov $E9,x                    ; 0556 port_counter
    mov x,#$00                   ; 0558
    jmp ($0539+x)                ; 055A loader_dest (self-modified operand)

data_055D:
    incbin "../data/04.bin":$0085..$0088     ; 3 bytes  SPC $055D-$055F

; ---- main driver (spc_upload_driver, 0x699 words): file 0x20088, SPC $0560-$1291 ----
org $0560
data_0560:
    incbin "../data/04.bin":$0088..$0188     ; 256 bytes  SPC $0560-$065F
start_song: ; cmd 3: song number in cmd_param -> song_table[$1312] -> $E5/$E6
    mov a,$055D                  ; 0660 data_055D; cmd_param
    asl a                        ; 0663
    mov y,a                      ; 0664
    mov a,$1312+y                ; 0665 song_table (word pointers, indexed by cmd 3 param)
    mov $E5,a                    ; 0668 song_ptr
    mov a,$1313+y                ; 066A song_table+1
    mov $E6,a                    ; 066D
    jmp driver_init              ; 066F

driver_entry: ; reached via loader `jmp ($0539+x)` after the 65816 sends addr $0672 with 0 words
    mov $E6,#$13                 ; 0672
    mov $E5,#$00                 ; 0675 song_ptr
driver_init: ; init DSP/channels from song header at ($E5), clear play flag
    call dsp_init                ; 0678
    mov a,#$00                   ; 067B
    mov $1C,a                    ; 067D play_flag
    mov $1D,a                    ; 067F mono_flag
    mov !CONTROL,a               ; 0681
main_loop: ; poll port0 == counter ($E9); else fall to loc_0781 tick handling
    mov a,$E9                    ; 0683 port_counter
    cmp a,!CPUIO0                ; 0685
    beq cmd_receive              ; 0687
    jmp tick_wait                ; 0689

cmd_receive: ; port2 -> cmd_param ($055D), port1 -> cmd; echo counter; counter++
    mov x,!CPUIO2                ; 068C
    mov $055D,x                  ; 068E data_055D; cmd_param
    mov x,!CPUIO1                ; 0691
    mov !CPUIO0,a                ; 0693
    inc a                        ; 0695
    mov $E9,a                    ; 0696 port_counter
    mov a,x                      ; 0698
    cmp a,#$80                   ; 0699
    bpl cmd_dispatch             ; 069B
    jmp play_sfx                 ; 069D

cmd_dispatch: ; cmd >= $80: (cmd & 7) -> jtab_06A7
    and a,#$07                   ; 06A0
    asl a                        ; 06A2
    mov x,a                      ; 06A3
    jmp (cmd_table+x)            ; 06A4


cmd_table: ; 8 port commands
    dw cmd0_set_E8               ; 06A7  [00]
    dw cmd1_set_E7               ; 06A9  [01]
    dw cmd2_set_mono             ; 06AB  [02]
    dw cmd3_fade_and_song        ; 06AD  [03]
    dw cmd4_pitch_offset         ; 06AF  [04]
    dw cmd5_voice5_volume        ; 06B1  [05]
    dw cmd6_play                 ; 06B3  [06]
    dw cmd7_stop_to_loader       ; 06B5  [07]
cmd3_fade_and_song: ; ramp every DSP volume toward 0 (0x7F steps) then start_song
    mov x,#$7F                   ; 06B7
loc_06B9:
    mov a,#$71                   ; 06B9
loc_06BB:
    mov y,a                      ; 06BB
    mov !DSPADDR,y               ; 06BC
    call dsp_step_toward_zero    ; 06BE
    dec y                        ; 06C1
    mov !DSPADDR,y               ; 06C2
    call dsp_step_toward_zero    ; 06C4
    mov a,y                      ; 06C7
    setc                         ; 06C8
    sbc a,#$0F                   ; 06C9
    bpl loc_06BB                 ; 06CB
    mov !DSPADDR,#$0C            ; 06CD DSP MVOLL
    call dsp_step_toward_zero    ; 06D0
    mov !DSPADDR,#$1C            ; 06D3 DSP MVOLR
    call dsp_step_toward_zero    ; 06D6
    mov !DSPADDR,#$2C            ; 06D9 DSP EVOLL
    call dsp_step_toward_zero    ; 06DC
    mov !DSPADDR,#$3C            ; 06DF DSP EVOLR
    call dsp_step_toward_zero    ; 06E2
    dec x                        ; 06E5
    bne loc_06B9                 ; 06E6
    jmp start_song               ; 06E8


dsp_step_toward_zero: ; read DSPDATA, move 2 toward 0, write back
    mov a,!DSPDATA               ; 06EB
    beq loc_06F7                 ; 06ED
    bmi loc_06F5                 ; 06EF
    dec a                        ; 06F1
    dec a                        ; 06F2
    bra loc_06F7                 ; 06F3

loc_06F5:
    inc a                        ; 06F5
    inc a                        ; 06F6
loc_06F7:
    mov !DSPDATA,a               ; 06F7
    ret                          ; 06F9

cmd2_set_mono: ; cmd_param -> mono flag ($1D)
    mov a,$055D                  ; 06FA data_055D; cmd_param
    mov $1D,a                    ; 06FD mono_flag
    jmp tick_wait                ; 06FF

cmd1_set_E7: ; cmd_param -> $E7 (unused elsewhere in traced code)
    mov a,$055D                  ; 0702 data_055D; cmd_param
    mov $E7,a                    ; 0705 var_E7
    jmp tick_wait                ; 0707

cmd0_set_E8: ; cmd_param -> $E8 (unused elsewhere in traced code)
    mov a,$055D                  ; 070A data_055D; cmd_param
    mov $E8,a                    ; 070D var_E8
    jmp tick_wait                ; 070F

cmd5_voice5_volume: ; scale DSP V5 VOL_L/R by cmd_param percent
    mov a,$04B6                  ; 0712 master_percent
    push a                       ; 0715
    push x                       ; 0716
    mov x,#$05                   ; 0717
    mov a,$055D                  ; 0719 data_055D; cmd_param
    mov $04B6,a                  ; 071C master_percent
    mov !DSPADDR,#$50            ; 071F DSP V5_VOL_L
    mov a,!DSPDATA               ; 0722
    call scale_volume            ; 0724
    mov !DSPDATA,a               ; 0727
    inc !DSPADDR                 ; 0729
    mov a,!DSPDATA               ; 072B
    call scale_volume            ; 072D
    mov !DSPDATA,a               ; 0730
    pop x                        ; 0732
    pop a                        ; 0733
    mov $04B6,a                  ; 0734 master_percent
    bra tick_wait                ; 0737

cmd4_pitch_offset: ; sign-extend cmd_param * 8 -> $EC/$ED (applied to SFX voice $0D); clear EON bit 5
    mov a,$055D                  ; 0739 data_055D; cmd_param
    bmi loc_074A                 ; 073C
    clrc                         ; 073E
    mov a,$055D                  ; 073F data_055D; cmd_param
    mov $EC,a                    ; 0742 pitch_offset
    mov a,#$00                   ; 0744
    mov $ED,a                    ; 0746
    bra loc_0756                 ; 0748

loc_074A:
    mov $055D,a                  ; 074A data_055D; cmd_param
    mov a,$055D                  ; 074D data_055D; cmd_param
    mov $EC,a                    ; 0750 pitch_offset
    mov a,#$FF                   ; 0752
    mov $ED,a                    ; 0754
loc_0756:
    movw ya,$EC                  ; 0756 pitch_offset
    addw ya,$EC                  ; 0758 pitch_offset
    addw ya,$EC                  ; 075A pitch_offset
    addw ya,$EC                  ; 075C pitch_offset
    addw ya,$EC                  ; 075E pitch_offset
    addw ya,$EC                  ; 0760 pitch_offset
    addw ya,$EC                  ; 0762 pitch_offset
    addw ya,$EC                  ; 0764 pitch_offset
    movw $EC,ya                  ; 0766 pitch_offset
    mov !DSPADDR,#$4D            ; 0768 DSP EON
    mov a,!DSPDATA               ; 076B
    and a,#$DF                   ; 076D
    mov !DSPDATA,a               ; 076F
    bra tick_wait                ; 0771

play_sfx: ; cmd < $80: sfx number = cmd, channel = cmd_param -> sfx_start
    mov x,$055D                  ; 0773 data_055D; cmd_param
    call sfx_start               ; 0776
    bra loc_078E                 ; 0779

cmd6_play: ; play flag $1C = 1, stop timers
    mov $1C,#$01                 ; 077B play_flag
    mov !CONTROL,#$00            ; 077E
tick_wait: ; if playing: T0 target = $E4, wait one tick, then step all channels
    mov a,$1C                    ; 0781 play_flag
    bne loc_0788                 ; 0783
    jmp main_loop                ; 0785

loc_0788:
    mov !T0TARGET,$E4            ; 0788 t0_target
    mov !CONTROL,#$01            ; 078B
loc_078E:
    mov a,!T0OUT                 ; 078E
    beq loc_078E                 ; 0790
    mov !CONTROL,#$01            ; 0792
    mov $20,#$00                 ; 0795 tick_music
    clrc                         ; 0798
    adc $1E,$1F                  ; 0799 tempo; tempo_acc
    ror $20                      ; 079C tick_music
    mov $23,#$00                 ; 079E tick_sfx
    clrc                         ; 07A1
    adc $21,$22                  ; 07A2 tempo2; tempo_acc2
    ror $23                      ; 07A5 tick_sfx
    mov x,#$00                   ; 07A7
channel_loop: ; x = 0..7 music voices; x|8 = sfx voice on the same DSP channel
    mov a,$20                    ; 07A9 tick_music
    bne loc_07B2                 ; 07AB
    call channel_update          ; 07AD
    bra loc_07B7                 ; 07B0

loc_07B2:
    call seq_step                ; 07B2
    bne loc_07B2                 ; 07B5
loc_07B7:
    mov a,$01E0+x                ; 07B7 sfx_override[x]
    beq loc_07D0                 ; 07BA
    push x                       ; 07BC
    mov a,x                      ; 07BD
    or a,#$08                    ; 07BE
    mov x,a                      ; 07C0
    mov a,$23                    ; 07C1 tick_sfx
    bne loc_07CA                 ; 07C3
    call channel_update          ; 07C5
    bra loc_07CF                 ; 07C8

loc_07CA:
    call seq_step                ; 07CA
    bne loc_07CA                 ; 07CD
loc_07CF:
    pop x                        ; 07CF
loc_07D0:
    inc x                        ; 07D0
    cmp x,#$08                   ; 07D1
    beq loc_07D8                 ; 07D3
    jmp channel_loop             ; 07D5

loc_07D8:
    jmp main_loop                ; 07D8

cmd7_stop_to_loader: ; param != 0: jump straight to loader; else keyoff, ~200 T1 ticks, re-init, then loader
    mov a,$055D                  ; 07DB data_055D; cmd_param
    beq loc_07E3                 ; 07DE
    jmp loader_reset_dsp         ; 07E0

loc_07E3:
    mov !DSPADDR,#$5C            ; 07E3 DSP KOFF
    mov !DSPDATA,#$FF            ; 07E6
    mov !CONTROL,#$00            ; 07E9
    mov !T1TARGET,#$C8           ; 07EC
    mov !CONTROL,#$02            ; 07EF
loc_07F2:
    mov a,!T1OUT                 ; 07F2
    beq loc_07F2                 ; 07F4
    mov !DSPADDR,#$6C            ; 07F6 DSP FLG
    mov !DSPDATA,#$A0            ; 07F9
    mov x,#$00                   ; 07FC
    mov !DSPADDR,#$4D            ; 07FE DSP EON
    mov !DSPDATA,x               ; 0801
    mov !DSPADDR,#$2C            ; 0803 DSP EVOLL
    mov !DSPDATA,x               ; 0806
    mov !DSPADDR,#$3C            ; 0808 DSP EVOLR
    mov !DSPDATA,x               ; 080B
    call dsp_init                ; 080D
    jmp loader_reset_dsp         ; 0810


seq_step: ; per-channel sequencer step: countdown, key off at gate, fetch next event
    mov a,$0110+x                ; 0813 chan_active[x]
    bne loc_081B                 ; 0816
    mov a,#$00                   ; 0818
    ret                          ; 081A

loc_081B:
    dec $34+x                    ; 081B duration[x]
    mov a,$34+x                  ; 081D duration[x]
    cmp a,#$01                   ; 081F
    beq loc_0839                 ; 0821
    cmp a,#$FF                   ; 0823
    bne loc_082F                 ; 0825
    mov a,$24+x                  ; 0827 gate[x]
    beq seq_fetch                ; 0829
    dec $24+x                    ; 082B gate[x]
    bra loc_084A                 ; 082D

loc_082F:
    cmp a,#$00                   ; 082F
    bne loc_084A                 ; 0831
    mov a,$24+x                  ; 0833 gate[x]
    beq seq_fetch                ; 0835
    bra loc_084A                 ; 0837

loc_0839:
    mov a,$24+x                  ; 0839 gate[x]
    bne loc_084A                 ; 083B
    mov a,$01E0+x                ; 083D sfx_override[x]
    bne loc_084A                 ; 0840
    mov a,$0FC8+x                ; 0842 voice_bits
    mov !DSPADDR,#$5C            ; 0845 DSP KOFF
    mov !DSPDATA,a               ; 0848
loc_084A:
    call channel_update          ; 084A
    mov a,#$00                   ; 084D
    ret                          ; 084F

seq_fetch: ; read event byte at ($44/$54+x); < $80 = command via jtab_0FD8, else note
    mov a,$44+x                  ; 0850 seq_ptr_lo[x]
    mov y,$54+x                  ; 0852 seq_ptr_hi[x]
    movw $00,ya                  ; 0854 tmp0
    mov y,#$00                   ; 0856
    mov a,($00)+y                ; 0858 tmp0
    bmi loc_0862                 ; 085A
    push x                       ; 085C
    asl a                        ; 085D
    mov x,a                      ; 085E
    jmp (seq_cmd_table+x)        ; 085F

loc_0862:
    call seq_note                ; 0862
    bra loc_084A                 ; 0865


seq_note: ; note event: $80 = rest, $E0/$E1 = stored notes, else pitch table lookup + KON
    cmp a,#$80                   ; 0867
    bne loc_088B                 ; 0869
    mov a,$01E0+x                ; 086B sfx_override[x]
    bne loc_0888                 ; 086E
    mov a,$0FC8+x                ; 0870 voice_bits
    mov !DSPADDR,#$5C            ; 0873 DSP KOFF
    mov !DSPDATA,a               ; 0876
    mov a,x                      ; 0878
    and a,#$07                   ; 0879
    xcn a                        ; 087B
    or a,#$02                    ; 087C
    mov !DSPADDR,a               ; 087E
    mov a,#$00                   ; 0880
    mov !DSPDATA,a               ; 0882
    inc !DSPADDR                 ; 0884
    mov !DSPDATA,a               ; 0886
loc_0888:
    jmp seq_note_length          ; 0888

loc_088B:
    cmp a,#$E0                   ; 088B
    bmi loc_0899                 ; 088D
    cmp a,#$E1                   ; 088F
    beq loc_0897                 ; 0891
    mov a,$0C+x                  ; 0893 note_e0[x]
    bra loc_0899                 ; 0895

loc_0897:
    mov a,$14+x                  ; 0897 note_e1[x]
loc_0899:
    clrc                         ; 0899
    adc a,#$24                   ; 089A
    adc a,$0140+x                ; 089C transpose[x]
    asl a                        ; 089F
    push x                       ; 08A0
    mov y,$64+x                  ; 08A1 finetune[x]
    beq loc_08DF                 ; 08A3
    mov x,a                      ; 08A5
    mov $04,y                    ; 08A6 tmp4
    mov a,y                      ; 08A8
    bpl loc_08AE                 ; 08A9
    eor a,#$FF                   ; 08AB
    inc a                        ; 08AD
loc_08AE:
    mov y,a                      ; 08AE
    push y                       ; 08AF
    mov a,$11CC+x                ; 08B0 pitch_table
    mul ya                       ; 08B3
    mov $02,y                    ; 08B4 tmp2
    mov $03,#$00                 ; 08B6 tmp3
    pop y                        ; 08B9
    mov a,$11CD+x                ; 08BA data_11CD
    mul ya                       ; 08BD
    addw ya,$02                  ; 08BE tmp2
    mov $03,y                    ; 08C0 tmp3
    lsr $03                      ; 08C2 tmp3
    ror a                        ; 08C4
    lsr $03                      ; 08C5 tmp3
    ror a                        ; 08C7
    mov $02,a                    ; 08C8 tmp2
    mov a,$11CD+x                ; 08CA data_11CD
    mov y,a                      ; 08CD
    mov a,$11CC+x                ; 08CE pitch_table
    mov x,$04                    ; 08D1 tmp4
    bmi loc_08D9                 ; 08D3
    addw ya,$02                  ; 08D5 tmp2
    bra loc_08DB                 ; 08D7

loc_08D9:
    subw ya,$02                  ; 08D9 tmp2
loc_08DB:
    movw $02,ya                  ; 08DB tmp2
    bra loc_08EA                 ; 08DD

loc_08DF:
    mov x,a                      ; 08DF
    mov a,$11CC+x                ; 08E0 pitch_table
    mov $02,a                    ; 08E3 tmp2
    mov a,$11CD+x                ; 08E5 data_11CD
    mov $03,a                    ; 08E8 tmp3
loc_08EA:
    pop a                        ; 08EA
    mov x,a                      ; 08EB
    and a,#$07                   ; 08EC
    xcn a                        ; 08EE
    mov !DSPADDR,a               ; 08EF
    mov a,$01E0+x                ; 08F1 sfx_override[x]
    beq loc_08F9                 ; 08F4
    jmp seq_note_length          ; 08F6

loc_08F9:
    mov a,$0254+x                ; 08F9 vol_l[x]
    call scale_volume            ; 08FC
    mov !DSPDATA,a               ; 08FF
    inc !DSPADDR                 ; 0901
    mov a,$0264+x                ; 0903 vol_r[x]
    call scale_volume            ; 0906
    mov !DSPDATA,a               ; 0909
    inc !DSPADDR                 ; 090B
    mov a,$0150+x                ; 090D chan_flags[x]
    and a,#$01                   ; 0910
    beq loc_092B                 ; 0912
    mov a,$0160+x                ; 0914 slide_delay[x]
    mov $01A0+x,a                ; 0917 slide_wait[x]
    mov a,$0170+x                ; 091A slide_rate[x]
    mov $0100+x,a                ; 091D slide_timer[x]
    mov a,$0180+x                ; 0920 slide_steps[x]
    mov $94+x,a                  ; 0923 slide_count[x]
    mov a,$0190+x                ; 0925 slide_hold[x]
    mov $01C0+x,a                ; 0928 slide_hold_ctr[x]
loc_092B:
    mov a,$0150+x                ; 092B chan_flags[x]
    and a,#$02                   ; 092E
    beq loc_094D                 ; 0930
    mov a,$0234+x                ; 0932 vib_depth[x]
    bpl loc_093D                 ; 0935
    eor a,#$FF                   ; 0937
    inc a                        ; 0939
    mov $0234+x,a                ; 093A vib_depth[x]
loc_093D:
    mov a,$0200+x                ; 093D vib_rate[x]
    lsr a                        ; 0940
    mov $A4+x,a                  ; 0941 vib_count[x]
    mov a,$0210+x                ; 0943 vib_speed[x]
    mov $B4+x,a                  ; 0946 vib_step[x]
    mov a,$0220+x                ; 0948 vib_delay_init[x]
    mov $C4+x,a                  ; 094B vib_delay[x]
loc_094D:
    mov a,$02                    ; 094D tmp2
    mov $84+x,a                  ; 094F pitch_lo[x]
    mov !DSPDATA,a               ; 0951
    inc !DSPADDR                 ; 0953
    mov a,$03                    ; 0955 tmp3
    mov $74+x,a                  ; 0957 pitch_hi[x]
    mov !DSPDATA,a               ; 0959
    inc !DSPADDR                 ; 095B
    mov a,$0244+x                ; 095D srcn[x]
    mov !DSPDATA,a               ; 0960
    inc !DSPADDR                 ; 0962
    mov a,$0274+x                ; 0964 adsr1[x]
    mov !DSPDATA,a               ; 0967
    inc !DSPADDR                 ; 0969
    mov a,$0284+x                ; 096B adsr2[x]
    mov !DSPDATA,a               ; 096E
    inc !DSPADDR                 ; 0970
    mov !DSPDATA,#$7F            ; 0972
    mov !DSPADDR,#$5C            ; 0975 DSP KOFF
    mov !DSPDATA,#$00            ; 0978
    mov !DSPADDR,#$4C            ; 097B DSP KON
    mov a,$0FC8+x                ; 097E voice_bits
    mov !DSPDATA,a               ; 0981
seq_note_length: ; set duration $34+x / gate $24+x from $0120/$0130 or inline bytes
    mov a,$0120+x                ; 0983 note_len[x]
    beq loc_0997                 ; 0986
    mov $00,#$01                 ; 0988 tmp0
    mov a,$0120+x                ; 098B note_len[x]
    mov $34+x,a                  ; 098E duration[x]
    mov a,$0130+x                ; 0990 note_gate[x]
    mov $24+x,a                  ; 0993 gate[x]
    bra loc_09AE                 ; 0995

loc_0997:
    mov y,#$01                   ; 0997
    mov a,($00)+y                ; 0999 tmp0
    mov $34+x,a                  ; 099B duration[x]
    mov a,$01D0+x                ; 099D gate_mode[x]
    beq loc_09AB                 ; 09A0
    mov a,$34+x                  ; 09A2 duration[x]
    mov $24+x,a                  ; 09A4 gate[x]
    inc y                        ; 09A6
    mov a,($00)+y                ; 09A7 tmp0
    mov $34+x,a                  ; 09A9 duration[x]
loc_09AB:
    inc y                        ; 09AB
    mov $00,y                    ; 09AC tmp0
loc_09AE:
    mov $01,#$00                 ; 09AE tmp1
    mov a,$44+x                  ; 09B1 seq_ptr_lo[x]
    mov y,$54+x                  ; 09B3 seq_ptr_hi[x]
    addw ya,$00                  ; 09B5 tmp0
    mov $54+x,y                  ; 09B7 seq_ptr_hi[x]
    mov $44+x,a                  ; 09B9 seq_ptr_lo[x]
    ret                          ; 09BB


channel_update: ; per-tick pitch slide (bit0 of $0150+x), vibrato (bit1), tremolo/volume env (bits 2-3)
    mov a,$0150+x                ; 09BC chan_flags[x]
    and a,#$01                   ; 09BF
    bne loc_09C6                 ; 09C1
    jmp loc_0A39                 ; 09C3

loc_09C6:
    mov a,$01A0+x                ; 09C6 slide_wait[x]
    beq loc_09DA                 ; 09C9
    cmp a,#$FF                   ; 09CB
    beq loc_0A39                 ; 09CD
    dec a                        ; 09CF
    mov $01A0+x,a                ; 09D0 slide_wait[x]
    bne loc_0A39                 ; 09D3
    mov a,#$01                   ; 09D5
    mov $0100+x,a                ; 09D7 slide_timer[x]
loc_09DA:
    mov a,$0100+x                ; 09DA slide_timer[x]
    dec a                        ; 09DD
    mov $0100+x,a                ; 09DE slide_timer[x]
    bne loc_0A39                 ; 09E1
    mov a,$0170+x                ; 09E3 slide_rate[x]
    mov $0100+x,a                ; 09E6 slide_timer[x]
    mov a,$01C0+x                ; 09E9 slide_hold_ctr[x]
    beq loc_0A10                 ; 09EC
    dec a                        ; 09EE
    mov $01C0+x,a                ; 09EF slide_hold_ctr[x]
    mov a,$01B0+x                ; 09F2 slide_delta[x]
    eor a,#$FF                   ; 09F5
    inc a                        ; 09F7
    mov $00,a                    ; 09F8 tmp0
    bpl loc_0A00                 ; 09FA
    mov a,#$FF                   ; 09FC
    bra loc_0A02                 ; 09FE

loc_0A00:
    mov a,#$00                   ; 0A00
loc_0A02:
    mov $01,a                    ; 0A02 tmp1
    mov a,$84+x                  ; 0A04 pitch_lo[x]
    mov y,$74+x                  ; 0A06 pitch_hi[x]
    addw ya,$00                  ; 0A08 tmp0
    mov $74+x,y                  ; 0A0A pitch_hi[x]
    mov $84+x,a                  ; 0A0C pitch_lo[x]
    bra loc_0A1B                 ; 0A0E

loc_0A10:
    mov a,$01B0+x                ; 0A10 slide_delta[x]
    mov $00,a                    ; 0A13 tmp0
    bpl loc_0A00                 ; 0A15
    mov a,#$FF                   ; 0A17
    bra loc_0A02                 ; 0A19

loc_0A1B:
    mov a,$01E0+x                ; 0A1B sfx_override[x]
    bne loc_0A30                 ; 0A1E
    mov a,x                      ; 0A20
    and a,#$07                   ; 0A21
    xcn a                        ; 0A23
    or a,#$02                    ; 0A24
    mov !DSPADDR,a               ; 0A26
    mov a,$84+x                  ; 0A28 pitch_lo[x]
    mov !DSPDATA,a               ; 0A2A
    inc !DSPADDR                 ; 0A2C
    mov !DSPDATA,y               ; 0A2E
loc_0A30:
    dec $94+x                    ; 0A30 slide_count[x]
    bne loc_0A39                 ; 0A32
    mov a,#$FF                   ; 0A34
    mov $01A0+x,a                ; 0A36 slide_wait[x]
loc_0A39:
    mov a,$0150+x                ; 0A39 chan_flags[x]
    and a,#$02                   ; 0A3C
    beq loc_0AB4                 ; 0A3E
    mov a,$C4+x                  ; 0A40 vib_delay[x]
    beq loc_0A48                 ; 0A42
    dec $C4+x                    ; 0A44 vib_delay[x]
    bra loc_0AB4                 ; 0A46

loc_0A48:
    dec $B4+x                    ; 0A48 vib_step[x]
    bne loc_0AB4                 ; 0A4A
    mov a,$0210+x                ; 0A4C vib_speed[x]
    mov $B4+x,a                  ; 0A4F vib_step[x]
    mov a,$0234+x                ; 0A51 vib_depth[x]
    mov $00,a                    ; 0A54 tmp0
    bpl loc_0A5C                 ; 0A56
    mov a,#$FF                   ; 0A58
    bra loc_0A5E                 ; 0A5A

loc_0A5C:
    mov a,#$00                   ; 0A5C
loc_0A5E:
    mov $01,a                    ; 0A5E tmp1
    mov a,$84+x                  ; 0A60 pitch_lo[x]
    mov y,$74+x                  ; 0A62 pitch_hi[x]
    cmp x,#$0D                   ; 0A64
    bne loc_0A87                 ; 0A66
    push a                       ; 0A68
    mov a,$EC                    ; 0A69 pitch_offset
    cmp a,$EE                    ; 0A6B pitch_offset_prev
    bne loc_0A78                 ; 0A6D
    mov a,$ED                    ; 0A6F
    cmp a,$EF                    ; 0A71
    bne loc_0A78                 ; 0A73
    pop a                        ; 0A75
    bra loc_0A87                 ; 0A76

loc_0A78:
    pop a                        ; 0A78
    subw ya,$EE                  ; 0A79 pitch_offset_prev
    addw ya,$EC                  ; 0A7B pitch_offset
    push a                       ; 0A7D
    mov a,$EC                    ; 0A7E pitch_offset
    mov $EE,a                    ; 0A80 pitch_offset_prev
    mov a,$ED                    ; 0A82
    mov $EF,a                    ; 0A84
    pop a                        ; 0A86
loc_0A87:
    addw ya,$00                  ; 0A87 tmp0
    mov $74+x,y                  ; 0A89 pitch_hi[x]
    mov $84+x,a                  ; 0A8B pitch_lo[x]
    mov a,$01E0+x                ; 0A8D sfx_override[x]
    bne loc_0AA2                 ; 0A90
    mov a,x                      ; 0A92
    and a,#$07                   ; 0A93
    xcn a                        ; 0A95
    or a,#$02                    ; 0A96
    mov !DSPADDR,a               ; 0A98
    mov a,$84+x                  ; 0A9A pitch_lo[x]
    mov !DSPDATA,a               ; 0A9C
    inc !DSPADDR                 ; 0A9E
    mov !DSPDATA,y               ; 0AA0
loc_0AA2:
    dec $A4+x                    ; 0AA2 vib_count[x]
    bne loc_0AB4                 ; 0AA4
    mov a,$0200+x                ; 0AA6 vib_rate[x]
    mov $A4+x,a                  ; 0AA9 vib_count[x]
    mov a,$0234+x                ; 0AAB vib_depth[x]
    eor a,#$FF                   ; 0AAE
    inc a                        ; 0AB0
    mov $0234+x,a                ; 0AB1 vib_depth[x]
loc_0AB4:
    mov a,$0150+x                ; 0AB4 chan_flags[x]
    and a,#$0C                   ; 0AB7
    bne loc_0ABE                 ; 0AB9
    jmp loc_0B17                 ; 0ABB

loc_0ABE:
    mov a,$02A4+x                ; 0ABE trem_delay[x]
    beq loc_0ACD                 ; 0AC1
    mov a,$02A4+x                ; 0AC3 trem_delay[x]
    dec a                        ; 0AC6
    mov $02A4+x,a                ; 0AC7 trem_delay[x]
    jmp loc_0B17                 ; 0ACA

loc_0ACD:
    mov a,$02B4+x                ; 0ACD trem_count[x]
    dec a                        ; 0AD0
    mov $02B4+x,a                ; 0AD1 trem_count[x]
    beq loc_0AD9                 ; 0AD4
    jmp loc_0B17                 ; 0AD6

loc_0AD9:
    mov a,$02C4+x                ; 0AD9 trem_rate[x]
    mov $02B4+x,a                ; 0ADC trem_count[x]
    mov a,$01E0+x                ; 0ADF sfx_override[x]
    bne loc_0AF8                 ; 0AE2
    mov a,x                      ; 0AE4
    and a,#$07                   ; 0AE5
    xcn a                        ; 0AE7
    or a,#$00                    ; 0AE8
    mov !DSPADDR,a               ; 0AEA
    mov a,$0254+x                ; 0AEC vol_l[x]
    mov !DSPDATA,a               ; 0AEF
    mov a,$0264+x                ; 0AF1 vol_r[x]
    inc !DSPADDR                 ; 0AF4
    mov !DSPDATA,a               ; 0AF6
loc_0AF8:
    mov a,$02E4+x                ; 0AF8 trem_steps[x]
    dec a                        ; 0AFB
    mov $02E4+x,a                ; 0AFC trem_steps[x]
    bne loc_0B17                 ; 0AFF
    mov a,$0150+x                ; 0B01 chan_flags[x]
    and a,#$08                   ; 0B04
    bne loc_0B17                 ; 0B06
    mov a,$02F4+x                ; 0B08 trem_len[x]
    mov $02E4+x,a                ; 0B0B trem_steps[x]
    mov a,$02D4+x                ; 0B0E trem_delta[x]
    eor a,#$FF                   ; 0B11
    inc a                        ; 0B13
    mov $02D4+x,a                ; 0B14 trem_delta[x]
loc_0B17:
    ret                          ; 0B17

seq_end: ; seq cmd $00: end of track, key off voice
    pop x                        ; 0B18
    mov a,#$00                   ; 0B19
    mov $0110+x,a                ; 0B1B chan_active[x]
    mov a,$01E0+x                ; 0B1E sfx_override[x]
    bne loc_0B2B                 ; 0B21
    mov !DSPADDR,#$5C            ; 0B23 DSP KOFF
    mov a,$0FC8+x                ; 0B26 voice_bits
    mov !DSPDATA,a               ; 0B29
loc_0B2B:
    mov a,x                      ; 0B2B
    cmp a,#$08                   ; 0B2C
    bcc loc_0B61                 ; 0B2E
    push x                       ; 0B30
    setc                         ; 0B31
    sbc a,#$08                   ; 0B32
    mov x,a                      ; 0B34
    mov a,#$00                   ; 0B35
    mov $01E0+x,a                ; 0B37 sfx_override[x]
    mov !DSPADDR,#$3D            ; 0B3A DSP NON
    mov a,$0FC8+x                ; 0B3D voice_bits
    eor a,#$FF                   ; 0B40
    and a,!DSPDATA               ; 0B42
    mov !DSPDATA,a               ; 0B44
    mov !DSPADDR,#$4D            ; 0B46 DSP EON
    mov a,$0294+x                ; 0B49 echo_on[x]
    beq loc_0B57                 ; 0B4C
    mov a,$0FC8+x                ; 0B4E voice_bits
    or a,!DSPDATA                ; 0B51
    mov !DSPDATA,a               ; 0B53
    bra loc_0B60                 ; 0B55

loc_0B57:
    mov a,$0FC8+x                ; 0B57 voice_bits
    eor a,#$FF                   ; 0B5A
    and a,!DSPDATA               ; 0B5C
    mov !DSPDATA,a               ; 0B5E
loc_0B60:
    pop x                        ; 0B60
loc_0B61:
    mov a,#$00                   ; 0B61
    ret                          ; 0B63


seq_pop_x: ; drop return address, restore x from stack (helper for seq commands)
    pop y                        ; 0B64
    pop a                        ; 0B65
    pop x                        ; 0B66
    push a                       ; 0B67
    push y                       ; 0B68

seq_retrigger: ; duration=1, gate=0
    mov y,#$01                   ; 0B69
    mov $34+x,y                  ; 0B6B duration[x]
    mov a,#$00                   ; 0B6D
    mov $24+x,a                  ; 0B6F gate[x]
    ret                          ; 0B71

seq_instrument: ; seq cmd $01
    call seq_pop_x               ; 0B72
    call seq_load_srcn           ; 0B75
loc_0B78:
    mov $00,#$02                 ; 0B78 tmp0
loc_0B7B:
    mov $01,#$00                 ; 0B7B tmp1
    mov a,$44+x                  ; 0B7E seq_ptr_lo[x]
    mov y,$54+x                  ; 0B80 seq_ptr_hi[x]
    addw ya,$00                  ; 0B82 tmp0
    mov $54+x,y                  ; 0B84 seq_ptr_hi[x]
    mov $44+x,a                  ; 0B86 seq_ptr_lo[x]
    mov a,#$01                   ; 0B88
    ret                          ; 0B8A


seq_load_srcn: ; sample index -> sample_remap[$0560] -> SRCN ($0244+x)
    push x                       ; 0B8B
    mov a,($00)+y                ; 0B8C tmp0
    mov x,a                      ; 0B8E
    mov a,$0560+x                ; 0B8F data_0560; sample_remap[256] (uploaded by 65816 loc_C18288)
    pop x                        ; 0B92
    mov $0244+x,a                ; 0B93 srcn[x]
    ret                          ; 0B96

seq_instr_full: ; seq cmd $22: instrument, transpose, finetune, volume, ADSR
    call seq_pop_x               ; 0B97
    call seq_load_srcn           ; 0B9A
    inc y                        ; 0B9D
    mov a,($00)+y                ; 0B9E tmp0
    mov $0140+x,a                ; 0BA0 transpose[x]
    inc y                        ; 0BA3
    mov a,($00)+y                ; 0BA4 tmp0
    mov $64+x,a                  ; 0BA6 finetune[x]
    inc y                        ; 0BA8
    call seq_read_volume         ; 0BA9
    inc y                        ; 0BAC
    call seq_read_adsr           ; 0BAD
    mov $00,#$08                 ; 0BB0 tmp0
    jmp loc_0B7B                 ; 0BB3

seq_volume: ; seq cmd $02
    call seq_pop_x               ; 0BB6
    call seq_read_volume         ; 0BB9
loc_0BBC:
    mov $00,#$03                 ; 0BBC tmp0
    jmp loc_0B7B                 ; 0BBF


seq_read_volume: ; L,R bytes; averaged when mono flag set
    mov a,$1D                    ; 0BC2 mono_flag
    bne loc_0BD2                 ; 0BC4
    mov a,($00)+y                ; 0BC6 tmp0
    mov $0254+x,a                ; 0BC8 vol_l[x]
    inc y                        ; 0BCB

sub_0BCC:
    mov a,($00)+y                ; 0BCC tmp0
    mov $0264+x,a                ; 0BCE vol_r[x]
    ret                          ; 0BD1

loc_0BD2:
    mov a,($00)+y                ; 0BD2 tmp0
    bpl loc_0BD9                 ; 0BD4
    eor a,#$FF                   ; 0BD6
    inc a                        ; 0BD8
loc_0BD9:
    lsr a                        ; 0BD9
    mov $03,a                    ; 0BDA tmp3
    inc y                        ; 0BDC
    clrc                         ; 0BDD
    mov a,($00)+y                ; 0BDE tmp0
    bpl loc_0BE5                 ; 0BE0
    eor a,#$FF                   ; 0BE2
    inc a                        ; 0BE4
loc_0BE5:
    lsr a                        ; 0BE5
    clrc                         ; 0BE6
    adc a,$03                    ; 0BE7 tmp3
    mov $0254+x,a                ; 0BE9 vol_l[x]
    mov $0264+x,a                ; 0BEC vol_r[x]
    ret                          ; 0BEF

seq_volume_mono: ; seq cmd $23: one byte -> both channels
    call seq_pop_x               ; 0BF0
    mov $0254+x,a                ; 0BF3 vol_l[x]
    call sub_0BCC                ; 0BF6
    mov a,$0264+x                ; 0BF9 vol_r[x]
    mov $0254+x,a                ; 0BFC vol_l[x]
    jmp loc_0B78                 ; 0BFF

seq_volume_preset: ; seq cmd $20: volume from $04B8/$04B9
    call seq_pop_x               ; 0C02
    mov a,$04B8                  ; 0C05 vol_preset_l
    mov $0254+x,a                ; 0C08 vol_l[x]
    mov a,$04B9                  ; 0C0B vol_preset_r
    mov $0264+x,a                ; 0C0E vol_r[x]
    mov a,$1D                    ; 0C11 mono_flag
    bne loc_0C2E                 ; 0C13
    jmp loc_0F09                 ; 0C15

orphan_volume_preset2: ; stale seq_cmd_table entry $31: copy of seq cmd $20 using $04BA/$04BB
    call seq_pop_x               ; 0C18
    mov a,$04BA                  ; 0C1B vol_preset2_l
    mov $0254+x,a                ; 0C1E vol_l[x]
    mov a,$04BB                  ; 0C21 vol_preset2_r
    mov $0264+x,a                ; 0C24 vol_r[x]
    mov a,$1D                    ; 0C27 mono_flag
    bne loc_0C2E                 ; 0C29
    jmp loc_0F09                 ; 0C2B

loc_0C2E:
    mov a,$0254+x                ; 0C2E vol_l[x]
    bpl loc_0C36                 ; 0C31
    eor a,#$FF                   ; 0C33
    inc a                        ; 0C35
loc_0C36:
    lsr a                        ; 0C36
    mov $00,a                    ; 0C37 tmp0
    mov a,$0264+x                ; 0C39 vol_r[x]
    bpl loc_0C41                 ; 0C3C
    eor a,#$FF                   ; 0C3E
    inc a                        ; 0C40
loc_0C41:
    lsr a                        ; 0C41
    clrc                         ; 0C42
    adc a,$00                    ; 0C43 tmp0
    mov $0254+x,a                ; 0C45 vol_l[x]
    mov $0264+x,a                ; 0C48 vol_r[x]
    jmp loc_0F09                 ; 0C4B

seq_master_percent: ; seq cmd $24: $04B6 = master volume percent
    call seq_pop_x               ; 0C4E
    mov a,($00)+y                ; 0C51 tmp0
    mov $04B6,a                  ; 0C53 master_percent
    jmp loc_0B78                 ; 0C56


scale_volume: ; a = a * $04B6 / 100, clamp +/-127
    push x                       ; 0C59
    mov y,$04B6                  ; 0C5A master_percent
    cmp x,#$08                   ; 0C5D
    bcs loc_0C6F                 ; 0C5F
    cmp a,#$00                   ; 0C61
    bmi loc_0C71                 ; 0C63
    mul ya                       ; 0C65
    mov x,#$64                   ; 0C66
    div ya,x                     ; 0C68
    cmp a,#$7F                   ; 0C69
    bmi loc_0C6F                 ; 0C6B
    mov a,#$7F                   ; 0C6D
loc_0C6F:
    pop x                        ; 0C6F
    ret                          ; 0C70

loc_0C71:
    eor a,#$FF                   ; 0C71
    inc a                        ; 0C73
    mul ya                       ; 0C74
    mov x,#$64                   ; 0C75
    div ya,x                     ; 0C77
    cmp a,#$7F                   ; 0C78
    bmi loc_0C7E                 ; 0C7A
    mov a,#$7F                   ; 0C7C
loc_0C7E:
    eor a,#$FF                   ; 0C7E
    inc a                        ; 0C80
    pop x                        ; 0C81
    ret                          ; 0C82

seq_volume_presets: ; seq cmd $1E: 4 bytes -> $04B8..$04BB
    call seq_pop_x               ; 0C83
    mov a,($00)+y                ; 0C86 tmp0
    mov $04B8,a                  ; 0C88 vol_preset_l
    inc y                        ; 0C8B
    mov a,($00)+y                ; 0C8C tmp0
    mov $04B9,a                  ; 0C8E vol_preset_r
    inc y                        ; 0C91
    mov a,($00)+y                ; 0C92 tmp0
    mov $04BA,a                  ; 0C94 vol_preset2_l
    inc y                        ; 0C97
    mov a,($00)+y                ; 0C98 tmp0
    mov $04BB,a                  ; 0C9A vol_preset2_r
    jmp seq_advance5             ; 0C9D

seq_echo_delay: ; seq cmd $1F: EDL, ESA = $FF - EDL*8, clear echo buffer
    call seq_pop_x               ; 0CA0
    mov a,($00)+y                ; 0CA3 tmp0
    call dsp_flg_20              ; 0CA5
    mov !DSPADDR,#$7D            ; 0CA8 DSP EDL
    clrc                         ; 0CAB
    lsr a                        ; 0CAC
    mov !DSPDATA,a               ; 0CAD
    mov !DSPADDR,#$6D            ; 0CAF DSP ESA
    rol a                        ; 0CB2
    rol a                        ; 0CB3
    rol a                        ; 0CB4
    mov $00,a                    ; 0CB5 tmp0
    mov a,#$FF                   ; 0CB7
    setc                         ; 0CB9
    sbc a,$00                    ; 0CBA tmp0
    mov !DSPDATA,a               ; 0CBC
    mov $04B7,a                  ; 0CBE esa_value
    mov y,a                      ; 0CC1
    mov a,#$00                   ; 0CC2
    movw $00,ya                  ; 0CC4 tmp0
loc_0CC6:
    mov y,a                      ; 0CC6
loc_0CC7:
    mov ($00)+y,a                ; 0CC7 tmp0
    inc y                        ; 0CC9
    bne loc_0CC7                 ; 0CCA
    inc $01                      ; 0CCC tmp1
    mov y,$01                    ; 0CCE tmp1
    cmp y,#$00                   ; 0CD0
    bne loc_0CC6                 ; 0CD2
    jmp loc_0B78                 ; 0CD4

seq_jump: ; seq cmd $03: new pointer
    call seq_pop_x               ; 0CD7
    mov a,($00)+y                ; 0CDA tmp0
    mov $44+x,a                  ; 0CDC seq_ptr_lo[x]
    inc y                        ; 0CDE
    mov a,($00)+y                ; 0CDF tmp0
    mov $54+x,a                  ; 0CE1 seq_ptr_hi[x]
    mov a,#$01                   ; 0CE3
    ret                          ; 0CE5

seq_call: ; seq cmd $04: count, addr; push return on $0334/$03B4/$0434 stack
    call seq_pop_x               ; 0CE6
    mov a,($00)+y                ; 0CE9 tmp0
    mov $04,a                    ; 0CEB tmp4
    inc y                        ; 0CED
    call seq_push_return         ; 0CEE
loc_0CF1:
    mov $0334+y,a                ; 0CF1 seq_stack_lo[16*x]
loc_0CF4:
    inc $D4+x                    ; 0CF4 seq_sp[x]
    movw ya,$02                  ; 0CF6 tmp2
    mov $44+x,a                  ; 0CF8 seq_ptr_lo[x]
    mov $54+x,y                  ; 0CFA seq_ptr_hi[x]
    mov a,#$01                   ; 0CFC
    ret                          ; 0CFE

seq_call_once: ; seq cmd $21: call (addr) with count 1
    call seq_pop_x               ; 0CFF
    mov $04,#$01                 ; 0D02 tmp4
    call seq_push_return         ; 0D05
    beq loc_0D0E                 ; 0D08
    dec a                        ; 0D0A
    jmp loc_0CF1                 ; 0D0B

loc_0D0E:
    dec a                        ; 0D0E
    mov $0334+y,a                ; 0D0F seq_stack_lo[16*x]
    mov a,$03B4+y                ; 0D12 seq_stack_hi
    dec a                        ; 0D15
    mov $03B4+y,a                ; 0D16 seq_stack_hi
    jmp loc_0CF4                 ; 0D19


seq_push_return: ; read target word, push count/return address
    mov a,($00)+y                ; 0D1C tmp0
    mov $02,a                    ; 0D1E tmp2
    inc y                        ; 0D20
    mov a,($00)+y                ; 0D21 tmp0
    mov $03,a                    ; 0D23 tmp3
    mov y,$D4+x                  ; 0D25 seq_sp[x]
    mov a,$04                    ; 0D27 tmp4
    mov $0434+y,a                ; 0D29 seq_stack_count
    mov a,$54+x                  ; 0D2C seq_ptr_hi[x]
    mov $03B4+y,a                ; 0D2E seq_stack_hi
    mov a,$44+x                  ; 0D31 seq_ptr_lo[x]
    ret                          ; 0D33

seq_return: ; seq cmd $05: pop; repeat while count > 0
    call seq_pop_x               ; 0D34
    dec $D4+x                    ; 0D37 seq_sp[x]
    mov y,$D4+x                  ; 0D39 seq_sp[x]
    mov a,$03B4+y                ; 0D3B seq_stack_hi
    mov $54+x,a                  ; 0D3E seq_ptr_hi[x]
    mov a,$0334+y                ; 0D40 seq_stack_lo[16*x]
    mov $44+x,a                  ; 0D43 seq_ptr_lo[x]
    mov a,$0434+y                ; 0D45 seq_stack_count
    dec a                        ; 0D48
    mov $0434+y,a                ; 0D49 seq_stack_count
    beq loc_0D6A                 ; 0D4C
    mov a,$44+x                  ; 0D4E seq_ptr_lo[x]
    mov y,$54+x                  ; 0D50 seq_ptr_hi[x]
    movw $00,ya                  ; 0D52 tmp0
    mov y,#$02                   ; 0D54
    mov a,($00)+y                ; 0D56 tmp0
    mov $02,a                    ; 0D58 tmp2
    inc y                        ; 0D5A
    mov a,($00)+y                ; 0D5B tmp0
    mov $03,a                    ; 0D5D tmp3
    inc $D4+x                    ; 0D5F seq_sp[x]
    movw ya,$02                  ; 0D61 tmp2
    mov $44+x,a                  ; 0D63 seq_ptr_lo[x]
    mov $54+x,y                  ; 0D65 seq_ptr_hi[x]
    mov a,#$01                   ; 0D67
    ret                          ; 0D69

loc_0D6A:
    mov $00,#$04                 ; 0D6A tmp0
    jmp loc_0B7B                 ; 0D6D

seq_set_length: ; seq cmd $06: note length (+gate byte if $01D0+x)
    call seq_pop_x               ; 0D70
    mov a,($00)+y                ; 0D73 tmp0
    mov $0120+x,a                ; 0D75 note_len[x]
    mov a,$01D0+x                ; 0D78 gate_mode[x]
    beq loc_0D89                 ; 0D7B
    mov a,$0120+x                ; 0D7D note_len[x]
    mov $0130+x,a                ; 0D80 note_gate[x]
    inc y                        ; 0D83
    mov a,($00)+y                ; 0D84 tmp0
    mov $0120+x,a                ; 0D86 note_len[x]
loc_0D89:
    inc y                        ; 0D89
    mov $00,y                    ; 0D8A tmp0
    jmp loc_0B7B                 ; 0D8C

seq_clear_length: ; seq cmd $07: lengths inline after notes
    pop x                        ; 0D8F
    mov a,#$00                   ; 0D90
    mov $0120+x,a                ; 0D92 note_len[x]
    mov $0130+x,a                ; 0D95 note_gate[x]
    jmp loc_0DDF                 ; 0D98

seq_slide_up: ; seq cmd $08: pitch slide (5 bytes)
    pop x                        ; 0D9B
    mov y,#$04                   ; 0D9C
    mov a,($00)+y                ; 0D9E tmp0
    bra loc_0DAA                 ; 0DA0

seq_slide_down: ; seq cmd $09
    pop x                        ; 0DA2
    mov y,#$04                   ; 0DA3
    mov a,($00)+y                ; 0DA5 tmp0
    eor a,#$FF                   ; 0DA7
    inc a                        ; 0DA9
loc_0DAA:
    mov $01B0+x,a                ; 0DAA slide_delta[x]
    mov a,$0150+x                ; 0DAD chan_flags[x]
    or a,#$01                    ; 0DB0
    mov $0150+x,a                ; 0DB2 chan_flags[x]
    call seq_retrigger           ; 0DB5
    mov a,($00)+y                ; 0DB8 tmp0
    mov $0160+x,a                ; 0DBA slide_delay[x]
    inc y                        ; 0DBD
    mov a,($00)+y                ; 0DBE tmp0
    mov $0170+x,a                ; 0DC0 slide_rate[x]
    inc y                        ; 0DC3
    mov a,($00)+y                ; 0DC4 tmp0
    mov $0180+x,a                ; 0DC6 slide_steps[x]
    inc y                        ; 0DC9
    inc y                        ; 0DCA
    mov a,($00)+y                ; 0DCB tmp0
    mov $0190+x,a                ; 0DCD slide_hold[x]
    mov $00,#$06                 ; 0DD0 tmp0
    jmp loc_0B7B                 ; 0DD3

seq_slide_off: ; seq cmd $0A
    pop x                        ; 0DD6
    mov a,$0150+x                ; 0DD7 chan_flags[x]
    and a,#$FE                   ; 0DDA
    mov $0150+x,a                ; 0DDC chan_flags[x]
loc_0DDF:
    mov a,#$01                   ; 0DDF
    mov $00,a                    ; 0DE1 tmp0
    mov $34+x,a                  ; 0DE3 duration[x]
    dec a                        ; 0DE5
    mov $24+x,a                  ; 0DE6 gate[x]
    jmp loc_0B7B                 ; 0DE8

seq_tempo: ; seq cmd $0B: $1F = tempo (tick accumulator increment)
    pop x                        ; 0DEB
    mov y,#$01                   ; 0DEC
    mov a,($00)+y                ; 0DEE tmp0
    mov $1F,a                    ; 0DF0 tempo
loc_0DF2:
    call seq_retrigger           ; 0DF2
    jmp loc_0B78                 ; 0DF5

seq_tempo_add: ; seq cmd $0C
    pop x                        ; 0DF8
    mov y,#$01                   ; 0DF9
    mov a,($00)+y                ; 0DFB tmp0
    clrc                         ; 0DFD
    adc a,$1F                    ; 0DFE tempo
    mov $1F,a                    ; 0E00 tempo
    jmp loc_0DF2                 ; 0E02

seq_vibrato_off: ; seq cmd $0E
    pop x                        ; 0E05
    mov a,$0150+x                ; 0E06 chan_flags[x]
    and a,#$FD                   ; 0E09
    mov $0150+x,a                ; 0E0B chan_flags[x]
    jmp loc_0DDF                 ; 0E0E

seq_vibrato: ; seq cmd $0D: rate, speed, depth
    pop x                        ; 0E11
    mov a,#$00                   ; 0E12
    call seq_read_vibrato        ; 0E14
    jmp loc_0D6A                 ; 0E17

seq_vibrato_delay: ; seq cmd $0F: delay, rate, speed, depth
    pop x                        ; 0E1A
    mov y,#$04                   ; 0E1B
    mov a,($00)+y                ; 0E1D tmp0
    call seq_read_vibrato        ; 0E1F
    jmp seq_advance5             ; 0E22


seq_read_vibrato:
    mov $0220+x,a                ; 0E25 vib_delay_init[x]
    mov a,$0150+x                ; 0E28 chan_flags[x]
    or a,#$02                    ; 0E2B
    mov $0150+x,a                ; 0E2D chan_flags[x]
    call seq_retrigger           ; 0E30
    mov a,($00)+y                ; 0E33 tmp0
    mov $0200+x,a                ; 0E35 vib_rate[x]
    inc y                        ; 0E38
    mov a,($00)+y                ; 0E39 tmp0
    mov $0210+x,a                ; 0E3B vib_speed[x]
    inc y                        ; 0E3E
    mov a,($00)+y                ; 0E3F tmp0
    mov $0234+x,a                ; 0E41 vib_depth[x]
    ret                          ; 0E44

seq_adsr: ; seq cmd $10: ADSR1, ADSR2
    call seq_pop_x               ; 0E45
    call seq_read_adsr           ; 0E48
    jmp loc_0BBC                 ; 0E4B


seq_read_adsr:
    mov a,($00)+y                ; 0E4E tmp0
    mov $0274+x,a                ; 0E50 adsr1[x]
    inc y                        ; 0E53
    mov a,($00)+y                ; 0E54 tmp0
    mov $0284+x,a                ; 0E56 adsr2[x]
    ret                          ; 0E59

seq_master_volume: ; seq cmd $11: MVOLL, MVOLR
    call seq_pop_x               ; 0E5A
    mov !DSPADDR,#$0C            ; 0E5D DSP MVOLL
    mov a,$1D                    ; 0E60 mono_flag
    beq loc_0E75                 ; 0E62
    mov a,($00)+y                ; 0E64 tmp0
    mov !DSPDATA,a               ; 0E66
    inc y                        ; 0E68
    clrc                         ; 0E69
    adc a,($00)+y                ; 0E6A tmp0
    ror a                        ; 0E6C
    mov $0230,a                  ; 0E6D mvol_lr
    mov !DSPADDR,#$1C            ; 0E70 DSP MVOLR
    bra loc_0E82                 ; 0E73

loc_0E75:
    mov a,($00)+y                ; 0E75 tmp0
    mov !DSPDATA,a               ; 0E77
    mov $0230,a                  ; 0E79 mvol_lr
    inc y                        ; 0E7C
    mov !DSPADDR,#$1C            ; 0E7D DSP MVOLR
    mov a,($00)+y                ; 0E80 tmp0
loc_0E82:
    mov !DSPDATA,a               ; 0E82
    mov $0231,a                  ; 0E84
    mov $00,#$03                 ; 0E87 tmp0
    jmp loc_0B7B                 ; 0E8A

seq_set_note_E0: ; seq cmd $1C: note used by event $E0
    pop x                        ; 0E8D
    mov y,#$01                   ; 0E8E
    mov a,($00)+y                ; 0E90 tmp0
    mov $0C+x,a                  ; 0E92 note_e0[x]
    jmp loc_0E9E                 ; 0E94

seq_set_note_E1: ; seq cmd $1D: note used by event $E1
    pop x                        ; 0E97
    mov y,#$01                   ; 0E98
    mov a,($00)+y                ; 0E9A tmp0
    mov $14+x,a                  ; 0E9C note_e1[x]
loc_0E9E:
    call seq_retrigger           ; 0E9E
    jmp loc_0B78                 ; 0EA1

seq_finetune: ; seq cmd $12: $64+x
    call seq_pop_x               ; 0EA4
    mov a,($00)+y                ; 0EA7 tmp0
    mov $64+x,a                  ; 0EA9 finetune[x]
    jmp loc_0B78                 ; 0EAB

seq_transpose: ; seq cmd $13
    call seq_pop_x               ; 0EAE
    mov $24+x,a                  ; 0EB1 gate[x]
    mov a,($00)+y                ; 0EB3 tmp0
    mov $0140+x,a                ; 0EB5 transpose[x]
    jmp loc_0B78                 ; 0EB8

seq_transpose_add: ; seq cmd $14
    call seq_pop_x               ; 0EBB
    mov a,($00)+y                ; 0EBE tmp0
    clrc                         ; 0EC0
    adc a,$0140+x                ; 0EC1 transpose[x]
    mov $0140+x,a                ; 0EC4 transpose[x]
    jmp loc_0B78                 ; 0EC7

seq_echo_setup: ; seq cmd $15: EFB, EVOLL, EVOLR; FLG = 0 (echo on)
    pop x                        ; 0ECA
    mov !DSPADDR,#$0D            ; 0ECB DSP EFB
    mov y,#$01                   ; 0ECE
    mov a,($00)+y                ; 0ED0 tmp0
    mov !DSPDATA,a               ; 0ED2
    inc y                        ; 0ED4
    mov !DSPADDR,#$2C            ; 0ED5 DSP EVOLL
    mov a,($00)+y                ; 0ED8 tmp0
    mov $0232,a                  ; 0EDA evol_lr
    mov !DSPDATA,a               ; 0EDD
    mov !DSPADDR,#$3C            ; 0EDF DSP EVOLR
    inc y                        ; 0EE2
    mov a,($00)+y                ; 0EE3 tmp0
    mov $0233,a                  ; 0EE5
    mov !DSPDATA,a               ; 0EE8
    mov a,#$00                   ; 0EEA
    mov $04B5,a                  ; 0EEC flg_echo_off
    mov !DSPADDR,#$6C            ; 0EEF DSP FLG
    mov !DSPDATA,a               ; 0EF2
    jmp loc_0D6A                 ; 0EF4

seq_echo_on: ; seq cmd $16: EON |= voice bit
    call seq_pop_x               ; 0EF7
    mov !DSPADDR,#$4D            ; 0EFA DSP EON
    mov a,$0FC8+x                ; 0EFD voice_bits
    or a,!DSPDATA                ; 0F00
    mov !DSPDATA,a               ; 0F02
    mov a,#$01                   ; 0F04
    mov $0294+x,a                ; 0F06 echo_on[x]
loc_0F09:
    mov $00,#$01                 ; 0F09 tmp0
    jmp loc_0B7B                 ; 0F0C

seq_echo_off: ; seq cmd $17
    pop x                        ; 0F0F
    mov !DSPADDR,#$4D            ; 0F10 DSP EON
    mov a,$0FC8+x                ; 0F13 voice_bits
    eor a,#$FF                   ; 0F16
    and a,!DSPDATA               ; 0F18
    mov !DSPDATA,a               ; 0F1A
    mov a,#$00                   ; 0F1C
    mov $0294+x,a                ; 0F1E echo_on[x]
    mov $24+x,a                  ; 0F21 gate[x]
    inc a                        ; 0F23
    mov $34+x,a                  ; 0F24 duration[x]
    jmp loc_0F09                 ; 0F26

seq_fir: ; seq cmd $18: 8 FIR coefficients
    call seq_pop_x               ; 0F29
    mov !DSPADDR,#$0F            ; 0F2C DSP FIR0
loc_0F2F:
    mov a,($00)+y                ; 0F2F tmp0
    mov !DSPDATA,a               ; 0F31
    inc y                        ; 0F33
    clrc                         ; 0F34
    adc !DSPADDR,#$10            ; 0F35
    cmp !DSPADDR,#$8F            ; 0F38
    bne loc_0F2F                 ; 0F3B
    mov $00,#$09                 ; 0F3D tmp0
    jmp loc_0B7B                 ; 0F40

seq_noise_clock: ; seq cmd $19: FLG noise bits ($04B4)
    call seq_pop_x               ; 0F43
    mov a,($00)+y                ; 0F46 tmp0
    mov $04B4,a                  ; 0F48 noise_clock
    or a,$04B5                   ; 0F4B flg_echo_off
    mov !DSPADDR,#$6C            ; 0F4E DSP FLG
    mov !DSPDATA,a               ; 0F51
    jmp loc_0B78                 ; 0F53

seq_noise_on: ; seq cmd $1A
    pop x                        ; 0F56
    mov !DSPADDR,#$3D            ; 0F57 DSP NON
    mov a,$0FC8+x                ; 0F5A voice_bits
    or a,!DSPDATA                ; 0F5D
    mov !DSPDATA,a               ; 0F5F
loc_0F61:
    call seq_retrigger           ; 0F61
    jmp loc_0F09                 ; 0F64

seq_noise_off: ; seq cmd $1B
    pop x                        ; 0F67
    mov !DSPADDR,#$3D            ; 0F68 DSP NON
    mov a,$0FC8+x                ; 0F6B voice_bits
    eor a,#$FF                   ; 0F6E
    and a,!DSPDATA               ; 0F70
    mov !DSPDATA,a               ; 0F72
    jmp loc_0F61                 ; 0F74

orphan_slide_up2: ; stale seq_cmd_table entry $26: slide variant (4 operand bytes)
    pop x                        ; 0F77
    mov y,#$04                   ; 0F78
    mov a,($00)+y                ; 0F7A tmp0
    eor a,#$FF                   ; 0F7C
    inc a                        ; 0F7E
    bra loc_0F86                 ; 0F7F

orphan_slide_down2: ; stale seq_cmd_table entry $27
    pop x                        ; 0F81
    mov y,#$04                   ; 0F82
    mov a,($00)+y                ; 0F84 tmp0
loc_0F86:
    mov $01B0+x,a                ; 0F86 slide_delta[x]
    mov a,$0150+x                ; 0F89 chan_flags[x]
    or a,#$01                    ; 0F8C
    mov $0150+x,a                ; 0F8E chan_flags[x]
    call seq_retrigger           ; 0F91
    mov a,($00)+y                ; 0F94 tmp0
    mov $0160+x,a                ; 0F96 slide_delay[x]
    inc y                        ; 0F99
    mov a,($00)+y                ; 0F9A tmp0
    mov $0170+x,a                ; 0F9C slide_rate[x]
    inc y                        ; 0F9F
    mov a,($00)+y                ; 0FA0 tmp0
    mov $0190+x,a                ; 0FA2 slide_hold[x]
    asl a                        ; 0FA5
    mov $0180+x,a                ; 0FA6 slide_steps[x]
seq_advance5: ; pointer += 5
    mov $00,#$05                 ; 0FA9 tmp0
    jmp loc_0B7B                 ; 0FAC

orphan_gate_on: ; stale seq_cmd_table entry $2B: gate_mode $01D0+x = 1
    call seq_pop_x               ; 0FAF
    inc a                        ; 0FB2
    mov $01D0+x,a                ; 0FB3 gate_mode[x]
    jmp loc_0F09                 ; 0FB6

orphan_gate_off: ; stale seq_cmd_table entry $2C: gate_mode $01D0+x = 0
    call seq_pop_x               ; 0FB9
    mov $01D0+x,a                ; 0FBC gate_mode[x]
    jmp loc_0F09                 ; 0FBF

    incbin "../data/04.bin":$0AEA..$0AF0     ; 6 bytes  SPC $0FC2-$0FC7

voice_bits: ; 8 x (1 << voice), twice: index by x (0-7) or x|8
    incbin "../data/04.bin":$0AF0..$0B00     ; 16 bytes  SPC $0FC8-$0FD7

seq_cmd_table: ; sequence command opcodes $00-$24 (+ stale $25-$32)
    dw seq_end                   ; 0FD8  [00]
    dw seq_instrument            ; 0FDA  [01]
    dw seq_volume                ; 0FDC  [02]
    dw seq_jump                  ; 0FDE  [03]
    dw seq_call                  ; 0FE0  [04]
    dw seq_return                ; 0FE2  [05]
    dw seq_set_length            ; 0FE4  [06]
    dw seq_clear_length          ; 0FE6  [07]
    dw seq_slide_up              ; 0FE8  [08]
    dw seq_slide_down            ; 0FEA  [09]
    dw seq_slide_off             ; 0FEC  [0A]
    dw seq_tempo                 ; 0FEE  [0B]
    dw seq_tempo_add             ; 0FF0  [0C]
    dw seq_vibrato               ; 0FF2  [0D]
    dw seq_vibrato_off           ; 0FF4  [0E]
    dw seq_vibrato_delay         ; 0FF6  [0F]
    dw seq_adsr                  ; 0FF8  [10]
    dw seq_master_volume         ; 0FFA  [11]
    dw seq_finetune              ; 0FFC  [12]
    dw seq_transpose             ; 0FFE  [13]
    dw seq_transpose_add         ; 1000  [14]
    dw seq_echo_setup            ; 1002  [15]
    dw seq_echo_on               ; 1004  [16]
    dw seq_echo_off              ; 1006  [17]
    dw seq_fir                   ; 1008  [18]
    dw seq_noise_clock           ; 100A  [19]
    dw seq_noise_on              ; 100C  [1A]
    dw seq_noise_off             ; 100E  [1B]
    dw seq_set_note_E0           ; 1010  [1C]
    dw seq_set_note_E1           ; 1012  [1D]
    dw seq_volume_presets        ; 1014  [1E]
    dw seq_echo_delay            ; 1016  [1F]
    dw seq_volume_preset         ; 1018  [20]
    dw seq_call_once             ; 101A  [21]
    dw seq_instr_full            ; 101C  [22]
    dw seq_volume_mono           ; 101E  [23]
    dw seq_master_percent        ; 1020  [24]
    incbin "../data/04.bin":$0B4A..$0B4C     ; 2 bytes  SPC $1022-$1023  null entry [25]
    dw orphan_slide_up2          ; 1024  [26]
    dw orphan_slide_down2        ; 1026  [27]
    incbin "../data/04.bin":$0B50..$0B56     ; 6 bytes  SPC $1028-$102D  null entries [28]-[2A]
    dw orphan_gate_on            ; 102E  [2B]
    dw orphan_gate_off           ; 1030  [2C]
    incbin "../data/04.bin":$0B5A..$0B60     ; 6 bytes  SPC $1032-$1037  null entries [2D]-[2F]
    dw seq_echo_off              ; 1038  [30]
    dw orphan_volume_preset2     ; 103A  [31]
    dw seq_echo_off              ; 103C  [32]

dsp_init: ; DSP reset, DIR = $31 ($3100), per-voice defaults, load 8 channel pointers from song header
    mov a,#$00                   ; 103E
    mov $EC,a                    ; 1040 pitch_offset
    mov $ED,a                    ; 1042
    mov $EE,a                    ; 1044 pitch_offset_prev
    mov $EF,a                    ; 1046
    mov !DSPADDR,#$6C            ; 1048 DSP FLG
    mov !DSPDATA,#$E0            ; 104B
    mov !DSPADDR,#$2C            ; 104E DSP EVOLL
    mov $0232,a                  ; 1051 evol_lr
    mov !DSPDATA,a               ; 1054
    mov !DSPADDR,#$3C            ; 1056 DSP EVOLR
    mov $0233,a                  ; 1059
    mov !DSPDATA,a               ; 105C
    mov !DSPADDR,#$0D            ; 105E DSP EFB
    mov !DSPDATA,a               ; 1061
    mov !DSPADDR,#$4C            ; 1063 DSP KON
    mov !DSPDATA,a               ; 1066
    mov !DSPADDR,#$5C            ; 1068 DSP KOFF
    mov !DSPDATA,#$FF            ; 106B
    mov !DSPADDR,#$2D            ; 106E DSP PMON
    mov !DSPDATA,a               ; 1071
    mov !DSPADDR,#$3D            ; 1073 DSP NON
    mov !DSPDATA,a               ; 1076
    mov !DSPADDR,#$4D            ; 1078 DSP EON
    mov !DSPDATA,a               ; 107B
    mov a,#$3C                   ; 107D
    mov $0230,a                  ; 107F mvol_lr
    mov $0231,a                  ; 1082
    mov !DSPADDR,#$0C            ; 1085 DSP MVOLL
    mov !DSPDATA,a               ; 1088
    mov !DSPADDR,#$1C            ; 108A DSP MVOLR
    mov !DSPDATA,a               ; 108D
    mov a,#$64                   ; 108F
    mov $04B6,a                  ; 1091 master_percent
    mov !DSPADDR,#$5D            ; 1094 DSP DIR
    mov !DSPDATA,#$31            ; 1097
    mov y,#$08                   ; 109A
    mov !DSPADDR,#$00            ; 109C DSP V0_VOL_L
loc_109F:
    mov a,#$7F                   ; 109F
    mov !DSPDATA,a               ; 10A1
    inc !DSPADDR                 ; 10A3
    mov !DSPDATA,a               ; 10A5
    clrc                         ; 10A7
    adc !DSPADDR,#$04            ; 10A8
    mov a,#$00                   ; 10AB
    mov !DSPDATA,a               ; 10AD
    inc !DSPADDR                 ; 10AF
    mov !DSPDATA,a               ; 10B1
    inc !DSPADDR                 ; 10B3
    mov !DSPDATA,#$FF            ; 10B5
    clrc                         ; 10B8
    adc !DSPADDR,#$09            ; 10B9
    dec y                        ; 10BC
    bne loc_109F                 ; 10BD
    mov $E7,#$FF                 ; 10BF var_E7
    mov $E8,#$FF                 ; 10C2 var_E8
    mov a,#$64                   ; 10C5
    mov $E4,a                    ; 10C7 t0_target
    mov a,#$20                   ; 10C9
    mov $04B5,a                  ; 10CB flg_echo_off
    mov $00,#$08                 ; 10CE tmp0
    mov x,#$00                   ; 10D1
    mov y,#$00                   ; 10D3
    mov $0A,y                    ; 10D5
    mov $04B4,y                  ; 10D7 noise_clock
    mov $01,y                    ; 10DA tmp1
loc_10DC:
    mov a,#$01                   ; 10DC
    mov $34+x,a                  ; 10DE duration[x]
    mov $0110+x,a                ; 10E0 chan_active[x]
    mov a,($E5)+y                ; 10E3 song_ptr
    mov $44+x,a                  ; 10E5 seq_ptr_lo[x]
    inc y                        ; 10E7
    mov a,($E5)+y                ; 10E8 song_ptr
    mov $54+x,a                  ; 10EA seq_ptr_hi[x]
    mov a,$01                    ; 10EC tmp1
    mov $D4+x,a                  ; 10EE seq_sp[x]
    mov a,#$00                   ; 10F0
    mov $01D0+x,a                ; 10F2 gate_mode[x]
    mov $24+x,a                  ; 10F5 gate[x]
    mov $0120+x,a                ; 10F7 note_len[x]
    mov $0130+x,a                ; 10FA note_gate[x]
    mov $0150+x,a                ; 10FD chan_flags[x]
    mov $0140+x,a                ; 1100 transpose[x]
    mov $64+x,a                  ; 1103 finetune[x]
    mov $01E0+x,a                ; 1105 sfx_override[x]
    mov $0294+x,a                ; 1108 echo_on[x]
    inc x                        ; 110B
    inc y                        ; 110C
    clrc                         ; 110D
    adc $01,#$08                 ; 110E tmp1
    dbnz $00,loc_10DC            ; 1111 tmp0
    mov a,($E5)+y                ; 1114 song_ptr
    mov $1F,a                    ; 1116 tempo
    inc y                        ; 1118
    mov a,($E5)+y                ; 1119 song_ptr
    mov $22,a                    ; 111B tempo2
    mov a,#$00                   ; 111D
    mov $1E,a                    ; 111F tempo_acc
    mov $21,a                    ; 1121 tempo_acc2

dsp_flg_20: ; FLG = $20 (echo write off)
    mov !DSPADDR,#$6C            ; 1123 DSP FLG
    mov !DSPDATA,#$20            ; 1126
    ret                          ; 1129


sfx_start: ; a = sfx id, x = channel: pointer from $2412 (id < $60) or $2E96 (id - $60), voice x|8
    push a                       ; 112A
    cmp a,#$60                   ; 112B
    bpl loc_1137                 ; 112D
    setc                         ; 112F
    sbc a,$2410                  ; 1130 sfx_bank1_count
    bpl loc_1140                 ; 1133
    bra loc_1144                 ; 1135

loc_1137:
    setc                         ; 1137
    sbc a,#$60                   ; 1138
    setc                         ; 113A
    sbc a,$2E94                  ; 113B sfx_bank2_count
    bmi loc_1144                 ; 113E
loc_1140:
    pop a                        ; 1140
    mov a,#$00                   ; 1141
    push a                       ; 1143
loc_1144:
    pop a                        ; 1144
    asl a                        ; 1145
    push a                       ; 1146
    mov a,#$01                   ; 1147
    mov $01E0+x,a                ; 1149 sfx_override[x]
    mov !DSPADDR,#$3D            ; 114C DSP NON
    mov a,$0FC8+x                ; 114F voice_bits
    eor a,#$FF                   ; 1152
    and a,!DSPDATA               ; 1154
    mov !DSPDATA,a               ; 1156
    mov a,x                      ; 1158
    clrc                         ; 1159
    adc a,#$08                   ; 115A
    mov x,a                      ; 115C
    asl a                        ; 115D
    asl a                        ; 115E
    asl a                        ; 115F
    mov $D4+x,a                  ; 1160 seq_sp[x]
    mov a,#$01                   ; 1162
    mov $0110+x,a                ; 1164 chan_active[x]
    dec a                        ; 1167
    mov $0120+x,a                ; 1168 note_len[x]
    mov $0130+x,a                ; 116B note_gate[x]
    mov $24+x,a                  ; 116E gate[x]
    mov $01D0+x,a                ; 1170 gate_mode[x]
    mov $01E0+x,a                ; 1173 sfx_override[x]
    mov $0150+x,a                ; 1176 chan_flags[x]
    mov $0140+x,a                ; 1179 transpose[x]
    mov $0294+x,a                ; 117C echo_on[x]
    mov $64+x,a                  ; 117F finetune[x]
    mov a,#$7F                   ; 1181
    mov $0254+x,a                ; 1183 vol_l[x]
    mov $0264+x,a                ; 1186 vol_r[x]
    mov $0314+x,a                ; 1189 sfx_vol_l[x]
    mov $0324+x,a                ; 118C sfx_vol_r[x]
    mov a,#$8E                   ; 118F
    mov $0274+x,a                ; 1191 adsr1[x]
    mov a,#$E0                   ; 1194
    mov $0284+x,a                ; 1196 adsr2[x]
    pop a                        ; 1199
    cmp a,#$C0                   ; 119A
    bcs loc_11AC                 ; 119C
    mov y,a                      ; 119E
    mov a,$2412+y                ; 119F sfx_bank1_ptrs (uploaded at spc_init from $C22E5C)
    mov $44+x,a                  ; 11A2 seq_ptr_lo[x]
    inc y                        ; 11A4
    mov a,$2412+y                ; 11A5 sfx_bank1_ptrs (uploaded at spc_init from $C22E5C)
    mov $54+x,a                  ; 11A8 seq_ptr_hi[x]
    bra loc_11BB                 ; 11AA

loc_11AC:
    setc                         ; 11AC
    sbc a,#$C0                   ; 11AD
    mov y,a                      ; 11AF
    mov a,$2E96+y                ; 11B0 sfx_bank2_ptrs (uploaded per command from data_C210EE)
    mov $44+x,a                  ; 11B3 seq_ptr_lo[x]
    inc y                        ; 11B5
    mov a,$2E96+y                ; 11B6 sfx_bank2_ptrs (uploaded per command from data_C210EE)
    mov $54+x,a                  ; 11B9 seq_ptr_hi[x]
loc_11BB:
    mov a,#$02                   ; 11BB
    mov $34+x,a                  ; 11BD duration[x]
    mov !DSPADDR,#$4D            ; 11BF DSP EON
    mov a,$0FC8+x                ; 11C2 voice_bits
    eor a,#$FF                   ; 11C5
    and a,!DSPDATA               ; 11C7
    mov !DSPDATA,a               ; 11C9
    ret                          ; 11CB


pitch_table: ; 98 words, DSP pitch per semitone, index = (note + $24 + transpose) * 2
    incbin "../data/04.bin":$0CF4..$0CF5     ; 1 byte  SPC $11CC-$11CC
data_11CD:
    incbin "../data/04.bin":$0CF5..$0DBA     ; 197 bytes  SPC $11CD-$1291
