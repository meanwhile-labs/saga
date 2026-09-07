#include <stdio.h>
#include <string.h>

#include "gameapi/ai/aisys/aisys.h"
#include "decomp.h"
#include "legoapi/world/level.h"
#include "globals.h"
#include "legoapi/characters/core/players.h"
#include "legoapi/characters/motion.h"
#include "legoapi/characters/motion/gameanim.h"
#include "legoapi/gizmo/base/GizBlowupObjectInterface.h"
#include "legoapi/gizmo/base/GizForceObjectInterface.h"
#include "legoapi/gizmos/object/newblowup.h"
#include "legoapi/gizmos/object/gizobstacles.h"
#include "legoapi/gizmos/object/gizpanel.h"
#include "legoapi/gizmos/traps/gizforce.h"
#include "legoapi/gizmos/traps/gizturrets.h"
#include "legoapi/gizmos/trigger/gizaimessage.h"
#include "legoapi/ai/core/ai_sys_stubs.h"
#include "legoapi/audio/sfx.h"
#include "legoapi/cutscenes/cutscenes.h"
#include "legoapi/items/base/collection.h"
#include "legoapi/items/objects/gameobjects.h"
#include "legoapi/legoapi_types.h"
#include "legoapi/menus/core/gamemessage.h"
#include "legoapi/world/levels/levels.h"
#include "legoapi/world/mission.h"
#include "legoapi/render/core/render.h"
#include "nu2api/nucore/nugcutscene.h"
#include "nu2api/nu3d/nuspecial.h"
#include "nu2api/nu3d/nuspline.h"
#include "nu2api/nu3d/nutex.h"
#include "nu2api/numath/nufloat.h"
#include "nu2api/numath/numtx.h"
#include "nu2api/numath/nuang.h"
#include "nu2api/numath/nurand.h"
#include "nu2api/numath/nutrig.h"
// This level's view of the shared 16-byte LevFlag scratch. byte0 holds the
// bonus-gunship milestone state; byte1 a secondary state.
enum GUNSHIP_STATE_e {
    GUNSHIP_INACTIVE = 0, // never entered
    GUNSHIP_ACTIVE = 1,   // player aboard / stage running
    GUNSHIP_WON = 2,      // stage finished
};
struct GUNSHIP_LEVFLAG_s {
    u8 progress; // 0x00 -> GUNSHIP_STATE_e
    u8 exit;     // 0x01
    u8 pad[14];  // 0x02
};
static_assert(sizeof(struct GUNSHIP_LEVFLAG_s) == 16, "LevFlag must be 16 bytes");
extern struct GUNSHIP_LEVFLAG_s LevFlag;

// --- Cross-file entry points / id globals ---
//
// AIPAthFindPathCnx remains local here because it is called with a different
// argument arity than in episodeI (both are byte-matched as-is), so it cannot
// live in a shared header.

extern "C" {
    void *AIPAthFindPathCnx(AISYS_s *, i32, char *, char *, void *); // legoapi/ai pathfinding
    float FactoryBConveyorStopFrame = 28.0f;
    // Jedi_B arena tuning.
    f32 jedib_exclusion_r = 3.0f;
    f32 jedib_create_step_LowEnd = 3.0f;
    f32 jedib_create_step_Normal = 2.0f;
    f32 jedib_proximity = 0.5f;
    f32 jedib_offset = 0.5f;
    f32 jedib_inner = 3.0f;
    f32 jedib_safe_r = 9.0f;
    f32 jedib_outer_r = 8.0f;
    i32 jedib_max_baddies_per_goody = 3;
    i32 jedib_min_baddies_per_goody = 1;
    u32 jedib_seed = 17;
    void *jedib_netpacket;
    i32 jedib_n_active;
    i32 jedib_n_drawn;
    void AISysGetPathPos(AISYS_s *, NUVEC *, AIPATH_s **, i32, i32);
    // Gunship_B drag-bomb seek tuning.
    f32 gunshipb_seekmomseek = 5.0f;
    f32 gunshipb_seekmom = 5.0f;
    f32 gunshipb_seekrange = 4.0f;
    struct KAMINOC_NETPACKET_s *kaminoc_netpacket;
    void *disco_off_spina[3];
    nuhspecial_s disco_on_spin[3];
    nuhspecial_s walllights[2];
    nuhspecial_s walllights_disco[2];
    nuhspecial_s striplights[2];
    nuhspecial_s discolights[2];
    nuhspecial_s discorm_wall_on;
    nuhspecial_s discorm_wall_off;
    GIZMO_s *gizTurrets[2];
    i32 last;
    i32 FindPlatInst(i32 instance);
    void NuLgtLaser(i32, f32, f32, f32, NUVEC *, NUVEC *, u32, f32, f32);
}

NUGSPLINE *edSpline_SplineFind(nugscn_s *, char *);
void UpdatePaintPuzzle(WORLDINFO_s *);
GIZTURRET_s *GizTurret_FindByName(GIZTURRETSYS_s *, char *);

// --- File-local statics (original _ZL... symbols; not renamed) ---------------

// Conveyor speeds from the level config, parked here while Factory_B forces the
// belts to a stop; FactoryB_Update restores them.
static f32 FactoryBConveyorXSpeed;
static f32 FactoryBConveyorZSpeed;
struct KAMINODISCO_s {
    AIAREA_s *area;
    nuhspecial_s off[16];
    nuhspecial_s flash[16];
    nuhspecial_s select[16];
    nuhspecial_s finish[16];
    nuhspecial_s on[16];
    u8 tiles[16];
    i8 special_count;
    u8 state;
    i8 current_tile;
    i8 previous_tile;
    f32 timer;
    u8 completion_sound_played;
    u8 pad[3];
    GIZAIMESSAGE_s *next_tile_message;
    GIZAIMESSAGE_s *complete_message;
};
DECOMP_ASSERT(sizeof(KAMINODISCO_s) == 0x3e8, "Kamino disco state size");
static KAMINODISCO_s kaminodisco;
struct KAMINOC_NETPACKET_s {
    i16 off;
    i16 flash;
    i16 select;
    i16 finish;
    i16 on;
    i16 sounds;
    u8 complete;
    u8 pad;
};
DECOMP_ASSERT(sizeof(KAMINOC_NETPACKET_s) == 0xe, "Kamino C packet size");
// Dooku_C level state (original _ZL7dooku_c, one 20-byte .bss object).
struct DOOKUC_STATE_s {
    GIZAIMESSAGE_s *total; // 0x00
    GIZAIMESSAGE_s *hits;  // 0x04
    nuhspecial_s node;     // 0x08, force-back effect model
};
static struct DOOKUC_STATE_s dooku_c;

struct KAMINO_E_s {
    GIZAIMESSAGE_s *fight;
    GIZAIMESSAGE_s *can_fire;
    GIZAIMESSAGE_s *reset_turrets;
    GIZAIMESSAGE_s *show_hearts;
    GIZAIMESSAGE_s *mini_cut_started;
    CUTINFO *intro;
    AIAREA_s *landing_pad;
    nuhspecial_s special; // 0x1c, Slave I
    GIZTURRET_s *turrets[4];
    GIZTURRET_s *hit_turret;
    GIZPANEL_s *panels[4];
    NUVEC position;
    i32 pitch;
    i32 yaw;
    i32 roll;
    i32 orbit_pitch;
    i32 orbit_yaw;
    f32 timer;
    f32 elapsed;
    u8 gun;
    u8 state;
    u16 bolt_filter; // 0x76
};
DECOMP_ASSERT(sizeof(struct KAMINO_E_s) == 0x78, "Kamino E state size");
static struct KAMINO_E_s kamino_e;

// Episode 2 level handlers, in the game's Episode_II progression:
// pursuit (coruscant bounty-hunter) / kamino / factory (geonosis droid
// factory) / jedi / gunship / bonus gunship / dooku, then the NewTown bonus.
//
// The bounty-hunter pursuit and bonus-gunship functions came from a separate
// pursuit.cpp translation unit in the original binary (_GLOBAL__sub_I_pursuit.cpp);
// JediB currently lives here per the Episode II level grouping.

// ===========================================================================
// Coruscant — bounty-hunter pursuit (Zam Wesell)
// ===========================================================================

void BountyHunterPursuitA_Init(WORLDINFO_s *world) {
    GIZMOBLOWUP_s *b;
    if ((b = GizmoBlowUp_FindByName(world, "Jango")) != NULL)
        b->field_0x9f |= 0x20;
    if ((b = GizmoBlowUp_FindByName(world, "b1")) != NULL)
        b->field_0x9f |= 0x20;
    if ((b = GizmoBlowUp_FindByName(world, "b2")) != NULL)
        b->field_0x9f |= 0x20;
    if ((b = GizmoBlowUp_FindByName(world, "b3")) != NULL)
        b->field_0x9f |= 0x20;
    if ((b = GizmoBlowUp_FindByName(world, "b4")) != NULL)
        b->field_0x9f |= 0x20;
}

void BountyHunterPursuitB_Init(WORLDINFO_s *) {
}

void BountyHunterPursuitC_Init(WORLDINFO_s *) {
}

void BountyHunterPursuitD_Init(WORLDINFO_s *) {
}

void BountyHunterPursuitA_Reset(WORLDINFO_s *world) {
    memset(&zamarrow, 0, sizeof(zamarrow));
    zamarrow.target = GetNamedGameObject(world->ai_sys, "pursuit_a");
    GIZMOBLOWUP_s *b;
    if ((b = GizmoBlowUp_FindByName(world, "za1")) != NULL)
        b->field_0x9f |= 0x20;
    if ((b = GizmoBlowUp_FindByName(world, "za2")) != NULL)
        b->field_0x9f |= 0x20;
    if ((b = GizmoBlowUp_FindByName(world, "za3")) != NULL)
        b->field_0x9f |= 0x20;
    if ((b = GizmoBlowUp_FindByName(world, "za4")) != NULL)
        b->field_0x9f |= 0x20;
    if ((b = GizmoBlowUp_FindByName(world, "za5")) != NULL)
        b->field_0x9f |= 0x20;
}

void BountyHunterPursuitB_Reset(WORLDINFO_s *world) {
    LevGizmo[0] = GizmoFindByName(world->gizmo_sys, blowup_gizmotype_id, "generator1");
    LevGizmo[1] = GizmoFindByName(world->gizmo_sys, blowup_gizmotype_id, "generator2");
    LevGizmo[2] = GizmoFindByName(world->gizmo_sys, blowup_gizmotype_id, "generator3");
    LevGizmo[3] = GizmoFindByName(world->gizmo_sys, blowup_gizmotype_id, "generator4");
    LevGizmo[4] = GizmoFindByName(world->gizmo_sys, blowup_gizmotype_id, "generator5");
    LevGizmo[5] = GizmoFindByName(world->gizmo_sys, blowup_gizmotype_id, "generator6");
    LevGameObject[0] = GetNamedGameObject(world->ai_sys, "ai_zam");
    memset(&zamarrow, 0, sizeof(zamarrow));
    zamarrow.target = GetNamedGameObject(world->ai_sys, "ai_zam");
    LevAIMessage[0] = CheckGizAIMessage(gizaimessagesys, "CompletedB", NULL);
}

void BountyHunterPursuitC_Reset(WORLDINFO_s *world) {
    memset(&zamarrow, 0, sizeof(zamarrow));
    zamarrow.target = GetNamedGameObject(world->ai_sys, "ai_zam");
    TRAFFICANIMSYS_s *traffic = world->trafficanim_sys;
    if (traffic == NULL)
        return;
    traffic->side = 0;
    TRAFFICANIM_s *anim = traffic->anims;
    for (i32 i = 0; i < traffic->anim_count; i++, anim++) {
        if (pursuit_c_hack == 0) {
            anim->side = 0;
            continue;
        }
        // Sign the anim by where its end pose sits relative to traffic_test_z,
        // so the pursuit can hide the traffic on the player's own side.
        NUMTX end;
        NUMTX current;
        EvalAnim(&anim->special, 1.0f, &end, 1);
        EvalAnim(&anim->special, anim->anim_time, &current, 1);
        if (traffic_test_z > end.m32)
            anim->side = (traffic_test_z > current.m32) ? -1 : 0;
        else
            anim->side = (traffic_test_z < current.m32);
    }
}

void BountyHunterPursuitD_Reset(WORLDINFO_s *world) {
    LevGizmo[0] = GizmoFindByName(world->gizmo_sys, blowup_gizmotype_id, "generator1");
    LevGizmo[1] = GizmoFindByName(world->gizmo_sys, blowup_gizmotype_id, "generator2");
    LevGizmo[2] = GizmoFindByName(world->gizmo_sys, blowup_gizmotype_id, "generator3");
    LevGizmo[3] = GizmoFindByName(world->gizmo_sys, blowup_gizmotype_id, "generator4");
    LevGizmo[4] = GizmoFindByName(world->gizmo_sys, blowup_gizmotype_id, "generator5");
    LevGizmo[5] = GizmoFindByName(world->gizmo_sys, blowup_gizmotype_id, "generator6");
    LevGizmo[6] = GizmoFindByName(world->gizmo_sys, blowup_gizmotype_id, "generator7");
    LevGizmo[7] = GizmoFindByName(world->gizmo_sys, blowup_gizmotype_id, "generator8");
    LevGizmo[8] = GizmoFindByName(world->gizmo_sys, blowup_gizmotype_id, "generator9");
    LevGizmo[9] = GizmoFindByName(world->gizmo_sys, blowup_gizmotype_id, "generator10");
    LevGizmo[10] = GizmoFindByName(world->gizmo_sys, blowup_gizmotype_id, "generator11");
    LevGizmo[11] = GizmoFindByName(world->gizmo_sys, blowup_gizmotype_id, "generator12");
    LevGameObject[0] = GetNamedGameObject(world->ai_sys, "ai_zam");
}

static void UpdateZamArrow(WORLDINFO_s *world) {
    GameObject_s *target = zamarrow.target;
    if (target == NULL || (target->apiobj.field_0x1f8 & 0x1000) == 0 || target->apiobj.field_0x287 != 0)
        return;
    if (FadeSys.fade != 0.0f || pause_rndr_on != 0) {
        zamarrow.anim_time = 0.0f;
        return;
    }
    zamarrow.anim_time += FRAMETIME * 2.0f;
    if (zamarrow.anim_time > 1.0f)
        zamarrow.anim_time = 1.0f;
    if (NuFmod(GameTimer.time_elapsed_mod_seconds, 0.2f) < 0.1f) {
        NUVEC position = zamarrow.target->apiobj.upper_position;
        position.y += 1.0f;
        GAMEMESSAGE_s *message = static_cast<GAMEMESSAGE_s *>(
            AddGameMessage(" ", &position, 0.05f, NULL, 0.0f, 0xff, 0x3f, 0x3f, 0x10083, 0.0f));
        if (message != NULL) {
            message->icon = 0x134;
            i32 phase = static_cast<i32>(16384.0f * zamarrow.anim_time) >> 1;
            message->alpha = static_cast<u8>(128.0f * NuTrigTable[phase & 0x7fff]);
            const u8 *state = reinterpret_cast<const u8 *>(world->lev_objs);
            if (state[0x134e] != 0)
                memcpy(&message->color1, state + 0x1340, 3 * sizeof(u32));
        }
    }
}

void BountyHunterPursuitA_Update(WORLDINFO_s *world) {
    UpdateZamArrow(world);
}

void BountyHunterPursuitB_Update(WORLDINFO_s *world) {
    if (LevAIMessage[0] != NULL && LevAIMessage[0]->value == 1.0f)
        UpdateZamArrow(world);
}

void BountyHunterPursuitC_Update(WORLDINFO_s *world) {
    UpdateZamArrow(world);

    TRAFFICANIMSYS_s *traffic = world->trafficanim_sys;
    if (traffic == NULL || player == NULL)
        return;

    f32 test_z = traffic_test_z;
    if (test_z > player->apiobj.position.z) {
        if (traffic->side == -1)
            return;
        traffic->side = -1;
    } else {
        if (traffic->side == 1)
            return;
        traffic->side = 1;
    }

    TRAFFICANIM_s *anim = traffic->anims;
    for (i32 i = 0; i < traffic->anim_count; i++, anim++) {
        if (test_z > player->apiobj.position.z)
            anim->hidden = anim->side == 1;
        else
            anim->hidden = anim->side == -1;
    }
}

void BountyHunterPursuitD_Update(WORLDINFO_s *) {
    if (LevGameObject[0] == NULL || LevGameObject[0]->field_0xe37 == 0)
        return;
    GIZMOBLOWUP_s *nearest = NULL;
    f32 distance = 1000000000.0f;
    for (i32 i = 0; i < 12; i++) {
        if (LevGizmo[i] != NULL) {
            GIZMOBLOWUP_s *blowup = static_cast<GIZMOBLOWUP_s *>(LevGizmo[i]->object);
            if ((blowup->output_flags & 1) == 0) {
                f32 candidate = NuVecDistSqr(&blowup->position, &LevGameObject[0]->apiobj.collision_position, NULL);
                if (candidate < distance) {
                    nearest = blowup;
                    distance = candidate;
                }
            }
        }
    }
    if (nearest == NULL) {
        LevGameObject[0]->field_0xe37 = 0;
        DrawBossHitPoints(LevGameObject[0]);
        return;
    }
    if (distance < 1000000.0f) {
        NUVEC direction;
        f32 length = NuVecDist(&LevGameObject[0]->apiobj.collision_position, &nearest->position, &direction);
        NuLgtLaser(0, 1.0f, 1.0f, 0.01f, &nearest->position, &direction, 0xff808040, 1.5f, length);
    }
}

// ===========================================================================
// Kamino
// ===========================================================================

i32 KaminoInside() {
    if (WORLD->area != NULL && WORLD->area == KAMINO_ADATA) {
        if (WORLD->current_level == KAMINOA_LDATA) {
            if (CUTSTOPGAME == 0) {
                if (GameCam->sock_position.location.sock == 5)
                    return 1;
                if (GameCam->sock_position.location.sock == 6)
                    return 1;
            } else {
                return 1;
            }
        } else if (WORLD->current_level == KAMINOE_LDATA) {
            if (CUTSTOPGAME == 0 && GameCam->sock_position.location.sock != 0x1e)
                return 1;
        }
    }
    return 0;
}

i32 KaminoDiscoOn() {
    return kaminodisco.state == 2;
}

i32 KaminoInDiscoRoom() {
    i32 r = 0;
    if (WORLD->current_level == KAMINOC_LDATA)
        r = (GameCam->sock_position.location.sock == 0x15);
    return r;
}

void KaminoA_AlwaysUpdate(WORLDINFO_s *) {
    bool v = 0;
    if (CUTSTOPGAME == 0) {
        u8 b = GameCam->sock_position.location.sock;
        if (b != 5)
            v = (b != 6);
    }
    object_switches[1] = v;
}

void KaminoC_Init(WORLDINFO_s *world) {
    memset(&kaminodisco, 0, sizeof(kaminodisco));
    kaminoc_netpacket = static_cast<KAMINOC_NETPACKET_s *>(SetLevelHack(sizeof(KAMINOC_NETPACKET_s)));
    char name[32];
    for (kaminodisco.special_count = 0; kaminodisco.special_count < 16; kaminodisco.special_count++) {
        if (kaminodisco.special_count < 9)
            sprintf(name, "dot_off_0%d", kaminodisco.special_count + 1);
        else
            sprintf(name, "dot_off_%d", kaminodisco.special_count + 1);
        NuSpecialFind(WORLD->current_gscn, &kaminodisco.off[kaminodisco.special_count], name, 1);
        if (kaminodisco.special_count < 9)
            sprintf(name, "dot_flash_0%d", kaminodisco.special_count + 1);
        else
            sprintf(name, "dot_flash_%d", kaminodisco.special_count + 1);
        NuSpecialFind(WORLD->current_gscn, &kaminodisco.flash[kaminodisco.special_count], name, 1);
        if (kaminodisco.special_count < 9)
            sprintf(name, "dot_select_0%d", kaminodisco.special_count + 1);
        else
            sprintf(name, "dot_select_%d", kaminodisco.special_count + 1);
        NuSpecialFind(WORLD->current_gscn, &kaminodisco.select[kaminodisco.special_count], name, 1);
        if (kaminodisco.special_count < 9)
            sprintf(name, "dot_finish_0%d", kaminodisco.special_count + 1);
        else
            sprintf(name, "dot_finish_%d", kaminodisco.special_count + 1);
        NuSpecialFind(WORLD->current_gscn, &kaminodisco.finish[kaminodisco.special_count], name, 1);
        if (kaminodisco.special_count < 9)
            sprintf(name, "dot_on_0%d", kaminodisco.special_count + 1);
        else
            sprintf(name, "dot_on_%d", kaminodisco.special_count + 1);
        NuSpecialFind(WORLD->current_gscn, &kaminodisco.on[kaminodisco.special_count], name, 1);
        if (!NuSpecialExistsFn(&kaminodisco.off[kaminodisco.special_count]) ||
            !NuSpecialExistsFn(&kaminodisco.flash[kaminodisco.special_count]) ||
            !NuSpecialExistsFn(&kaminodisco.select[kaminodisco.special_count]) ||
            !NuSpecialExistsFn(&kaminodisco.finish[kaminodisco.special_count]))
            break;
    }
    kaminodisco.area = AISysFindArea(WORLD->ai_sys, "DISCO");
    NuSpecialFind(WORLD->current_gscn, &walllights[0], "walllights1", 1);
    NuSpecialFind(WORLD->current_gscn, &walllights[1], "walllights2", 1);
    NuSpecialFind(WORLD->current_gscn, &walllights_disco[0], "walllights1_disco", 1);
    NuSpecialFind(WORLD->current_gscn, &walllights_disco[1], "walllights2_disco", 1);
    NuSpecialFind(WORLD->current_gscn, &striplights[0], "striplights1", 1);
    NuSpecialFind(WORLD->current_gscn, &striplights[1], "striplights1b", 1);
    NuSpecialFind(WORLD->current_gscn, &discolights[0], "discolight1", 1);
    NuSpecialFind(WORLD->current_gscn, &discolights[1], "discolight2", 1);
    NuSpecialFind(WORLD->current_gscn, &discorm_wall_on, "discorm_wall_on", 1);
    NuSpecialFind(WORLD->current_gscn, &discorm_wall_off, "discorm_wall_off", 1);
    for (i32 i = 0; i < 3; i++) {
        sprintf(name, "disco_on_spin%d", i + 3);
        NuSpecialFind(WORLD->current_gscn, &disco_on_spin[i], name, 1);
        if (netclient == 0) {
            sprintf(name, "disco_off%d", i + 1);
            disco_off_spina[i] = GizmoFindByName(world->gizmo_sys, force_gizmotype_id, name);
        }
    }
    if (netclient == 0) {
        gizTurrets[0] = GizmoFindByName(world->gizmo_sys, turret_gizmotype_id, "turret01");
        gizTurrets[1] = GizmoFindByName(world->gizmo_sys, turret_gizmotype_id, "turret02");
        GIZTURRET_s *turret = GizTurret_FindByName(world->giz_turret_sys, "turret01");
        if (turret != NULL)
            turret->field_0x140 = 0.6f;
        turret = GizTurret_FindByName(world->giz_turret_sys, "turret02");
        if (turret != NULL)
            turret->field_0x140 = 0.6f;
        LevGizmo[0] = GizmoFindByName(world->gizmo_sys, gizaimessage_gizmotype_id, "msg_KaminoCProgress");
        LevGizmo[1] = GizmoFindByName(world->gizmo_sys, obstacle_gizmotype_id, "JANGOFIELD01");
    }
    last = 0;
}

void KaminoC_Reset(WORLDINFO_s *world) {
    memset(kaminodisco.tiles, 0, sizeof(kaminodisco.tiles));
    kaminodisco.state = 0;
    kaminodisco.current_tile = -1;
    kaminodisco.previous_tile = -1;
    kaminodisco.timer = 0.0f;
    kaminodisco.completion_sound_played = 0;
    kaminoc_netpacket->flash = 0;
    kaminoc_netpacket->finish = 0;
    kaminoc_netpacket->select = 0;
    kaminoc_netpacket->on = 0;
    kaminoc_netpacket->off = -1;
    kaminoc_netpacket->complete = 0;
    for (i32 i = 0; i < kaminodisco.special_count; i++) {
        NuSpecialSetVisibility(&kaminodisco.off[i], 1);
        NuSpecialSetVisibility(&kaminodisco.flash[i], 0);
        NuSpecialSetVisibility(&kaminodisco.finish[i], 0);
    }
    kaminodisco.next_tile_message = SetGizAIMessage(gizaimessagesys, "NextDiscoTile", 0.0f, NULL);
    kaminodisco.complete_message = SetGizAIMessage(gizaimessagesys, "DiscoComplete", 0.0f, NULL);
    for (i32 i = 0; i < 3; i++) {
        NuSpecialSetVisibility(&disco_on_spin[i], 0);
        if (netclient == 0) {
            GIZMO_s *gizmo = static_cast<GIZMO_s *>(disco_off_spina[i]);
            GizmoSetVisibility(world->gizmo_sys, gizmo, 1, 0);
            gizmo = static_cast<GIZMO_s *>(disco_off_spina[i]);
            if (static_cast<GIZFORCE_s *>(gizmo->object)->anim_set->state == GAMEANIMSET_STATE_AT_START)
                GizmoActivate(world->gizmo_sys, gizmo, 1, 0);
        }
    }
}

static i32 KaminoC_ChooseTile(i32 excluded, u8 state) {
    i32 candidates[16];
    i32 count = 0;
    for (i32 i = 0; i < kaminodisco.special_count; i++) {
        if (i != excluded && kaminodisco.tiles[i] == state)
            candidates[count++] = i;
    }
    if (count == 0)
        return -1;
    return candidates[NuRand(0) % count];
}

static bool KaminoC_PlayerInArea(WORLDINFO_s *world) {
    AISYS_s *ai = world->ai_sys;
    if ((ai->player_1 == NULL && ai->player_2 == NULL) || kaminodisco.area == NULL)
        return false;
    i64 mask = 1 << (kaminodisco.area - ai->areas);
    GameObject_s *object = reinterpret_cast<GameObject_s *>(ai->player_1);
    return ((object->ai_area_mask_low & static_cast<u32>(mask)) |
            (object->ai_area_mask_high & static_cast<u32>(mask >> 32))) != 0;
}

static inline bool KaminoC_CheckTile(i8 tile, i32 *player_tile) {
    u8 previous = kaminodisco.tiles[tile];
    kaminodisco.tiles[tile] = 1;
    NUVEC *position = static_cast<NUVEC *>(NuSpecialGetPos(&kaminodisco.flash[tile]));
    GameObject_s *object = NULL;
    for (i32 i = 0; i < 8; i++) {
        GameObject_s *candidate = Player[i];
        if (candidate != NULL && (candidate->apiobj.field_0x1f8 & 0x1001) == 0x1001 &&
            candidate->apiobj.field_0x27d != 0) {
            f32 x = position->x - candidate->apiobj.position.x;
            f32 y = position->y - candidate->apiobj.position.y;
            f32 z = position->z - candidate->apiobj.position.z;
            if (x * x + y * y + z * z < 0.2f * 0.2f) {
                object = candidate;
                break;
            }
        }
    }
    if (object != NULL) {
        if (object == player)
            *player_tile = tile;
        kaminodisco.tiles[tile] = 2;
    }
    if (previous != kaminodisco.tiles[tile] && kaminodisco.tiles[tile] == 2)
        kaminoc_netpacket->sounds |= 1 << tile;
    return object != NULL;
}

void KaminoC_Update(WORLDINFO_s *world) {
    const u8 *progress = static_cast<const u8 *>(LevGizmo[0]->object);
    if (GizmoGetOutput(world->gizmo_sys, LevGizmo[0], 0, 0)) {
        GizmoSetVisibility(world->gizmo_sys, LevGizmo[1], 0, 1);
    } else if ((progress[0x98] & 1) == 0) {
        GizmoActivate(world->gizmo_sys, LevGizmo[1], 1, 1);
        if (GizmoGetOutput(world->gizmo_sys, gizTurrets[0], 0, 0) &&
            GizmoGetOutput(world->gizmo_sys, gizTurrets[1], 0, 0))
            GizmoSetVisibility(world->gizmo_sys, LevGizmo[1], 0, 1);
    }
    kaminoc_netpacket->sounds = 0;
    SetGizAIMessage(gizaimessagesys, "NextDiscoTile", 0.0f, kaminodisco.next_tile_message);
    SetGizAIMessage(gizaimessagesys, "DiscoComplete", 0.0f, kaminodisco.complete_message);
    switch (kaminodisco.state) {
    case 0:
        if (KaminoC_PlayerInArea(world)) {
            kaminoc_netpacket->complete = 0;
            kaminodisco.state = 1;
            kaminodisco.timer = 0.0f;
            kaminodisco.current_tile = KaminoC_ChooseTile(-1, 0);
            kaminodisco.previous_tile = KaminoC_ChooseTile(kaminodisco.current_tile, 0);
            if (kaminodisco.current_tile != -1 && kaminodisco.previous_tile != -1) {
                kaminodisco.tiles[kaminodisco.current_tile] = 1;
                kaminodisco.tiles[kaminodisco.previous_tile] = 1;
            }
        }
        break;
    case 1: {
        i32 player_tile = -1;
        bool first = KaminoC_CheckTile(kaminodisco.current_tile, &player_tile);
        bool second = KaminoC_CheckTile(kaminodisco.previous_tile, &player_tile);
        if (first && second) {
            kaminodisco.timer = 0.0f;
            kaminodisco.tiles[kaminodisco.current_tile] = 3;
            kaminodisco.tiles[kaminodisco.previous_tile] = 3;
            kaminodisco.current_tile = KaminoC_ChooseTile(-1, 0);
            kaminodisco.previous_tile = KaminoC_ChooseTile(kaminodisco.current_tile, 0);
            if (kaminodisco.current_tile != -1 && kaminodisco.previous_tile != -1) {
                kaminodisco.tiles[kaminodisco.current_tile] = 1;
                kaminodisco.tiles[kaminodisco.previous_tile] = 1;
            } else {
                kaminodisco.state = 2;
                for (i32 i = 0; i < kaminodisco.special_count; i++)
                    kaminodisco.tiles[i] = 0;
            }
        } else {
            kaminodisco.timer += FRAMETIME;
            if (kaminodisco.timer > 2.5f) {
                kaminodisco.timer = 0.0f;
                for (i32 i = 0; i < 2; i++) {
                    i32 tile = KaminoC_ChooseTile(-1, 3);
                    if (tile != -1)
                        kaminodisco.tiles[tile] = 0;
                }
            }
            if (player2 == NULL) {
                GameObject_s *companion = Player[0];
                if (companion == player)
                    companion = Player[1];
                if (companion != NULL) {
                    i32 next = -1;
                    if (player_tile == kaminodisco.current_tile)
                        next = kaminodisco.previous_tile;
                    else if (player_tile == kaminodisco.previous_tile)
                        next = kaminodisco.current_tile;
                    if (next != -1)
                        SetGizAIMessage(gizaimessagesys, "NextDiscoTile", static_cast<f32>(next + 1),
                                        kaminodisco.next_tile_message);
                }
            }
        }
        break;
    }
    case 2:
        SetGizAIMessage(gizaimessagesys, "DiscoComplete", 1.0f, kaminodisco.complete_message);
        kaminoc_netpacket->complete = 1;
        if (!KaminoC_PlayerInArea(world)) {
            KaminoC_Reset(world);
            return;
        }
        for (i32 i = 0; i < kaminodisco.special_count; i++)
            kaminodisco.tiles[i] = 4;
        kaminodisco.timer += FRAMETIME;
        if (kaminodisco.timer > 20.0f) {
            KaminoC_Reset(world);
            return;
        }
        break;
    }
    kaminoc_netpacket->on = 0;
    kaminoc_netpacket->off = 0;
    kaminoc_netpacket->flash = 0;
    kaminoc_netpacket->select = 0;
    kaminoc_netpacket->finish = 0;
    for (i32 i = 0; i < kaminodisco.special_count; i++) {
        switch (kaminodisco.tiles[i]) {
        case 0: kaminoc_netpacket->off |= 1 << i; break;
        case 1: kaminoc_netpacket->flash |= 1 << i; break;
        case 2: kaminoc_netpacket->select |= 1 << i; break;
        case 3: kaminoc_netpacket->on |= 1 << i; break;
        case 4: kaminoc_netpacket->finish |= 1 << i; break;
        }
    }
    for (i32 i = 0; i < kaminodisco.special_count; i++) {
        NuSpecialSetVisibility(&kaminodisco.off[i], kaminoc_netpacket->off & (1 << i));
        NuSpecialSetVisibility(&kaminodisco.flash[i], kaminoc_netpacket->flash & (1 << i));
        NuSpecialSetVisibility(&kaminodisco.select[i], kaminoc_netpacket->select & (1 << i));
        NuSpecialSetVisibility(&kaminodisco.on[i], kaminoc_netpacket->on & (1 << i));
        NuSpecialSetVisibility(&kaminodisco.finish[i], kaminoc_netpacket->finish & (1 << i));
        if ((kaminoc_netpacket->sounds & (1 << i)) != 0) {
            NUMTX *matrix = static_cast<NUMTX *>(NuSpecialGetDrawMtx(&kaminodisco.finish[i]));
            PlaySfx("Kam_DiscoFloorPanelOn", reinterpret_cast<NUVEC *>(&matrix->m30));
        }
    }
    if (kaminoc_netpacket->complete == 0) {
        for (i32 i = 0; i < 3; i++) {
            NuSpecialSetVisibility(&disco_on_spin[i], 0);
            GizmoSetVisibility(world->gizmo_sys, static_cast<GIZMO_s *>(disco_off_spina[i]), 1, 0);
        }
        NuSpecialSetVisibility(&walllights_disco[0], 0);
        NuSpecialSetVisibility(&walllights_disco[1], 0);
        NuSpecialSetVisibility(&walllights[0], 1);
        NuSpecialSetVisibility(&walllights[1], 1);
        NuSpecialSetVisibility(&striplights[0], 1);
        NuSpecialSetVisibility(&striplights[1], 0);
        NuSpecialSetVisibility(&discolights[0], 0);
        NuSpecialSetVisibility(&discolights[1], 0);
        NuSpecialSetVisibility(&discorm_wall_on, 0);
        NuSpecialSetVisibility(&discorm_wall_off, 1);
    } else {
        for (i32 i = 0; i < 3; i++) {
            NuSpecialSetVisibility(&disco_on_spin[i], 1);
            GizmoSetVisibility(world->gizmo_sys, static_cast<GIZMO_s *>(disco_off_spina[i]), 0, 0);
        }
        NuSpecialSetVisibility(&walllights_disco[0], 1);
        NuSpecialSetVisibility(&walllights_disco[1], 1);
        NuSpecialSetVisibility(&walllights[0], 0);
        NuSpecialSetVisibility(&walllights[1], 0);
        NuSpecialSetVisibility(&striplights[0], 0);
        NuSpecialSetVisibility(&striplights[1], 1);
        NuSpecialSetVisibility(&discolights[0], 1);
        NuSpecialSetVisibility(&discolights[1], 1);
        NuSpecialSetVisibility(&discorm_wall_off, 0);
        NuSpecialSetVisibility(&discorm_wall_on, 1);
        if (kaminodisco.completion_sound_played == 0) {
            PlaySfx("Kam_DiscoFloorPanelDone", NuSpecialGetDrawPos(&kaminodisco.off[3]));
            kaminodisco.completion_sound_played = 1;
        }
    }
}

void KaminoD_Init(WORLDINFO_s *world) {
    for (i32 i = 1; i < 13; i++) {
        char buf[0x10];
        sprintf(buf, "DOT%i", i);
        GIZOBSTACLE_s *g = GizObstacle_FindByName(world->giz_obstacle_sys, buf);
        if (g->field_0x3c == 4.5f) {
            g->field_0x3c = 13.5f;
        }
    }

    GIZMOBLOWUP_s *target = GizmoBlowUp_FindByName(world, "target_a11");
    if (target != NULL) {
        target->field_0x124 = 1;
    }
}

void KaminoE_Init(WORLDINFO_s *world) {
    kaminoe_netpacket = SetLevelHack(0x14);
    GIZMO_s *g = GizmoFindByName(world->gizmo_sys, force_gizmotype_id, "Force");
    if (g != NULL)
        LevForce = *(i32 *)g;
}

void KaminoE_Reset(WORLDINFO_s *world) {
    memset(&kamino_e, 0, sizeof(kamino_e));
    kamino_e.landing_pad = AISysFindArea(WORLD->ai_sys, "landing_pad");
    if (NuSpecialFind(world->current_gscn, &kamino_e.special, "slave1", 1))
        kamino_e.bolt_filter = FindPlatInst(NuSpecialGetInstanceix(&kamino_e.special));
    char name[16];
    for (i32 i = 0; i < 4; i++) {
        sprintf(name, "turret%d", i + 1);
        GIZMO_s *gizmo = GizmoFindByName(world->gizmo_sys, turret_gizmotype_id, name);
        if (gizmo != NULL) {
            GIZTURRET_s *turret = static_cast<GIZTURRET_s *>(gizmo->object);
            kamino_e.turrets[i] = turret;
            turret->target_position = &kamino_e.position;
            turret->field_0x12c = 2;
            turret->flags |= 1;
        }
        sprintf(name, "R4_t%d", i + 1);
        gizmo = GizmoFindByName(world->gizmo_sys, gizpanel_gizmotype_id, name);
        if (gizmo != NULL)
            kamino_e.panels[i] = static_cast<GIZPANEL_s *>(gizmo->object);
    }
    kamino_e.fight = SetGizAIMessage(gizaimessagesys, "JangoFight", 0.0f, NULL);
    kamino_e.can_fire = SetGizAIMessage(gizaimessagesys, "Slave1CanFire", 0.0f, NULL);
    kamino_e.reset_turrets = SetGizAIMessage(gizaimessagesys, "ResetTurrets", 0.0f, NULL);
    kamino_e.show_hearts = CheckGizAIMessage(gizaimessagesys, "ShowHearts", NULL);
    kamino_e.mini_cut_started = CheckGizAIMessage(gizaimessagesys, "MiniCutStarted", NULL);
    CutScene_Find(world->cutscene_sys, "Ep2_Kamino_Intro2");
    kamino_e.intro = CutScene_Find(world->cutscene_sys, "Ep2_Kamino_Intro2");
}

void KaminoE_Update(WORLDINFO_s *) {
}

void KaminoE_AlwaysUpdate(WORLDINFO_s *) {
    bool v = 1;
    if (CUTSTOPGAME == 0)
        v = (GameCam->sock_position.location.sock == 0x1e);
    object_switches[1] = v;
}

void KaminoE_Draw(WORLDINFO_s *world) {
    if (netclient == 0) {
        if (kamino_e.show_hearts->value > 0.0f) {
            GameObject_s *obj = (GameObject_s *)FindGameObject((i32)(i16)id_JANGOFETT, 1, 1, 1, 0);
            if (obj != NULL && kamino_e.show_hearts != NULL && kamino_e.show_hearts->value == 1.0f)
                DrawBossHitPoints(obj);
        }
    }
    NuSpecialSetDrawMtx(&kamino_e.special, NuSpecialGetDrawMtx(&kamino_e.special));
    NuSpecialSetVisibility(&kamino_e.special, 1);
}

i32 KaminoE_CheckPlatHit(BOLT_s *bolt) {
    if (bolt->hit_platform_id != kamino_e.bolt_filter)
        return 0;
    for (i32 i = 0; i < 4; i++) {
        GIZTURRET_s *turret = kamino_e.turrets[i];
        if (turret != NULL && kamino_e.panels[i] != NULL &&
            (turret->flags & 0x32) == 2 && kamino_e.hit_turret == NULL)
            kamino_e.hit_turret = turret;
    }
    return 1;
}

void KaminoF_Init(WORLDINFO_s *world) {
    GIZMOBLOWUP_s *b;
    if ((b = GizmoBlowUp_FindByName(world, "f1")) != NULL) {
        b->field_0x128 = 0.3f;
        b->field_0x124 = 1;
    }
    if ((b = GizmoBlowUp_FindByName(world, "f2")) != NULL) {
        b->field_0x128 = 0.3f;
        b->field_0x124 = 1;
    }
    if ((b = GizmoBlowUp_FindByName(world, "f3")) != NULL) {
        b->field_0x128 = 0.3f;
        b->field_0x124 = 1;
    }
}

void KaminoOutro_Init(WORLDINFO_s *) {
    bool v = 0;
    if (CUTSTOPGAME == 0) {
        u8 b = GameCam->sock_position.location.sock;
        if (b != 5)
            v = (b != 6);
    }
    object_switches[1] = v;
}

void NbKaminoA_Init(WORLDINFO_s *world) {
    GIZMOBLOWUP_s *b;
    if ((b = GizmoBlowUp_FindByName(world, "nb1")) != NULL) {
        b->field_0x128 = 1.0f;
        b->field_0x124 = 1;
    }
    if ((b = GizmoBlowUp_FindByName(world, "nb2")) != NULL) {
        b->field_0x128 = 1.0f;
        b->field_0x124 = 1;
    }
}

// ===========================================================================
// Geonosis — droid factory (Factory_B / Factory_G)
// ===========================================================================

void FactoryB_Init(WORLDINFO_s *world) {
    factoryb_netpacket = SetLevelHack(0x4);
    InitPaintPuzzle(world);
    // First entry captures the config speeds; a restart must not re-capture the
    // zeroes this function leaves behind.
    if (FactoryBConveyorXSpeed == 0.0f && FactoryBConveyorZSpeed == 0.0f) {
        FactoryBConveyorXSpeed = world->current_level->conveyor_x_speed;
        FactoryBConveyorZSpeed = world->current_level->conveyor_z_speed;
    }
    world->current_level->conveyor_x_speed = 0.0f;
    world->current_level->conveyor_z_speed = 0.0f;
    LevGizObst[0] = GizObstacle_FindByName(world->giz_obstacle_sys, "obstacle49");
    LevGizObst[1] = GizObstacle_FindByName(world->giz_obstacle_sys, "obstacle50");
    LevGizObst[2] = GizObstacle_FindByName(world->giz_obstacle_sys, "obstacle48");
    LevGizObst[3] = GizObstacle_FindByName(world->giz_obstacle_sys, "obstacle52");
    LevGizObst[4] = GizObstacle_FindByName(world->giz_obstacle_sys, "obstacle47");
    LevGizObst[5] = GizObstacle_FindByName(world->giz_obstacle_sys, "obstacle46");
    LevGizObst[6] = GizObstacle_FindByName(world->giz_obstacle_sys, "obstacle45");
    LevGizObst[7] = GizObstacle_FindByName(world->giz_obstacle_sys, "obstacle51");
    for (i32 i = 0; i < 8; i++) {
        if (LevGizObst[i] != NULL) {
            LevGizObst[i]->mode = 3;
            LevGizObst[i]->state = 0;
        }
    }
}

void FactoryB_Reset(WORLDINFO_s *world) {
    ResetPaintPuzzle(world);
    CUTINFO *cut = NewCutScene(NULL, world->cutscene_sys, "fb_cut", 0);
    factoryb_cut = cut;
    if (cut != NULL && cut->instance != NULL) {
        // Bit 2 marks the instance finished so the cutscene updater skips it.
        reinterpret_cast<instNUGCUTSCENE_s *>(cut->instance)->flags_88 |= 2;
        reinterpret_cast<instNUGCUTSCENE_s *>(cut->instance)->flags_88 |= 8;
    }
    factoryb_conveyor_stopped_msg = CheckGizAIMessage(gizaimessagesys, "conv_stopped", NULL);
}

void FactoryB_Update(WORLDINFO_s *world) {
    UpdatePaintPuzzle(world);

    GIZAIMESSAGE_s *stopped = static_cast<GIZAIMESSAGE_s *>(factoryb_conveyor_stopped_msg);
    if (stopped != NULL && stopped->value != 0.0f && factoryb_cut->previous_frame > 50.0f &&
        factoryb_cut->previous_frame < 54.0f) {
        instNUGCUTSCENE_s *instance = static_cast<instNUGCUTSCENE_s *>(factoryb_cut->instance);
        instance->current_frame = 0.0f;
        instance->flags_88 &= ~2U;
        world->current_level->conveyor_x_speed = 0.0f;
        world->current_level->conveyor_z_speed = 0.0f;
    } else if (static_cast<instNUGCUTSCENE_s *>(factoryb_cut->instance)->current_frame >
               FactoryBConveyorStopFrame) {
        world->current_level->conveyor_x_speed = FactoryBConveyorXSpeed;
        world->current_level->conveyor_z_speed = FactoryBConveyorZSpeed;
        PlaySfx("FacB_BeltLp", NULL);
    } else {
        world->current_level->conveyor_x_speed = 0.0f;
        world->current_level->conveyor_z_speed = 0.0f;
    }

    f32 phase = NuFmod(GameTimer.time_elapsed, 25.0f) / 25.0f;
    i32 pattern;
    if (phase < 0.25f)
        pattern = 0x70;
    else if (phase < 0.5f)
        pattern = 0x15;
    else if (phase < 0.75f)
        pattern = 0xa8;
    else
        pattern = 0x0e;

    for (i32 i = 0; i < 8; i++) {
        GIZOBSTACLE_s *obstacle = LevGizObst[i];
        if (obstacle == NULL)
            continue;
        if ((pattern & (1 << i)) != 0) {
            if (obstacle->anim_set->state != GAMEANIMSET_STATE_AT_END)
                GizObstacle_PlayForwards(obstacle);
        } else if (obstacle->anim_set->state != GAMEANIMSET_STATE_AT_START) {
            GizObstacle_PlayBackwards(obstacle);
        }
    }
}

void FactoryB_Draw(WORLDINFO_s *) {
    DrawPaintLights();
}

void FactoryG_Init(WORLDINFO_s *world) {
    if (netclient != 0)
        return;
    GIZMO *g = GizmoFindByName(world->gizmo_sys, force_gizmotype_id, "force_g1");
    if (g != NULL)
        force_array[0] = (GIZFORCE_s *)g->object;
    g = GizmoFindByName(world->gizmo_sys, force_gizmotype_id, "force_g2");
    if (g != NULL)
        force_array[1] = (GIZFORCE_s *)g->object;
    g = GizmoFindByName(world->gizmo_sys, force_gizmotype_id, "force_g3");
    if (g != NULL)
        force_array[2] = (GIZFORCE_s *)g->object;
    g = GizmoFindByName(world->gizmo_sys, force_gizmotype_id, "force_g4");
    if (g != NULL)
        force_array[3] = (GIZFORCE_s *)g->object;
}

void FactoryG_Update(WORLDINFO_s *world) {
    if (netclient != 0)
        return;
    i32 complete = 0;
    for (i32 i = 0; i < 4; i++) {
        if (GizForce_Complete(force_array[i]))
            complete++;
    }
    if (ObiWan == NULL) {
        ObiWan = (GameObject_s *)FindGameObject((i32)(i16)id_OBIWANKENOBIJEDIMASTER, 0x400, 0, 1, 0);
        return;
    }
    if (complete != 4) {
        // Until all four force platforms are held, Obi-Wan is pinned to his
        // scripted spot and turned by a 2-second sine sweep of +/-30 degrees
        // about the quarter turn, so he keeps scanning instead of standing still.
        ObiWan->apiobj.position = {112.76f, 0.75f, -10.5f};
        const u16 sweep_phase = static_cast<u16>(NuFmod(GameTimer.time_elapsed, 2.0f) / 2.0f * 65536.0f);
        ObiWan->apiobj.field_0x276 =
            static_cast<u16>(static_cast<i32>(NU_SIN_LUT(sweep_phase) * 30.0f * (65536.0f / 360.0f))) - NUANG_90DEG;
    } else if (FreePlay != 0) {
        CompleteLevel(WORLD);
    } else {
        // Bit 2 marks the instance finished so the cutscene updater skips it.
        reinterpret_cast<instNUGCUTSCENE_s *>(NewCutScene(NULL, world->cutscene_sys, "ep2_factory_outro", 1)->instance)
            ->flags_88 |= 2;
    }
}

// ===========================================================================
// Jedi (Jedi_B)
// ===========================================================================

// One Jedi_B arena slot. Slot 0 of a cluster is the "goody" the player must
// reach; the slots that follow it are the baddies placed around it.
struct JEDIB_SPAWN_s {
    NUVEC position;      // 0x00
    i32 angle;           // 0x0c, orbit angle of a baddie around its goody
    u8 filler_0x10[0x8]; // 0x10
    JEDIB_SPAWN_s *link; // 0x18, goody <-> baddie cross link
    AILOCATOR_s locator; // 0x1c
    u8 filler_0x58[0x4]; // 0x58
    u8 flags;            // 0x5c, bit 1 marks a baddie
    u8 filler_0x5d[0x3]; // 0x5d
};
DECOMP_ASSERT(sizeof(JEDIB_SPAWN_s) == 0x60, "Jedi_B spawn slot size");

struct JEDIB_s {
    JEDIB_SPAWN_s spawns[264];              // 0x0000
    i16 spawn_count;                        // 0x6300
    u8 filler_0x6302[0x630c - 0x6302];      // 0x6302
    u32 seed;                               // 0x630c
    u8 filler_0x6310[0x63ec - 0x6310];      // 0x6310
};
DECOMP_ASSERT(sizeof(JEDIB_s) == 0x63ec, "Jedi_B state size");
static JEDIB_s jedi_b;

// Places one arena slot: pushes the point clear of any anti-node, drops it onto
// the terrain and resolves the AI path position for it. Returns false when the
// point cannot be used.
static i32 JediBInitLocator(WORLDINFO_s *world, NUVEC *position, i32 flags, AILOCATOR_s *locator, f32 radius) {
    if (netclient != 0)
        return 0;
    if (world->ai_sys != NULL) {
        AIANTINODE_s *node = world->ai_sys->antinodes;
        for (i32 i = 0; i < world->ai_sys->antinode_count; i++, node++) {
            if (node->radius != 0.0f) {
                f32 clearance = node->radius + 1.0f;
                f32 dx = position->x - node->position.x;
                f32 dz = position->z - node->position.z;
                f32 distance = dx * dx + dz * dz;
                if (clearance * clearance > distance) {
                    f32 scale = clearance / NuFsqrt(distance);
                    position->x = node->position.x + dx * scale;
                    position->z = node->position.z + dz * scale;
                    break;
                }
            }
        }
    }
    memset(locator, 0, sizeof(*locator));
    if (radius * radius <= position->x * position->x + position->z * position->z)
        return 0;
    f32 height = GameShadow(NULL, position, 5.0f, 0);
    if (height != 2000000.0f)
        position->y = height;
    AISysGetPathPos(world->ai_sys, position, &locator->path, 0, 0xff);
    if ((locator->path_flags & 1) == 0)
        return 0;
    locator->position = *position;
    locator->flags = flags;
    return 1;
}

// Scatters goody/baddie clusters over the arena disc, then lowers the teleport
// spline so it meets the new floor.
void JediB_Init(WORLDINFO_s *world) {
    if (Mission_Active(MissionSys) != NULL)
        return;
    memset(&jedi_b, 0, sizeof(jedi_b));
    jedi_b.seed = jedib_seed;
    jedib_netpacket = SetLevelHack(0x20);
    NUVEC position;
    NUVEC orbit;
    AILOCATOR_s locator;
    if (netclient == 0) {
        f32 step = jedib_create_step_Normal;
        if (g_lowEndLevelBehaviour != 0)
            step = jedib_create_step_LowEnd;
        for (f32 x = -jedib_outer_r; x <= jedib_outer_r; x += step) {
            for (f32 z = -jedib_outer_r; z <= jedib_outer_r; z += step) {
                position.x = x + (jedib_offset - (jedib_offset + jedib_offset) * NuRandFloatSeeded(&jedi_b.seed));
                position.y = 0.0f;
                position.z = z + (jedib_offset - (jedib_offset + jedib_offset) * NuRandFloatSeeded(&jedi_b.seed));
                f32 distance = position.x * position.x + position.z * position.z;
                if (jedib_outer_r * jedib_outer_r > distance && distance > jedib_inner * jedib_inner) {
                f32 height = GameShadow(NULL, &position, 5.0f, 0);
                if (height != 2000000.0f)
                    position.y = height;
                if (!JediBInitLocator(world, &position, NuRandIntSeeded(&jedi_b.seed), &locator, jedib_outer_r))
                    continue;
                if (jedi_b.spawn_count > 0xff)
                    continue;
                JEDIB_SPAWN_s *goody = &jedi_b.spawns[jedi_b.spawn_count];
                jedi_b.spawn_count = jedi_b.spawn_count + 1;
                goody->position = position;
                goody->locator = locator;
                goody->flags &= ~2;
                orbit.x = 0.0f;
                orbit.y = 0.0f;
                orbit.z = jedib_proximity;
                i32 angle = NuRandIntSeeded(&jedi_b.seed);
                NuVecRotateY(&orbit, &orbit, NuRandIntSeeded(&jedi_b.seed));
                i32 baddies = jedib_min_baddies_per_goody +
                              static_cast<i32>(NuRandIntSeeded(&jedi_b.seed) %
                                               static_cast<u32>(jedib_max_baddies_per_goody -
                                                                jedib_min_baddies_per_goody));
                for (i32 i = 0; i < baddies && jedi_b.spawn_count <= 0xff; i++) {
                    if (i != 0)
                        angle = NuAngAdd(angle, 0x10000 / baddies);
                    NuVecRotateY(&position, &orbit, angle);
                    NuVecAdd(&position, &position, &goody->position);
                    if (!JediBInitLocator(world, &position, angle, &locator, jedib_outer_r))
                        continue;
                    JEDIB_SPAWN_s *baddie = &jedi_b.spawns[jedi_b.spawn_count];
                    jedi_b.spawn_count = jedi_b.spawn_count + 1;
                    baddie->flags |= 2;
                    baddie->position = position;
                    baddie->locator = locator;
                    baddie->link = goody;
                    baddie->angle = angle;
                    goody->link = baddie;
                }
                }
            }
        }
    }
    NUGSPLINE *spline = edSpline_SplineFind(world->current_gscn, "teleport_01");
    if (spline != NULL) {
        spline->pts[0].y -= 0.35f;
        spline->pts[1].y = spline->pts[0].y;
    }
    LevBlowUp[0] = reinterpret_cast<i32>(GizmoBlowUp_FindByName(world, "Thermo_011"));
}

void JediB_Reset(WORLDINFO_s *) {
}

void JediB_Update(WORLDINFO_s *) {
}

void JediB_DrawPanel(WORLDINFO_s *) {
}

// ===========================================================================
// Gunship (Gunship_A / Gunship_B)
// ===========================================================================

void GunshipA_Init(WORLDINFO_s *world) {
    trooper_boltid[1] = BoltType_FindIDByName("trooper_green", world);
    trooper_boltid[0] = BoltType_FindIDByName("trooper_red", world);
    trooper_side[0] = 0;
    trooper_side[1] = 0;
    trooper_side[2] = 0;
    trooper_side[3] = 0;
    trooper_side[4] = 0;
    trooper_side[5] = 1;
    trooper_side[6] = 1;
    trooper_side[7] = 1;
    trooper_side[8] = 1;
    trooper_side[9] = 1;
    InitMiniSnowTroopers(world, 0xa, 0x20, 0);
    LevGizmo[0] = GizmoFindByName(world->gizmo_sys, blowup_gizmotype_id, "power_a11");
    LevGizmo[1] = GizmoFindByName(world->gizmo_sys, blowup_gizmotype_id, "power_b11");
    LevGizmo[2] = GizmoFindByName(world->gizmo_sys, blowup_gizmotype_id, "power_a21");
    LevGizmo[3] = GizmoFindByName(world->gizmo_sys, blowup_gizmotype_id, "power_b21");
}

void GunshipA_Update(WORLDINFO_s *world) {
    UpdateMiniSnowTroopers(world);
}

void GunshipA_Draw(WORLDINFO_s *world) {
    if (TimingBarSet == 5)
        TBOPENFN("mini", 5);
    DrawMiniSnowTroopers(world);
    if (TimingBarSet == 5)
        TBCLOSEFN("mini", 5);
}

void GunshipB_Reset(WORLDINFO_s *world) {
    LevGizmo[0] = GizmoFindByName(world->gizmo_sys, blowup_gizmotype_id, "gun1");
    LevGizmo[1] = GizmoFindByName(world->gizmo_sys, blowup_gizmotype_id, "gun2");
    LevGizmo[2] = GizmoFindByName(world->gizmo_sys, blowup_gizmotype_id, "gun3");
    LevGizmo[3] = GizmoFindByName(world->gizmo_sys, blowup_gizmotype_id, "gun4");
    LevGizmo[4] = GizmoFindByName(world->gizmo_sys, blowup_gizmotype_id, "gun5");
    LevGizmo[5] = GizmoFindByName(world->gizmo_sys, blowup_gizmotype_id, "gun6");
    LevGizmo[6] = GizmoFindByName(world->gizmo_sys, blowup_gizmotype_id, "gun7");
    LevGizmo[7] = GizmoFindByName(world->gizmo_sys, blowup_gizmotype_id, "gun8");
}

i32 GunshipInLevel(LEVELDATA_s *level) {
    if (BONUS_GUNSHIPA_LDATA == NULL)
        return 0;
    return BONUS_GUNSHIPA_LDATA == level;
}

// Nudges a dragged bomb toward the nearest armed blow-up gizmo, easing harder
// the closer it already is.
void GunShip_DragBombSeekBlowUp(GameObject_s *object) {
    if (object->character_context != 0x34)
        return;
    f32 nearest_distance = gunshipb_seekrange * gunshipb_seekrange;
    GIZMOBLOWUP_s *nearest = NULL;
    NUVEC candidate_offset;
    NUVEC offset;
    NUVEC direction;
    for (i32 i = 0; i < 8; i++) {
        if (LevGizmo[i] != NULL) {
            GIZMOBLOWUP_s *blowup = static_cast<GIZMOBLOWUP_s *>(LevGizmo[i]->object);
            if (blowup != NULL && (blowup->status_flags & 0x800001) == 0x800000) {
                f32 distance = NuVecDistSqr(&blowup->mid_position, &object->apiobj.collision_position,
                                            &candidate_offset);
                if (distance < nearest_distance) {
                    nearest_distance = distance;
                    offset = candidate_offset;
                    nearest = blowup;
                }
            }
        }
    }
    if (nearest == NULL)
        return;
    NuVecNorm(&direction, &offset);
    f32 pull = (1.0f - NuFsqrt(nearest_distance) / gunshipb_seekrange) * gunshipb_seekmom;
    direction.x *= pull;
    direction.z *= pull;
    object->apiobj.velocity.x = SeekValF(object->apiobj.velocity.x, direction.x, gunshipb_seekmomseek);
    object->apiobj.velocity.z = SeekValF(object->apiobj.velocity.z, direction.z, gunshipb_seekmomseek);
}

// ===========================================================================
// Bonus gunship (Bonus_Gunship_A / Bonus_Gunship_B)
// ===========================================================================

void BonusGunshipA_Reset(WORLDINFO_s *) {
    gunship_player_dead = 0;
    if (LevFlag.progress == GUNSHIP_ACTIVE)
        LevFlag.progress = GUNSHIP_WON;
    LevFlag.exit = 0;
    bonus_gunship_store_progress_flag = 0;
}

void BonusGunshipA_Update(WORLDINFO_s *world) {
    if (LevFlag.progress == GUNSHIP_INACTIVE) {
        if ((Player[0] != NULL && Player[0]->field_0x661 == 0 && Player[0]->field_0x68c > 12.1f) ||
            (Player[1] != NULL && Player[1]->field_0x661 == 0 && Player[1]->field_0x68c > 12.1f)) {
            i32 client = netclient;

            if (client == 0) {
                Doors_SetLastDoor((DOOR_s *)Door_FindByName(world, "gunshipa_mid"));
                bonus_gunship_store_progress_flag = 1;
                StoreLevelProgress(world);
                bonus_gunship_store_progress_flag = 0;
            }
            LevFlag.progress = GUNSHIP_ACTIVE;
        }
    }
    if (gunship_player_dead == 0) {
        if ((Player[0] != NULL && Player[0]->apiobj.field_0x287 != 0) ||
            (Player[1] != NULL && Player[1]->apiobj.field_0x287 != 0)) {
            gunship_player_dead = 1;
            ResetLevel(world, "ep2_bonus_gunshipcavalry_explode", 1);
        }
    }
}

void BonusGunshipB_Init(WORLDINFO_s *world) {
    bonusgunshipb_netpacket = (struct BONUSGUNSHIP_NETPACKET_s *)SetLevelHack(0xc);
    LevGizObst[0] = GizObstacle_FindByName(world->giz_obstacle_sys, "obs");
}

void BonusGunshipB_Reset(WORLDINFO_s *) {
    LevFlag.progress = GUNSHIP_INACTIVE;
    LevFlag.exit = 0;
    MiscTime = 0;
    gunship_player_dead = 0;
    bonus_gunship_store_progress_flag = 0;
}

void BonusGunshipB_Update(WORLDINFO_s *world) {
    if (netclient == 0) {
        if (gunship_player_dead == 0) {
            if ((Player[0] != NULL && Player[0]->apiobj.field_0x287 != 0) ||
                (Player[1] != NULL && Player[1]->apiobj.field_0x287 != 0)) {
                gunship_player_dead = 1;
                ResetLevel(world, "ep2_bonus_gunshipcavalry_explode", 1);
            }
        }
        u8 progress = LevFlag.progress;

        if (progress == GUNSHIP_INACTIVE) {
            // The cavalry is held back three seconds longer per death so far,
            // capped at half a minute.
            f32 wait = 15.0f;
            if (LevDeaths > 0) {
                wait = (f32)LevDeaths * 3.0f + 15.0f;
                if (wait > 30.0f)
                    wait = 30.0f;
            }
            if (GameTimer.time_elapsed >= wait) {
                LevFlag.progress = GUNSHIP_ACTIVE;
                if (LevGizObst[0] != NULL)
                    LevGizObst[0]->runtime_flags |= GIZOBSTACLE_RUNTIME_FLAG_AI_ACTIVE;
                MiscTime = 45.0f;
                TimerScale = 1.0f;
                TimerAlpha = 0.0f;
            }
        } else if (progress == GUNSHIP_ACTIVE) {
            // The result is unused; the original still makes the call.
            NuFmod(MiscTime, 5.0f);
            const f32 previous = MiscTime;
            MiscTime = previous - FRAMETIME;
            if (MiscTime <= 0.0f) {
                if (Player[0] != NULL && static_cast<i8>(Player[0]->apiobj.flags_low) < 0)
                    LoseCoins(Player[0], 1);
                if (Player[1] != NULL && static_cast<i8>(Player[1]->apiobj.flags_low) < 0)
                    LoseCoins(Player[1], 1);
                if (gunship_player_dead == 0) {
                    gunship_player_dead = 1;
                    ResetLevel(world, "ep2_bonus_gunshipcavalry_explode", 1);
                }
                LevFlag.progress = GUNSHIP_WON;
            } else if (MiscTime > 0.0f) {
                // Pulse the countdown and tick once per whole second crossed.
                if ((i32)MiscTime != (i32)previous) {
                    TimerScale = 1.25f;
                    TickTockSfx();
                }
            }
        }
        bonusgunshipb_netpacket->state = LevFlag.progress;
        bonusgunshipb_netpacket->sub = LevFlag.exit;
        bonusgunshipb_netpacket->time = MiscTime;
    } else {
        LevFlag.progress = bonusgunshipb_netpacket->state;
        LevFlag.exit = bonusgunshipb_netpacket->sub;
        MiscTime = bonusgunshipb_netpacket->time;
    }
}

void BonusGunshipB_Panel(WORLDINFO_s *) {
    if (LevFlag.progress == GUNSHIP_ACTIVE) {
        if (MiscTime > 60.0f)
            DrawTimer((i32)MiscTime + 1, 0, 0);
    }
}

// ===========================================================================
// Dooku (Dooku_C)
// ===========================================================================

void DookuC_Init(WORLDINFO_s *world) {
    LevGizForce[0] = GizForce_FindByName(world->giz_force_sys, "fptower_1");
    LevGizForce[1] = GizForce_FindByName(world->giz_force_sys, "fptower_2");
    LevGizForce[2] = GizForce_FindByName(world->giz_force_sys, "fptower_3");
    LevAIPathNode[0] = AIPathFindNode(world->ai_sys, 0, "fptower_a");
    LevAIPathNode[1] = AIPathFindNode(world->ai_sys, 0, "fptower_b");
    LevAIPathNode[2] = AIPathFindNode(world->ai_sys, 0, "fptower_c");
    LevAIPathNode[3] = AIPathFindNode(world->ai_sys, 0, "fptower_d");
    // Callee-written scratch that this level never reads. Its 4-byte size is
    // load-bearing: it fixes the original's stack frame layout.
    void *cnx_scratch;
    LevPathCnx[0] = AIPAthFindPathCnx(world->ai_sys, 0, "fptower_a", "fptower_b", &cnx_scratch);
    LevPathCnx[1] = AIPAthFindPathCnx(world->ai_sys, 0, "fptower_b", "fptower_c", &cnx_scratch);
    LevPathCnx[2] = AIPAthFindPathCnx(world->ai_sys, 0, "fptower_c", "fptower_d", &cnx_scratch);
    LevPathCnx[3] = AIPAthFindPathCnx(world->ai_sys, 0, "fptower_d", "fptower_e", &cnx_scratch);
    dookuC_nodesNeedUpdating = 1;
}

void DookuC_Reset(WORLDINFO_s *world) {
    dooku_c.total = NULL;
    dooku_c.hits = NULL;
    dooku_c.node.scene = NULL;
    dooku_c.node.special = NULL;
    dooku_c.node.display_special = NULL;
    if (netclient == 0) {
        dooku_c.total = SetGizAIMessage(gizaimessagesys, "DookuFight", 0.0f, NULL);
        dooku_c.hits = CheckGizAIMessage(gizaimessagesys, "ShowHearts", NULL);
    }
    NuSpecialFind(world->current_gscn, &dooku_c.node, "dooku_force", 1);
}

// The three force towers form a stack; while it is complete the AI path runs
// up and over it, so the four "fptower" nodes are moved onto the tower tops
// and the connections between them become traversable again. The node
// positions depend on which two towers the stack picked up, in order.
static void DookuC_SetNodePos(AIPATHNODE_s *node, f32 x, f32 y, f32 z) {
    node->position.x = x;
    node->position.y = y;
    node->position.z = z;
}

void DookuC_Update(WORLDINFO_s *world) {
    if (netclient == 0) {
        if (FreePlay != 0) {
            KillBossCompleteLevel((i32)(i16)id_COUNTDOOKU, 0, 0.0f);
        } else if (DOOKUOUTRO_LDATA != NULL) {
            KillBossNewLevel((i32)(i16)id_COUNTDOOKU, 0, 0.0f, DOOKUOUTRO_LDATA->idx);
        }
    }
    DrawForceBackEffect(&dooku_c.node);

    GIZFORCE_s *tower_a = LevGizForce[0];
    if (tower_a == NULL) {
        return;
    }
    GIZFORCE_s *tower_b = LevGizForce[1];
    if (tower_b == NULL) {
        return;
    }
    GIZFORCE_s *tower_c = LevGizForce[2];
    if (tower_c == NULL) {
        return;
    }

    GIZFORCEGROUP_s *stack = tower_a->group;
    if (stack == NULL || (stack->field_0x24 & GIZFORCE_GROUP_STACK_COMPLETE) == 0) {
        if (dookuC_nodesNeedUpdating == 0) {
            return;
        }
        dookuC_nodesNeedUpdating = 0;
        for (i32 i = 0; i < 4; i++) {
            PATHCNXDATA_s *cnx = (PATHCNXDATA_s *)LevPathCnx[i];
            if (cnx != NULL) {
                cnx->flags0 |= 0x80000000;
                cnx->flags4 |= 0x80000000;
            }
        }
    } else {
        if (dookuC_nodesNeedUpdating != 0) {
            return;
        }
        dookuC_nodesNeedUpdating = 1;
        for (i32 i = 0; i < 4; i++) {
            PATHCNXDATA_s *cnx = (PATHCNXDATA_s *)LevPathCnx[i];
            if (cnx != NULL) {
                cnx->flags0 &= 0x7fffffff;
                cnx->flags4 &= 0x7fffffff;
            }
        }

        AIPATHNODE_s *node_a = (AIPATHNODE_s *)LevAIPathNode[0];
        if (node_a == NULL) {
            return;
        }
        AIPATHNODE_s *node_b = (AIPATHNODE_s *)LevAIPathNode[1];
        if (node_b == NULL) {
            return;
        }
        AIPATHNODE_s *node_c = (AIPATHNODE_s *)LevAIPathNode[2];
        if (node_c == NULL) {
            return;
        }
        AIPATHNODE_s *node_d = (AIPATHNODE_s *)LevAIPathNode[3];
        if (node_d == NULL) {
            return;
        }

        if (stack->forces[0] == LevGizForce[0]) {
            DookuC_SetNodePos(node_a, 3.76f, 0.01f, -1.59f);
            if (stack->forces[1] == LevGizForce[1]) {
                DookuC_SetNodePos(node_b, 4.29f, 0.56f, -1.5f);
                DookuC_SetNodePos(node_c, 4.05f, 1.12f, -1.12f);
                DookuC_SetNodePos(node_d, 4.46f, 1.69f, -0.97f);
            } else {
                DookuC_SetNodePos(node_b, 4.42f, 0.56f, -1.46f);
                DookuC_SetNodePos(node_c, 4.66f, 1.12f, -1.08f);
                DookuC_SetNodePos(node_d, 4.41f, 1.69f, -1.04f);
            }
        } else if (stack->forces[0] == LevGizForce[1]) {
            DookuC_SetNodePos(node_a, 3.6f, 0.01f, -0.96f);
            if (stack->forces[1] == LevGizForce[0]) {
                DookuC_SetNodePos(node_b, 4.06f, 0.56f, -0.98f);
                DookuC_SetNodePos(node_c, 4.2f, 1.12f, -1.28f);
                DookuC_SetNodePos(node_d, 4.41f, 1.69f, -1.04f);
            } else {
                DookuC_SetNodePos(node_b, 4.06f, 0.56f, -0.98f);
                DookuC_SetNodePos(node_c, 4.37f, 1.12f, -0.81f);
                DookuC_SetNodePos(node_d, 4.3f, 1.69f, -1.3f);
            }
        } else if (stack->forces[0] == LevGizForce[2]) {
            DookuC_SetNodePos(node_a, 4.28f, 0.01f, -0.41f);
            if (stack->forces[1] == LevGizForce[0]) {
                DookuC_SetNodePos(node_b, 4.66f, 0.56f, -0.98f);
                DookuC_SetNodePos(node_c, 4.5f, 1.12f, -1.35f);
                DookuC_SetNodePos(node_d, 4.38f, 1.69f, -1.08f);
            } else {
                DookuC_SetNodePos(node_b, 4.53f, 0.56f, -0.77f);
                DookuC_SetNodePos(node_c, 4.2f, 1.12f, -0.91f);
                DookuC_SetNodePos(node_d, 4.43f, 1.69f, -1.28f);
            }
        }

        if (world->ai_sys->path_sys != NULL && world->ai_sys->path_sys->active_path != NULL) {
            for (i32 i = 0; i < 4; i++) {
                AIPathNodeUpdatePos(world->ai_sys, world->ai_sys->path_sys->active_path,
                                    (AIPATHNODE_s *)LevAIPathNode[i]);
            }
        }
    }
}

void DookuC_DrawPanel(WORLDINFO_s *) {
    if (netclient != 0)
        return;
    GameObject_s *obj = (GameObject_s *)FindGameObject((i32)(i16)id_COUNTDOOKU, 1, 1, 1, 0);
    if (obj != NULL && dooku_c.hits != NULL && dooku_c.hits->value == 1.0f)
        DrawBossHitPoints(obj);
}
