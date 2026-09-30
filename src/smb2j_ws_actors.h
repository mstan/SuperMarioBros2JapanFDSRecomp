#pragma once
#include <stdint.h>

#include "smb2j_ws.h"

void smb_ws_actors_configure(int enabled, SmbEnemyMode mode);
void smb_ws_actors_reset(void);
/* Before a frame runs: the packets captured last frame are the ones the
 * picture this frame shows. */
void smb_ws_actors_begin_frame(void);
void smb_ws_actors_update(void);
void smb_ws_actors_draw(uint32_t *out, int width, int height, int native_x0, int render_camera, const uint8_t *opaque);
/* A picture is about to be composed (or not composed at all): the pixel
 * counts smb_ws_actors_json reports start again. */
void smb_ws_actors_compose_begin(void);
/* TCP (window builds): smb_ws_enemies, smb_ws_flag. */
void smb_ws_actors_tcp_setup(void);
/* JSON members (no braces) describing the residents and the goal flag, for
 * TCP and the per-frame widescreen log. Returns the length written. */
int smb_ws_actors_json(char *buf, int cap);
