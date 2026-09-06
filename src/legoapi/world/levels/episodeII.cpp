#include <stdio.h>
#include <string.h>

#include "gameapi/ai/aisys/aisys.h"
#include "decomp.h"
#include "legoapi/world/level.h"
#include "globals.h"
#include "legoapi/characters/core/players.h"
#include "legoapi/characters/motion/gameanim.h"
#include "legoapi/gizmo/base/GizBlowupObjectInterface.h"
#include "legoapi/gizmo/base/GizForceObjectInterface.h"
#include "legoapi/gizmos/object/newblowup.h"
#include "legoapi/gizmos/traps/gizforce.h"
#include "legoapi/ai/core/ai_sys_stubs.h"
#include "legoapi/audio/sfx.h"
#include "legoapi/cutscenes/cutscenes.h"
#include "legoapi/items/base/collection.h"
#include "legoapi/items/objects/gameobjects.h"
#include "legoapi/legoapi_types.h"
#include "legoapi/world/levels/levels.h"
#include "legoapi/render/core/render.h"
#include "nu2api/nucore/nugcutscene.h"
#include "nu2api/nu3d/nuspecial.h"
#include "nu2api/nu3d/nutex.h"
#include "nu2api/numath/nufloat.h"
#include "nu2api/numath/numtx.h"
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
}

// --- File-local statics (original _ZL... symbols; not renamed) ---------------

// Conveyor speeds from the level config, parked here while Factory_B forces the
// belts to a stop; FactoryB_Update restores them.
static f32 FactoryBConveyorXSpeed;
static f32 FactoryBConveyorZSpeed;
// Kamino disco-room state (original _ZL11kaminodisco). A byte flag (0/1/2)
// that KaminoC_Init clears via memset of the enclosing disco struct, which is
// why readers cannot be constant-folded.
static u8 kaminodisco;
// Dooku_C level state (original _ZL7dooku_c, one 20-byte .bss object).
struct DOOKUC_STATE_s {
    GIZAIMESSAGE_s *total; // 0x00
    GIZAIMESSAGE_s *hits;  // 0x04
    nuhspecial_s node;     // 0x08, force-back effect model
};
static struct DOOKUC_STATE_s dooku_c;

struct kamino_e_state_s {
    char pad_0x00[0x28];
    f32 field_0x28; // 0x28
};

// Kamino_E level state (original _ZL8kamino_e, one 120-byte .bss object
// holding more of the level's state than is modelled here). The fields must
// stay in one object: handing `&special` to the NuSpecial API makes the whole
// block address-taken, so `state` is reloaded after every call.
struct KAMINO_E_s {
    u8 pad_0x00[0x0c];
    struct kamino_e_state_s *state; // 0x0c
    u8 pad_0x10[0x0c];
    void *special; // 0x1c, kamino_e named scene object
};
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

void BountyHunterPursuitA_Update(WORLDINFO_s *) {
}

void BountyHunterPursuitB_Update(WORLDINFO_s *) {
}

void BountyHunterPursuitC_Update(WORLDINFO_s *) {
}

void BountyHunterPursuitD_Update(WORLDINFO_s *) {
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
    // KaminoC_Init/Update are not recovered yet, but the original routines
    // mutate this byte. Keep the read visible without changing its codegen to
    // the load-then-compare sequence produced by a volatile object.
    __asm__ __volatile__("" : : "m"(kaminodisco));
    return kaminodisco == 2;
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

void KaminoC_Init(WORLDINFO_s *) {
}

void KaminoC_Reset(WORLDINFO_s *) {
}

void KaminoC_Update(WORLDINFO_s *) {
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

void KaminoE_Reset(WORLDINFO_s *) {
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
        if (kamino_e.state->field_0x28 > 0.0f) {
            GameObject_s *obj = (GameObject_s *)FindGameObject((i32)(i16)id_JANGOFETT, 1, 1, 1, 0);
            if (obj != NULL && kamino_e.state != NULL && kamino_e.state->field_0x28 == 1.0f)
                DrawBossHitPoints(obj);
        }
    }
    NuSpecialSetDrawMtx(&kamino_e.special, NuSpecialGetDrawMtx(&kamino_e.special));
    NuSpecialSetVisibility(&kamino_e.special, 1);
}

void KaminoE_CheckPlatHit(BOLT_s *) {
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

void FactoryB_Update(WORLDINFO_s *) {
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
    if (*(volatile i32 *)&netclient != 0)
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

void JediB_Init(WORLDINFO_s *) {
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

void GunShip_DragBombSeekBlowUp(GameObject_s *) {
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
            register i32 client asm("edi") = netclient;
            __asm__ __volatile__("" : "+D"(client));
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
    if (*(volatile i32 *)&netclient == 0) {
        if (gunship_player_dead == 0) {
            if ((Player[0] != NULL && Player[0]->apiobj.field_0x287 != 0) ||
                (Player[1] != NULL && Player[1]->apiobj.field_0x287 != 0)) {
                gunship_player_dead = 1;
                ResetLevel(world, "ep2_bonus_gunshipcavalry_explode", 1);
            }
        }
        register u8 progress asm("eax") = LevFlag.progress;
        __asm__ __volatile__("" : "+a"(progress));
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
        dooku_c.total = SetGizAIMessage(gizaimessagesys, "dooku_total", 0.0f, NULL);
        dooku_c.hits = CheckGizAIMessage(gizaimessagesys, "dooku_hits", NULL);
    }
    NuSpecialFind(world->current_gscn, &dooku_c.node, "dooku_node", 1);
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
