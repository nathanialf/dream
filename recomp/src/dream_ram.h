/* RAM and hardware-register names for the port.
 *
 * The names and addresses are the ones in tools/names.txt and
 * docs/naming_proposals.md sections 7 and 8, so a C body and the corresponding
 * block in out/dream.asm read the same way. Entity fields are the flat 32-byte
 * struct-of-arrays columns of section 7: the index register X holds slot * 2,
 * so a field access is always ENTITY_<field> + X.
 */
#ifndef DREAM_RAM_H
#define DREAM_RAM_H

/* ---- direct page ------------------------------------------------------- */
#define nmi_handler_ptr      0x0000
#define dma_pending_mask     0x0002
#define ptr_04               0x0004
#define scratch_18           0x0018
#define entity_screen_x      0x004C
#define entity_screen_y      0x004E
#define depth_sort_key       0x0052   /* $52: scratch in entity_sort_draw_order */
#define camera_x             0x0062
#define camera_y             0x0068
#define camera_y_bias        0x0074
#define level_width_mask     0x0086
#define level_height_mask    0x0088
#define joy1_held            0x008A   /* $8A/$8C, $8E/$90: held / newly pressed */
#define joy1_pressed         0x008C
#define joy2_held            0x008E
#define joy2_pressed         0x0090
#define oam_write_ptr        0x0094
#define entity_render_index  0x0096
#define layer_parallax_mode  0x0098
#define camera_y_lookahead   0x009A
#define game_mode            0x00A4

/* ---- low WRAM ---------------------------------------------------------- */
#define oam_buffer           0x0200
#define oam_buffer_upper     0x0400

#define entity_type          0x0708
#define entity_state         0x0728
#define entity_hitstun_timer 0x0768
#define entity_flags         0x0788
#define entity_substate      0x07A8
#define entity_frame_id      0x07C8
#define entity_x             0x0828
#define entity_x_sub         0x0848
#define entity_vel_x         0x0868
#define entity_vel_x_target  0x0888
#define entity_y             0x08A8
#define entity_y_sub         0x08C8
#define entity_vel_y         0x0948
#define entity_depth_key     0x0988
#define entity_render_order  0x09A8
#define entity_anim_id       0x09E8
#define entity_anim_rate     0x0A68

#define entity_tile_job      0x0A8A   /* $0A8A..: 8-byte pending VRAM upload jobs */
#define cgram_queue_index    0x0B8A   /* $0B8A: queue depth, 8-byte records at $0B84 */
#define ground_probe_result  0x0BAE
#define player_attack_flag   0x0BB4
#define particle_table       0x7F0906 /* long: 40 words of particle slots */

/* ---- hardware registers ------------------------------------------------ */
#define INIDISP   0x2100
#define VMAIN     0x2115
#define VMADDL    0x2116
#define CGADD     0x2121
#define BG1HOFS   0x210D
#define BG1VOFS   0x210E
#define BG2HOFS   0x210F
#define BG2VOFS   0x2110
#define BG3HOFS   0x2111
#define BG3VOFS   0x2112
#define JOYSER0   0x4016
#define JOYSER1   0x4017
#define HVBJOY    0x4212
#define JOY1L     0x4218
#define JOY2L     0x421A
#define MDMAEN    0x420B
#define DMAP0     0x4300
#define A1TL0     0x4302
#define A1B0      0x4304
#define DASL0     0x4305
#define DASB0     0x4307
#define A2AL0     0x4308

#endif
