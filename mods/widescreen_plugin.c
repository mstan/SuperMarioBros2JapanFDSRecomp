/* The widescreen mod's trusted plugin: the Mods screen's selection turns it
 * on with its options; the reset callback restores stock presentation first
 * on every launch (runner/include/mod_runtime.h). */
#include "mod_runtime.h"
#include "smb2j_ws.h"

#include <stdio.h>

static const char PACKAGE[] = "super-mario-bros-2-japan-fds.enhancement.widescreen";

static void reset_widescreen(void) {
    smb2j_ws_set_mod_enabled(0);
}

static void activate_widescreen(void) {
    char aspect[32] = "fit", hud[32] = "edges", enemies[32] = "viewport", camera[32] = "edges";
    nes_mod_option_value(PACKAGE, "widescreen", "aspect", aspect, sizeof aspect);
    nes_mod_option_value(PACKAGE, "widescreen", "hud", hud, sizeof hud);
    nes_mod_option_value(PACKAGE, "widescreen", "enemy_activation", enemies, sizeof enemies);
    nes_mod_option_value(PACKAGE, "widescreen", "camera", camera, sizeof camera);
    smb2j_ws_configure(aspect, hud, enemies);
    smb2j_ws_set_camera(camera);
    smb2j_ws_set_mod_enabled(1);
}

NES_MOD_CONSTRUCTOR(register_smb2j_widescreen_plugin) {
    if (!nes_mod_register_reset_callback(reset_widescreen) ||
        !nes_mod_register_activation_plugin("super-mario-bros-2-japan-fds.widescreen", activate_widescreen))
        fprintf(stderr, "[Mods] Failed to register the SMB2J widescreen plugin\n");
}
