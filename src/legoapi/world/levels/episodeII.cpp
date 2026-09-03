#include <stdio.h>
#include <string.h>

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
#include "legoapi/items/objects/gameobjects.h"
#include "legoapi/legoapi_types.h"
#include "legoapi/world/levels/levels.h"
#include "legoapi/render/core/render.h"
#include "nu2api/nucore/nugcutscene.h"
#include "nu2api/nu3d/nuspecial.h"
#include "nu2api/nu3d/nutex.h"
#include "nu2api/numath/numtx.h"
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
    void *AIPAthFindPathCnx(AISYS_s *, i32, void *, void *, void *); // legoapi/ai pathfinding
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

// kamino_e level state block and hud scene object.
struct kamino_e_state_s {
    char pad_0x00[0x28];
    f32 field_0x28; // 0x28
};
static struct kamino_e_state_s *kamino_e_state;
static void *kamino_e_special; // kamino_e named scene object

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
        if (kamino_e_state != NULL && kamino_e_state->field_0x28 > 0.0f) {
            GameObject_s *obj = (GameObject_s *)FindGameObject((i32)(i16)id_JANGOFETT, 1, 1, 1, 0);
            if (obj != NULL && kamino_e_state != NULL && obj->apiobj.anim_packet.time_secondary == 1.0f)
                DrawBossHitPoints(obj);
        }
    }
    NuSpecialSetDrawMtx(&kamino_e_special, NuSpecialGetDrawMtx(&kamino_e_special));
    NuSpecialSetVisibility(&kamino_e_special, 1);
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
    if (netclient != 0)
        return;
    i32 complete = 0;
    if (GizForce_Complete(force_array[0]))
        complete++;
    if (GizForce_Complete(force_array[1]))
        complete++;
    if (GizForce_Complete(force_array[2]))
        complete++;
    if (GizForce_Complete(force_array[3]))
        complete++;
    if (ObiWan == NULL) {
        ObiWan = (GameObject_s *)FindGameObject((i32)(i16)id_OBIWANKENOBIJEDIMASTER, 0x400, 0, 1, 0);
        return;
    }
    if (complete == 4) {
        if (FreePlay == 0)
            NewCutScene(NULL, world->cutscene_sys, "factory_escape", 1);
    } else {
        ObiWan->apiobj.position = {79.2f, 0.75f, -10.5f};
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
    if (TimingBarSet == 5) {
        TBOPENFN("gun_timing", 5);
        DrawMiniSnowTroopers(world);
    } else {
        DrawMiniSnowTroopers(world);
        if (TimingBarSet == 5)
            TBCLOSEFN("gun_timing", 5);
    }
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
        bool found = false;
        if (Player[0] != NULL && Player[0]->field_0x661 == 0 && Player[0]->field_0x68c > 0.001f) {
            found = true;
        } else if (Player[1] != NULL && Player[1]->field_0x661 == 0 && Player[1]->field_0x68c > 0.001f) {
            found = true;
        }
        if (found) {
            if (netclient != 0) {
                LevFlag.progress = GUNSHIP_ACTIVE;
            } else {
                Doors_SetLastDoor((DOOR_s *)Door_FindByName(world, "bonus_door"));
                bonus_gunship_store_progress_flag = 1;
                StoreLevelProgress(world);
                bonus_gunship_store_progress_flag = 0;
                LevFlag.progress = GUNSHIP_ACTIVE;
            }
        }
    }
    if (gunship_player_dead == 0) {
        if ((Player[0] != NULL && Player[0]->apiobj.field_0x287 != 0) ||
            (Player[1] != NULL && Player[1]->apiobj.field_0x287 != 0)) {
            gunship_player_dead = 1;
            ResetLevel(world, "bonus", 1);
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
    if (netclient != 0) {
        LevFlag.progress = bonusgunshipb_netpacket->state;
        LevFlag.exit = bonusgunshipb_netpacket->sub;
        MiscTime = bonusgunshipb_netpacket->time;
    } else {
        if (gunship_player_dead == 0 && ((Player[0] != NULL && Player[0]->apiobj.field_0x287 != 0) ||
                                         (Player[1] != NULL && Player[1]->apiobj.field_0x287 != 0))) {
            gunship_player_dead = 1;
            ResetLevel(world, "bonus_gunship", 1);
        }
        bonusgunshipb_netpacket->state = LevFlag.progress;
        bonusgunshipb_netpacket->sub = LevFlag.exit;
        bonusgunshipb_netpacket->time = MiscTime;
    }
    if (LevFlag.progress == 0) {
        if (LevDeaths > 0) {
            float x = (float)LevDeaths * LevDeaths + 1.0f;
            if (GameTimer.time_elapsed >= x)
                LevFlag.progress = GUNSHIP_ACTIVE;
        }
    } else if (LevFlag.progress == GUNSHIP_ACTIVE) {
        if (MiscTime > 5.0f)
            MiscTime = 5.0f;
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
    LevGizForce[0] = GizForce_FindByName(world->giz_force_sys, "dooku");
    LevGizForce[1] = GizForce_FindByName(world->giz_force_sys, "dooku1");
    LevGizForce[2] = GizForce_FindByName(world->giz_force_sys, "dooku2");
    void *path1 = AIPathFindNode(world->ai_sys, NULL, "path1");
    LevAIPathNode[0] = path1;
    void *path2 = AIPathFindNode(world->ai_sys, NULL, "path2");
    LevAIPathNode[1] = path2;
    void *path3 = AIPathFindNode(world->ai_sys, NULL, "path3");
    LevAIPathNode[2] = path3;
    void *path4 = AIPathFindNode(world->ai_sys, NULL, "path4");
    LevAIPathNode[3] = path4;
    char buf[0x40];
    LevPathCnx[0] = AIPAthFindPathCnx(world->ai_sys, 0, path1, path2, buf);
    LevPathCnx[1] = AIPAthFindPathCnx(world->ai_sys, 0, path2, path3, buf);
    LevPathCnx[2] = AIPAthFindPathCnx(world->ai_sys, 0, path3, path4, buf);
    LevPathCnx[3] = AIPAthFindPathCnx(world->ai_sys, 0, path4, (void *)"conn", buf);
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
    NuSpecialFind(world->current_gscn, reinterpret_cast<void **>(&dooku_c.node), "dooku_node", 1);
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
}

void DookuC_DrawPanel(WORLDINFO_s *) {
    if (netclient != 0)
        return;
    GameObject_s *obj = (GameObject_s *)FindGameObject((i32)(i16)id_COUNTDOOKU, 1, 1, 1, 0);
    if (obj != NULL && dooku_c.hits != NULL && obj->apiobj.anim_packet.time_secondary == 1.0f)
        DrawBossHitPoints(obj);
}
