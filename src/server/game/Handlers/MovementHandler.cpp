/*
 * Copyright (C) 2008-2013 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "AnticheatMgr.h"
#include "Common.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "Opcodes.h"
#include "Log.h"
#include "Corpse.h"
#include "Player.h"
#include "SpellAuras.h"
#include "MapManager.h"
#include "Transport.h"
#include "Battleground.h"
#include "WaypointMovementGenerator.h"
#include "InstanceSaveMgr.h"
#include "ObjectMgr.h"

//#define __ANTI_DEBUG__

#ifdef __ANTI_DEBUG__
std::string FlagsToStr(const uint32 Flags)
{
    std::string Ret="";
    if(Flags==0)
    {
        Ret="None";
        return Ret;
    }

    if(Flags & MOVEMENTFLAG_FORWARD)
    {   Ret+="FW "; }
    if(Flags & MOVEMENTFLAG_BACKWARD)
    {   Ret+="BW "; }
    if(Flags & MOVEMENTFLAG_STRAFE_LEFT)
    {   Ret+="STL ";    }
    if(Flags & MOVEMENTFLAG_STRAFE_RIGHT)
    {   Ret+="STR ";    }
    if(Flags & MOVEMENTFLAG_LEFT)
    {   Ret+="LF "; }
    if(Flags & MOVEMENTFLAG_RIGHT)
    {   Ret+="RI "; }
    if(Flags & MOVEMENTFLAG_PITCH_UP)
    {   Ret+="PTUP ";   }
    if(Flags & MOVEMENTFLAG_PITCH_DOWN)
    {   Ret+="PTDW ";   }
    if(Flags & MOVEMENTFLAG_WALKING)
    {   Ret+="WALK ";   }
    if(Flags & MOVEMENTFLAG_ONTRANSPORT)
    {   Ret+="TRANS ";  }
    if(Flags & MOVEMENTFLAG_LEVITATING)
    {   Ret+="LEVI ";   }
    if(Flags & MOVEMENTFLAG_ROOT)
    {   Ret+="ROOT ";    }
    if(Flags & MOVEMENTFLAG_FALLING)
    {   Ret+="JUMP ";   }
    if(Flags & MOVEMENTFLAG_FALLING_FAR)
    {   Ret+="FALL ";   }
    if(Flags & MOVEMENTFLAG_PENDING_STOP)
    {   Ret+="PENDING_STOP ";   }
    if(Flags & MOVEMENTFLAG_PENDING_STRAFE_STOP)
    {   Ret+="PENDING_STRAFE_STOP ";   }
    if(Flags & MOVEMENTFLAG_PENDING_FORWARD)
    {   Ret+="PENDING_FORWARD ";   }
    if(Flags & MOVEMENTFLAG_PENDING_BACKWARD)
    {   Ret+="PENDING_BACKWARD ";   }
    if(Flags & MOVEMENTFLAG_PENDING_STRAFE_LEFT)
    {   Ret+="PENDING_STRAFE_LEFT ";   }
    if(Flags & MOVEMENTFLAG_PENDING_STRAFE_RIGHT)
    {   Ret+="PENDING_STRAFE_RIGHT ";   }
    if(Flags & MOVEMENTFLAG_PENDING_ROOT)
    {   Ret+="PENDING_ROOT ";   }
    if(Flags & MOVEMENTFLAG_SWIMMING)
    {   Ret+="SWIM ";   }
    if(Flags & MOVEMENTFLAG_ASCENDING)
    {   Ret+="ASC ";  }
    if(Flags & MOVEMENTFLAG_DESCENDING)
    {   Ret+="DESC ";   }
    if(Flags & MOVEMENTFLAG_CAN_FLY)
    {   Ret+="CFLY ";   }
    if(Flags & MOVEMENTFLAG_FLYING)
    {   Ret+="FLY ";    }
    if(Flags & MOVEMENTFLAG_SPLINE_ELEVATION)
    {   Ret+="SPLINE_ELEVATION ";     }
    if(Flags & MOVEMENTFLAG_SPLINE_ENABLED)
    {   Ret+="SPLINE_ENABLED ";    }
    if(Flags & MOVEMENTFLAG_WATERWALKING)
    {   Ret+="WTWALK "; }
    if(Flags & MOVEMENTFLAG_FALLING_SLOW)
    {   Ret+="FALLING_SLOW ";   }
    if(Flags & MOVEMENTFLAG_HOVER)
    {   Ret+="HOVER ";   }

    return Ret;
}
#endif // __ANTI_DEBUG__

bool Player::Anti__CheatOccurred(const char* Reason,float Speed,uint16 Op,
                                float Val1,uint32 Val2,const MovementInfo* MvInfo, bool ForceReport)
{
    if(!Reason)
    {
		sLog->outInfo(LOG_FILTER_CHARACTER, "Anti__ReportCheat: Missing Reason parameter!");
        return false;
    }

    const uint32 CurTime=getMSTime();
    if(getMSTimeDiff(m_anti_lastalarmtime,CurTime) > sWorld->GetMvAnticheatAlarmPeriod())
        m_anti_alarmcount = 0;

    m_anti_lastalarmtime = CurTime;
    m_anti_alarmcount = m_anti_alarmcount + 1;

    if (!ForceReport && m_anti_alarmcount <= sWorld->GetMvAnticheatAlarmCount())
        return false;

    const char* Player = GetName().c_str();
    uint32 Acc = GetSession()->GetAccountId();
    uint32 Map = GetMapId();
    uint32 zone_id = GetZoneId();
    uint32 area_id = GetAreaId();
    float startX, startY, startZ;
    float endX = 0.0f, endY = 0.0f, endZ = 0.0f;
    uint32 fallTime = 0;
    uint32 t_guid = 0;
    uint32 flags = 0;

    MapEntry const* mapEntry = sMapStore.LookupEntry(GetMapId());
    AreaTableEntry const* zoneEntry = GetAreaEntryByAreaID(zone_id);
    AreaTableEntry const* areaEntry = GetAreaEntryByAreaID(area_id);

    std::string mapName(mapEntry ? mapEntry->name[GetSession()->GetSessionDbcLocale()] : "<unknown>");
    std::string zoneName(zoneEntry ? zoneEntry->area_name[GetSession()->GetSessionDbcLocale()] : "<unknown>");
    std::string areaName(areaEntry ? areaEntry->area_name[GetSession()->GetSessionDbcLocale()] : "<unknown>");

    CharacterDatabase.EscapeString(mapName);
    CharacterDatabase.EscapeString(zoneName);
    CharacterDatabase.EscapeString(areaName);

    if(!Player)
    {
        sLog->outInfo(LOG_FILTER_CHARACTER, "Anti__ReportCheat: Player with no name?!?");
        return false;
    }

    QueryResult Res=CharacterDatabase.PQuery("SELECT speed,Val1 FROM cheaters WHERE player='%s' AND reason LIKE '%s' AND Map='%u' AND last_date >= NOW()-300",Player,Reason,Map);
    if(Res)
    {
        Field* Fields = Res->Fetch();

        std::stringstream Query;
        Query << "UPDATE cheaters SET count=count+1,last_date=NOW()";
        Query.precision(5);
        if(Speed>0.0f && Speed > Fields[0].GetFloat())
            Query << ",speed='" << Speed << "'";

        if(Val1>0.0f && Val1 > Fields[1].GetFloat())
            Query << ",Val1='" << Val1 << "'";

        Query << " WHERE player='" << Player << "' AND reason='" << Reason << "' AND Map='" << Map << "' AND last_date >= NOW()-300 ORDER BY entry DESC LIMIT 1";

        CharacterDatabase.Execute(Query.str().c_str());
    }
    else
    {
        startX = GetPositionX();
        startY = GetPositionY();
        startZ = GetPositionZ();
		
        if(MvInfo)
        {
            fallTime = MvInfo->fallTime;
            flags = MvInfo->flags;
            t_guid = GUID_LOPART(MvInfo->t_guid);

            endX = MvInfo->pos.GetPositionX();
            endY = MvInfo->pos.GetPositionY();
            endZ = MvInfo->pos.GetPositionZ();
        }

        CharacterDatabase.PExecute("INSERT INTO cheaters (player,acctid,reason,speed,count,first_date,last_date,Op,Val1,Val2,Map,mapEntry,zone_id,zoneEntry,area_id,areaEntry,Level,startX,startY,startZ,endX,endY,endZ,t_guid,flags,fallTime) "
                                   "VALUES ('%s','%u','%s','%f','1',NOW(),NOW(),'%s','%f','%u','%u','%s','%u','%s','%u','%s','%u','%f','%f','%f','%f','%f','%f','%u','%u','%u')",
                                   Player,Acc,Reason,Speed,LookupOpcodeName(Op),Val1,Val2,
                                   Map,mapName.c_str(),
                                   zone_id,zoneName.c_str(),
                                   area_id,areaName.c_str(),
                                   getLevel(),
                                   startX,startY,startZ,
                                   endX,endY,endZ,
                                   t_guid,flags,fallTime);
    }

    if(sWorld->GetMvAnticheatKill() && isAlive())
        DealDamage(this, GetHealth(), NULL, DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, NULL, false);

    if(sWorld->GetMvAnticheatKick())
        GetSession()->KickPlayer();

    if(sWorld->GetMvAnticheatBan() & 1)
        sWorld->BanAccount(BAN_CHARACTER,Player,sWorld->GetMvAnticheatBanTime(),"Cheat","Anticheat");

    if(sWorld->GetMvAnticheatBan() & 2)
    {
        QueryResult result = LoginDatabase.PQuery("SELECT last_ip FROM account WHERE id=%u", Acc);
        if(result)
        {

            Field *fields = result->Fetch();
            std::string LastIP = fields[0].GetString();
            if(!LastIP.empty())
                sWorld->BanAccount(BAN_IP,LastIP,sWorld->GetMvAnticheatBanTime(),"Cheat","Anticheat");
        }
    }
    return true;
}


void WorldSession::HandleMoveWorldportAckOpcode(WorldPacket & /*recvData*/)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: got MSG_MOVE_WORLDPORT_ACK.");
    HandleMoveWorldportAckOpcode();
}

void WorldSession::HandleMoveWorldportAckOpcode()
{
    // ignore unexpected far teleports
    if (!GetPlayer()->IsBeingTeleportedFar())
        return;

    GetPlayer()->SetSemaphoreTeleportFar(false);

    // get the teleport destination
    WorldLocation const& loc = GetPlayer()->GetTeleportDest();

    // possible errors in the coordinate validity check
    if (!MapManager::IsValidMapCoord(loc))
    {
        LogoutPlayer(false);
        return;
    }

    // get the destination map entry, not the current one, this will fix homebind and reset greeting
    MapEntry const* mEntry = sMapStore.LookupEntry(loc.GetMapId());
    InstanceTemplate const* mInstance = sObjectMgr->GetInstanceTemplate(loc.GetMapId());

    // reset instance validity, except if going to an instance inside an instance
    if (GetPlayer()->m_InstanceValid == false && !mInstance)
        GetPlayer()->m_InstanceValid = true;

    Map* oldMap = GetPlayer()->GetMap();
    Map* newMap = sMapMgr->CreateMap(loc.GetMapId(), GetPlayer());

    if (GetPlayer()->IsInWorld())
    {
        sLog->outError(LOG_FILTER_NETWORKIO, "Player %s (GUID: %u) is still in world when teleported from map %s (%u) to new map %s (%u)", GetPlayer()->GetName().c_str(), GUID_LOPART(GetPlayer()->GetGUID()), oldMap->GetMapName(), oldMap->GetId(), newMap ? newMap->GetMapName() : "Unknown", loc.GetMapId());
        oldMap->RemovePlayerFromMap(GetPlayer(), false);
    }

    // relocate the player to the teleport destination
    // the CanEnter checks are done in TeleporTo but conditions may change
    // while the player is in transit, for example the map may get full
    if (!newMap || !newMap->CanEnter(GetPlayer()))
    {
        sLog->outError(LOG_FILTER_NETWORKIO, "Map %d (%s) could not be created for player %d (%s), porting player to homebind", loc.GetMapId(), newMap ? newMap->GetMapName() : "Unknown", GetPlayer()->GetGUIDLow(), GetPlayer()->GetName().c_str());
        GetPlayer()->TeleportTo(GetPlayer()->m_homebindMapId, GetPlayer()->m_homebindX, GetPlayer()->m_homebindY, GetPlayer()->m_homebindZ, GetPlayer()->GetOrientation());
        return;
    }
    else
        GetPlayer()->Relocate(&loc);

    GetPlayer()->ResetMap();
    GetPlayer()->SetMap(newMap);

    GetPlayer()->SendInitialPacketsBeforeAddToMap();
    if (!GetPlayer()->GetMap()->AddPlayerToMap(GetPlayer()))
    {
        sLog->outError(LOG_FILTER_NETWORKIO, "WORLD: failed to teleport player %s (%d) to map %d (%s) because of unknown reason!",
            GetPlayer()->GetName().c_str(), GetPlayer()->GetGUIDLow(), loc.GetMapId(), newMap ? newMap->GetMapName() : "Unknown");
        GetPlayer()->ResetMap();
        GetPlayer()->SetMap(oldMap);
        GetPlayer()->TeleportTo(GetPlayer()->m_homebindMapId, GetPlayer()->m_homebindX, GetPlayer()->m_homebindY, GetPlayer()->m_homebindZ, GetPlayer()->GetOrientation());
        return;
    }

    // battleground state prepare (in case join to BG), at relogin/tele player not invited
    // only add to bg group and object, if the player was invited (else he entered through command)
    if (_player->InBattleground())
    {
        // cleanup setting if outdated
        if (!mEntry->IsBattlegroundOrArena())
        {
            // We're not in BG
            _player->SetBattlegroundId(0, BATTLEGROUND_TYPE_NONE);
            // reset destination bg team
            _player->SetBGTeam(0);
        }
        // join to bg case
        else if (Battleground* bg = _player->GetBattleground())
        {
            if (_player->IsInvitedForBattlegroundInstance(_player->GetBattlegroundId()))
                bg->AddPlayer(_player);
        }
    }

    GetPlayer()->SendInitialPacketsAfterAddToMap();

    // flight fast teleport case
    if (GetPlayer()->GetMotionMaster()->GetCurrentMovementGeneratorType() == FLIGHT_MOTION_TYPE)
    {
        if (!_player->InBattleground())
        {
            // short preparations to continue flight
            FlightPathMovementGenerator* flight = (FlightPathMovementGenerator*)(GetPlayer()->GetMotionMaster()->top());
            flight->Initialize(GetPlayer());
            return;
        }

        // battleground state prepare, stop flight
        GetPlayer()->GetMotionMaster()->MovementExpired();
        GetPlayer()->CleanupAfterTaxiFlight();
    }

    // resurrect character at enter into instance where his corpse exist after add to map
    Corpse* corpse = GetPlayer()->GetCorpse();
    if (corpse && corpse->GetType() != CORPSE_BONES && corpse->GetMapId() == GetPlayer()->GetMapId())
    {
        if (mEntry->IsDungeon())
        {
            GetPlayer()->ResurrectPlayer(0.5f, false);
            GetPlayer()->SpawnCorpseBones();
        }
    }

    bool allowMount = !mEntry->IsDungeon() || mEntry->IsBattlegroundOrArena();
    if (mInstance)
    {
        Difficulty diff = GetPlayer()->GetDifficulty(mEntry->IsRaid());
        if (MapDifficulty const* mapDiff = GetMapDifficultyData(mEntry->MapID, diff))
        {
            if (mapDiff->resetTime)
            {
                if (time_t timeReset = sInstanceSaveMgr->GetResetTimeFor(mEntry->MapID, diff))
                {
                    uint32 timeleft = uint32(timeReset - time(NULL));
                    GetPlayer()->SendInstanceResetWarning(mEntry->MapID, diff, timeleft);
                }
            }
        }
        allowMount = mInstance->AllowMount;
    }

    // mount allow check
    if (!allowMount)
        _player->RemoveAurasByType(SPELL_AURA_MOUNTED);

    // update zone immediately, otherwise leave channel will cause crash in mtmap
    uint32 newzone, newarea;
    GetPlayer()->GetZoneAndAreaId(newzone, newarea);
    GetPlayer()->UpdateZone(newzone, newarea);

    // honorless target
    if (GetPlayer()->pvpInfo.IsHostile)
        GetPlayer()->CastSpell(GetPlayer(), 2479, true);

    // in friendly area
    else if (GetPlayer()->IsPvP() && !GetPlayer()->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_IN_PVP))
        GetPlayer()->UpdatePvP(false, false);

    // resummon pet
    GetPlayer()->ResummonPetTemporaryUnSummonedIfAny();
    GetPlayer()->Anti__SetLastTeleTime();

    //lets process all delayed operations on successful teleport
    GetPlayer()->ProcessDelayedOperations();
}

void WorldSession::HandleMoveTeleportAck(WorldPacket& recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "MSG_MOVE_TELEPORT_ACK");
    uint64 guid;

    recvData.readPackGUID(guid);

    uint32 flags, time;
    recvData >> flags >> time;
    sLog->outDebug(LOG_FILTER_NETWORKIO, "Guid " UI64FMTD, guid);
    sLog->outDebug(LOG_FILTER_NETWORKIO, "Flags %u, time %u", flags, time/IN_MILLISECONDS);

    Player* plMover = _player->m_mover->ToPlayer();

    if (!plMover || !plMover->IsBeingTeleportedNear())
        return;

    if (guid != plMover->GetGUID())
        return;

    plMover->SetSemaphoreTeleportNear(false);

    uint32 old_zone = plMover->GetZoneId();

    WorldLocation const& dest = plMover->GetTeleportDest();

    plMover->UpdatePosition(dest, true);

    uint32 newzone, newarea;
    plMover->GetZoneAndAreaId(newzone, newarea);
    plMover->UpdateZone(newzone, newarea);

    // new zone
    if (old_zone != newzone)
    {
        // honorless target
        if (plMover->pvpInfo.IsHostile)
            plMover->CastSpell(plMover, 2479, true);

        // in friendly area
        else if (plMover->IsPvP() && !plMover->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_IN_PVP))
            plMover->UpdatePvP(false, false);
    }

    // resummon pet
    GetPlayer()->ResummonPetTemporaryUnSummonedIfAny();
    plMover->Anti__SetLastTeleTime();

    //lets process all delayed operations on successful teleport
    GetPlayer()->ProcessDelayedOperations();
}

void WorldSession::HandleMovementOpcodes(WorldPacket& recvData)
{
    uint16 opcode = recvData.GetOpcode();

    Unit* mover = _player->m_mover;

    ASSERT(mover != NULL);                      // there must always be a mover

    Player* plrMover = mover->ToPlayer();

    // ignore, waiting processing in WorldSession::HandleMoveWorldportAckOpcode and WorldSession::HandleMoveTeleportAck
    if (plrMover && plrMover->IsBeingTeleported())
    {
        recvData.rfinish();                     // prevent warnings spam
        return;
    }

    /* extract packet */
    uint64 guid;

    recvData.readPackGUID(guid);

    MovementInfo movementInfo;
    movementInfo.guid = guid;
    ReadMovementInfo(recvData, &movementInfo);

    recvData.rfinish();                         // prevent warnings spam

    // prevent tampered movement data
    if (guid != mover->GetGUID())
        return;

    if (!movementInfo.pos.IsPositionValid())
    {
        recvData.rfinish();                     // prevent warnings spam
        return;
    }

    /* handle special cases */
    if (movementInfo.flags & MOVEMENTFLAG_ONTRANSPORT)
    {
        // transports size limited
        // (also received at zeppelin leave by some reason with t_* as absolute in continent coordinates, can be safely skipped)
        if (movementInfo.t_pos.GetPositionX() > 50 || movementInfo.t_pos.GetPositionY() > 50 || movementInfo.t_pos.GetPositionZ() > 50)
        {
            recvData.rfinish();                 // prevent warnings spam
            return;
        }

        if (!Trinity::IsValidMapCoord(movementInfo.pos.GetPositionX() + movementInfo.t_pos.GetPositionX(), movementInfo.pos.GetPositionY() + movementInfo.t_pos.GetPositionY(),
            movementInfo.pos.GetPositionZ() + movementInfo.t_pos.GetPositionZ(), movementInfo.pos.GetOrientation() + movementInfo.t_pos.GetOrientation()))
        {
            recvData.rfinish();                 // prevent warnings spam
            return;
        }

        // if we boarded a transport, add us to it
        if (plrMover)
        {
            if (!plrMover->GetTransport())
            {
                // elevators also cause the client to send MOVEMENTFLAG_ONTRANSPORT - just dismount if the guid can be found in the transport list
                for (MapManager::TransportSet::const_iterator iter = sMapMgr->m_Transports.begin(); iter != sMapMgr->m_Transports.end(); ++iter)
                {
                    if ((*iter)->GetGUID() == movementInfo.t_guid)
                    {
                        plrMover->m_transport = *iter;
                        (*iter)->AddPassenger(plrMover);
                        break;
                    }
                }
            }
            else if (plrMover->GetTransport()->GetGUID() != movementInfo.t_guid)
            {
                bool foundNewTransport = false;
                plrMover->m_transport->RemovePassenger(plrMover);
                for (MapManager::TransportSet::const_iterator iter = sMapMgr->m_Transports.begin(); iter != sMapMgr->m_Transports.end(); ++iter)
                {
                    if ((*iter)->GetGUID() == movementInfo.t_guid)
                    {
                        foundNewTransport = true;
                        plrMover->m_transport = *iter;
                        (*iter)->AddPassenger(plrMover);
                        break;
                    }
                }

                if (!foundNewTransport)
                {
                    plrMover->m_transport = NULL;
                    movementInfo.t_pos.Relocate(0.0f, 0.0f, 0.0f, 0.0f);
                    movementInfo.t_time = 0;
                    movementInfo.t_seat = -1;
                }
            }
        }

        if (!mover->GetTransport() && !mover->GetVehicle())
        {
            GameObject* go = mover->GetMap()->GetGameObject(movementInfo.t_guid);
            if (!go || go->GetGoType() != GAMEOBJECT_TYPE_TRANSPORT)
                movementInfo.flags &= ~MOVEMENTFLAG_ONTRANSPORT;
        }
    }
    else if (plrMover && plrMover->GetTransport())                // if we were on a transport, leave
    {
        plrMover->m_transport->RemovePassenger(plrMover);
        plrMover->m_transport = NULL;
        movementInfo.t_pos.Relocate(0.0f, 0.0f, 0.0f, 0.0f);
        movementInfo.t_time = 0;
        movementInfo.t_seat = -1;
    }

    // fall damage generation (ignore in flight case that can be triggered also at lags in moment teleportation to another map).
    if (opcode == MSG_MOVE_FALL_LAND && plrMover && !plrMover->isInFlight())
        plrMover->HandleFall(movementInfo);

    if (plrMover && ((movementInfo.flags & MOVEMENTFLAG_SWIMMING) != 0) != plrMover->IsInWater())
    {
        // now client not include swimming flag in case jumping under water
        plrMover->SetInWater(!plrMover->IsInWater() || plrMover->GetBaseMap()->IsUnderWater(movementInfo.pos.GetPositionX(), movementInfo.pos.GetPositionY(), movementInfo.pos.GetPositionZ()));
    }

    if (plrMover)
        sAnticheatMgr->StartHackDetection(plrMover, movementInfo, opcode);

    /*----------------------*/
    // ---- anti-cheat features -->>>
    if (plrMover && (plrMover->m_transport == 0) && sWorld->GetMvAnticheatEnable() &&
        GetPlayer()->GetSession()->GetSecurity() <= sWorld->GetMvAnticheatGmLevel() &&
        GetPlayer()->GetMotionMaster()->GetCurrentMovementGeneratorType()!=FLIGHT_MOTION_TYPE && // POINT_MOTION_TYPE 8 = charge; TARGETED_MOTION_TYPE 5=chase,follow (Mind Control by NPCs?) - better use "IDLE_MOTION_TYPE"
        (plrMover->Anti__GetLastTeleTime() + sWorld->GetMvAnticheatIgnoreAfterTeleport()) < time(NULL) )
    {
        /* I really don't care about movement-type yet (todo)
        UnitMoveType move_type;

        if (movementInfo.flags & MOVEMENTFLAG_FLYING) move_type = MOVE_FLY;
        else if (movementInfo.flags & MOVEMENTFLAG_SWIMMING) move_type = MOVE_SWIM;
        else if (movementInfo.flags & MOVEMENTFLAG_WALK_MODE) move_type = MOVE_WALK;
        else move_type = MOVE_RUN;*/

        float delta_x = GetPlayer()->GetPositionX() - movementInfo.pos.GetPositionX();
        float delta_y = GetPlayer()->GetPositionY() - movementInfo.pos.GetPositionY();
        float delta_z = GetPlayer()->GetPositionZ() - movementInfo.pos.GetPositionZ();
        float delta = sqrt(delta_x * delta_x + delta_y * delta_y); // Len of movement-vector via Pythagoras (a^2+b^2=Len^2)
        float angle = 0.0f;

        GetPlayer()->m_anti_MovedLen += delta;

        // Tangens of walking angel
        if (!(movementInfo.flags & (MOVEMENTFLAG_FLYING | MOVEMENTFLAG_SWIMMING)))
        {
            angle = ((delta > 0.0f) && (delta_z > 0.0f)) ? (atan(delta_z / delta) * 180.0f / M_PI) : 0.0f;
        }

        //antiOFF fall-damage, MOVEMENTFLAG_FALLING_FAR set by client if player try movement when falling and unset in this case the MOVEMENTFLAG_FALLING_FAR flag.
        if((GetPlayer()->m_anti_BeginFallZ == INVALID_HEIGHT) &&
           ((movementInfo.flags & (MOVEMENTFLAG_FALLING_FAR | MOVEMENTFLAG_PENDING_STOP)) != 0) &&
           !GetPlayer()->CanFlyAC1() && !GetPlayer()->HasAuraType(SPELL_AURA_FEATHER_FALL))
        {
            GetPlayer()->m_anti_BeginFallZ=movementInfo.pos.GetPositionZ();
        }

        const uint32 CurTime=getMSTime(); //TODO use client time
        if(getMSTimeDiff(GetPlayer()->m_anti_LastLenCheck,CurTime) >= 500)
        {
            float delta_xyt=GetPlayer()->m_anti_MovedLen/(float)(getMSTimeDiff(GetPlayer()->m_anti_LastLenCheck,CurTime));
            GetPlayer()->m_anti_LastLenCheck = CurTime;
            GetPlayer()->m_anti_MovedLen = 0.0f;

#ifdef __ANTI_DEBUG__
            SendAreaTriggerMessage("XYT: %f ; Flags: %s",delta_xyt,FlagsToStr(movementInfo.flags).c_str());
#endif //__ANTI_DEBUG__

            // MOVEMENTFLAG_ONTRANSPORT should already have been removed by the code earlier in this function if player is not on a transport, but maybe it can still be cheated so add zone/area IDs just to be sure
            // 2257=deep run tram, 3992=The Ancient Lift (HFojrd)-area!!, 2618=Jadefire Run (Felwood) (???)-area + 3988 (The Isle of Spears)-; 4384 - SotA (zone)
            if(delta_xyt > sWorld->GetMvAnticheatMaxXYT() && delta<=100.0f &&
                ((movementInfo.flags & MOVEMENTFLAG_ONTRANSPORT) == 0 || (GetPlayer()->GetZoneId() != 2257 && GetPlayer()->GetAreaId() != 3992 && GetPlayer()->GetAreaId() != 3988 && GetPlayer()->GetAreaId() != 2618 && GetPlayer()->GetZoneId() != 4384)) &&
                (movementInfo.flags & MOVEMENTFLAG_FALLING_FAR) == 0 )
            {
                GetPlayer()->Anti__CheatOccurred("Speed hack",delta_xyt,opcode,
                                                GetPlayer()->GetMotionMaster()->GetCurrentMovementGeneratorType(),
                                                getMSTimeDiff(plrMover->m_movementInfo.time,movementInfo.time),&movementInfo);
            }
        }

        if(delta > 100.0f &&
            ((movementInfo.flags & MOVEMENTFLAG_ONTRANSPORT) == 0 || (GetPlayer()->GetZoneId() != 2257 && GetPlayer()->GetAreaId() != 3992 && GetPlayer()->GetAreaId() != 3988 && GetPlayer()->GetAreaId() != 2618 && GetPlayer()->GetZoneId() != 4384)))
        {
            GetPlayer()->Anti__CheatOccurred("Tele hack",delta,opcode,
                GetPlayer()->GetMotionMaster()->GetCurrentMovementGeneratorType() /*Val1*/,
                getMSTimeDiff(plrMover->m_movementInfo.time,movementInfo.time) /*Val2*/,
                &movementInfo /*MvInfo*/, true /*ForceReport*/);
        }

        // Check for waterwalking
        if(((movementInfo.flags & MOVEMENTFLAG_WATERWALKING) != 0) &&
           ((movementInfo.flags ^ MOVEMENTFLAG_WATERWALKING) != 0) && // Client sometimes set waterwalk where it shouldn't do that...
           ((movementInfo.flags & MOVEMENTFLAG_FALLING) == 0) &&
           GetPlayer()->GetBaseMap()->IsUnderWater(movementInfo.pos.GetPositionX(), movementInfo.pos.GetPositionY(), movementInfo.pos.GetPositionZ()-4.0f) &&
           !(GetPlayer()->HasAuraType(SPELL_AURA_WATER_WALK) || GetPlayer()->HasAuraType(SPELL_AURA_GHOST)))
        {
            GetPlayer()->Anti__CheatOccurred("Water walking",0.0f,opcode,GetPlayer()->GetMotionMaster()->GetCurrentMovementGeneratorType(),0,&movementInfo);
        }

        // Check for walking upwards a mountain while not beeing able to do that
        /*if (angle > 50.0f &&
            !GetPlayer()->IsInWater() &&
            !GetPlayer()->IsFlying() &&
            !GetPlayer()->IsFalling() &&
            opcode == MSG_MOVE_HEARTBEAT)
        {
            GetPlayer()->Anti__CheatOccurred("Climb hack (test!!)",delta,opcode,angle,delta_z,&movementInfo);
        }*/


        static const float DIFF_OVERGROUND = 10.0f; //too much - aggro distance is 3+0.4+bounding_radius => change to 5
        float Anti__GroundZ = GetPlayer()->GetMap()->GetHeight(GetPlayer()->GetPositionX(),GetPlayer()->GetPositionY(),MAX_HEIGHT);
        float Anti__FloorZ  = GetPlayer()->GetMap()->GetHeight(GetPlayer()->GetPositionX(),GetPlayer()->GetPositionY(),GetPlayer()->GetPositionZ());
        //float Anti__FloorZ  = GetPlayer()->GetMap()->GetHeight(GetPlayer()->GetPositionX(),GetPlayer()->GetPositionY(),GetPlayer()->GetPositionZ(), true, 100.0f); //this is better, but maybe slower?
        float Anti__MapZ = ((Anti__FloorZ <= (INVALID_HEIGHT+5.0f)) ? Anti__GroundZ : Anti__FloorZ) + DIFF_OVERGROUND;

        if(!GetPlayer()->CanFlyAC1() &&
           !GetPlayer()->GetBaseMap()->IsUnderWater(movementInfo.pos.GetPositionX(), movementInfo.pos.GetPositionY(), movementInfo.pos.GetPositionZ()-4.0f) &&
           Anti__MapZ < GetPlayer()->GetPositionZ() && Anti__MapZ > (INVALID_HEIGHT+DIFF_OVERGROUND + 5.0f))
        {
            // Fly Hack ... alternatively one can check for MSG_MOVE_START_PITCH_UP/MSG_MOVE_START_PITCH_DOWN
            if((movementInfo.flags & plrMover->m_movementInfo.flags & (MOVEMENTFLAG_CAN_FLY | MOVEMENTFLAG_FLYING | MOVEMENTFLAG_DESCENDING/* | MOVEMENTFLAG_ASCENDING | MOVEMENTFLAG_PITCH_UP | MOVEMENTFLAG_PITCH_DOWN*/)) != 0)
            {
                GetPlayer()->Anti__CheatOccurred("Fly hack",
                                                GetPlayer()->GetPositionZ()-Anti__MapZ,
                                                opcode,GetPlayer()->GetMotionMaster()->GetCurrentMovementGeneratorType(),
                                                ((uint8)(GetPlayer()->HasAuraType(SPELL_AURA_FLY))) + // is included in canfly
                                                ((uint8)(GetPlayer()->HasAuraType(SPELL_AURA_MOD_INCREASE_FLIGHT_SPEED))*2)+ // not included in canfly, but should be present only together with spell_aura_fly
                                                ((uint8)(GetPlayer()->HasAuraType(SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED))*4), // is included in canfly
                                                &movementInfo);
            }

     //       static const float DIFF_AIRJUMP=25.0f; // 25 is realy high, but to many false positives...
            // Air-Jump-Detection definitively needs a better way to be detected...
            /* Need a better way to do that - currently a lot of fake alarms
            else if((Anti__MapZ+DIFF_AIRJUMP < GetPlayer()->GetPositionZ() &&
                     (movementInfo.flags & (MOVEMENTFLAG_FALLING_FAR | MOVEMENTFLAG_UNK4))==0) ||
                    (Anti__MapZ < GetPlayer()->GetPositionZ() &&
                     opcode==MSG_MOVE_JUMP))
            {
                GetPlayer()->Anti__CheatOccurred("Air Jump hack (test)",
                                    0.0f,opcode,0.0f,movementInfo.flags,&movementInfo);
            }*/
        }


        if(Anti__FloorZ < -199900.0f && Anti__GroundZ >= -199900.0f &&
           GetPlayer()->GetPositionZ()+5.0f < Anti__GroundZ &&
           GetPlayer()->GetPositionZ() == 0.0f && // lame check, but should work ;-)
           (movementInfo.flags & MOVEMENTFLAG_FALLING_FAR) == 0 )
        {
            GetPlayer()->Anti__CheatOccurred("Teleport2Plane hack",
                                             Anti__GroundZ,opcode,GetPlayer()->GetMotionMaster()->GetCurrentMovementGeneratorType(),0,&movementInfo);
        }
    }
    // <<---- anti-cheat features

    /* process position-change */
    WorldPacket data(opcode, recvData.size());
    movementInfo.time = getMSTime();
    movementInfo.guid = mover->GetGUID();
    WriteMovementInfo(&data, &movementInfo);
    mover->SendMessageToSet(&data, _player);

    mover->m_movementInfo = movementInfo;

    // this is almost never true (not sure why it is sometimes, but it is), normally use mover->IsVehicle()
    if (mover->GetVehicle())
    {
        mover->SetOrientation(movementInfo.pos.GetOrientation());
        return;
    }

    mover->UpdatePosition(movementInfo.pos);

    if (plrMover)                                            // nothing is charmed, or player charmed
    {
        plrMover->UpdateFallInformationIfNeed(movementInfo, opcode);

         float underMapValueZ;

        switch (plrMover->GetMapId())
        {
            case 617: underMapValueZ = 3.0f; break; // Dalaran Sewers
            case 618: underMapValueZ = 28.0f; break; // Ring of Valor
            default: underMapValueZ = -500.0f; break;
        }

        if (movementInfo.pos.GetPositionZ() < underMapValueZ)
        {
            if (!(plrMover->GetBattleground() && plrMover->GetBattleground()->HandlePlayerUnderMap(_player)))
            {
                // NOTE: this is actually called many times while falling
                // even after the player has been teleported away
                /// @todo discard movement packets after the player is rooted
                if (plrMover->isAlive())
                {
                    plrMover->EnvironmentalDamage(DAMAGE_FALL_TO_VOID, GetPlayer()->GetMaxHealth());
                    // player can be alive if GM/etc
                    // change the death state to CORPSE to prevent the death timer from
                    // starting in the next player update
                    if (!plrMover->isAlive())
                        plrMover->KillPlayer();
                }
            }
        }
    }
}

void WorldSession::HandleForceSpeedChangeAck(WorldPacket &recvData)
{
    uint32 opcode = recvData.GetOpcode();
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Recvd %s (%u, 0x%X) opcode", LookupOpcodeName(opcode), opcode, opcode);

    /* extract packet */
    uint64 guid;
    uint32 unk1;
    float  newspeed;

    recvData.readPackGUID(guid);

    // now can skip not our packet
    if (_player->GetGUID() != guid)
    {
        recvData.rfinish();                   // prevent warnings spam
        return;
    }

    // continue parse packet

    recvData >> unk1;                                      // counter or moveEvent

    MovementInfo movementInfo;
    movementInfo.guid = guid;
    ReadMovementInfo(recvData, &movementInfo);

    recvData >> newspeed;
    /*----------------*/

    // client ACK send one packet for mounted/run case and need skip all except last from its
    // in other cases anti-cheat check can be fail in false case
    UnitMoveType move_type;
    UnitMoveType force_move_type;

    static char const* move_type_name[MAX_MOVE_TYPE] = {  "Walk", "Run", "RunBack", "Swim", "SwimBack", "TurnRate", "Flight", "FlightBack", "PitchRate" };

    switch (opcode)
    {
        case CMSG_FORCE_WALK_SPEED_CHANGE_ACK:          move_type = MOVE_WALK;          force_move_type = MOVE_WALK;        break;
        case CMSG_FORCE_RUN_SPEED_CHANGE_ACK:           move_type = MOVE_RUN;           force_move_type = MOVE_RUN;         break;
        case CMSG_FORCE_RUN_BACK_SPEED_CHANGE_ACK:      move_type = MOVE_RUN_BACK;      force_move_type = MOVE_RUN_BACK;    break;
        case CMSG_FORCE_SWIM_SPEED_CHANGE_ACK:          move_type = MOVE_SWIM;          force_move_type = MOVE_SWIM;        break;
        case CMSG_FORCE_SWIM_BACK_SPEED_CHANGE_ACK:     move_type = MOVE_SWIM_BACK;     force_move_type = MOVE_SWIM_BACK;   break;
        case CMSG_FORCE_TURN_RATE_CHANGE_ACK:           move_type = MOVE_TURN_RATE;     force_move_type = MOVE_TURN_RATE;   break;
        case CMSG_FORCE_FLIGHT_SPEED_CHANGE_ACK:        move_type = MOVE_FLIGHT;        force_move_type = MOVE_FLIGHT;      break;
        case CMSG_FORCE_FLIGHT_BACK_SPEED_CHANGE_ACK:   move_type = MOVE_FLIGHT_BACK;   force_move_type = MOVE_FLIGHT_BACK; break;
        case CMSG_FORCE_PITCH_RATE_CHANGE_ACK:          move_type = MOVE_PITCH_RATE;    force_move_type = MOVE_PITCH_RATE;  break;
        default:
            sLog->outError(LOG_FILTER_NETWORKIO, "WorldSession::HandleForceSpeedChangeAck: Unknown move type opcode: %u", opcode);
            return;
    }

    // skip all forced speed changes except last and unexpected
    // in run/mounted case used one ACK and it must be skipped.m_forced_speed_changes[MOVE_RUN} store both.
    if (_player->m_forced_speed_changes[force_move_type] > 0)
    {
        --_player->m_forced_speed_changes[force_move_type];
        if (_player->m_forced_speed_changes[force_move_type] > 0)
            return;
    }

    if (!_player->GetTransport() && fabs(_player->GetSpeed(move_type) - newspeed) > 0.01f)
    {
        if (_player->GetSpeed(move_type) > newspeed)         // must be greater - just correct
        {
            sLog->outError(LOG_FILTER_NETWORKIO, "%sSpeedChange player %s is NOT correct (must be %f instead %f), force set to correct value",
                move_type_name[move_type], _player->GetName().c_str(), _player->GetSpeed(move_type), newspeed);
            _player->SetSpeed(move_type, _player->GetSpeedRate(move_type), true);
        }
        else                                                // must be lesser - cheating
        {
            sLog->outDebug(LOG_FILTER_GENERAL, "Player %s from account id %u kicked for incorrect speed (must be %f instead %f)",
                _player->GetName().c_str(), _player->GetSession()->GetAccountId(), _player->GetSpeed(move_type), newspeed);
            _player->GetSession()->KickPlayer();
        }
    }
}

void WorldSession::HandleSetActiveMoverOpcode(WorldPacket &recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Recvd CMSG_SET_ACTIVE_MOVER");

    uint64 guid;
    recvData >> guid;

    if (GetPlayer()->IsInWorld())
    {
        if (_player->m_mover->GetGUID() != guid)
            sLog->outError(LOG_FILTER_NETWORKIO, "HandleSetActiveMoverOpcode: incorrect mover guid: mover is " UI64FMTD " (%s - Entry: %u) and should be " UI64FMTD, guid, GetLogNameForGuid(guid), GUID_ENPART(guid), _player->m_mover->GetGUID());
    }
}

void WorldSession::HandleMoveNotActiveMover(WorldPacket &recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Recvd CMSG_MOVE_NOT_ACTIVE_MOVER");

    uint64 old_mover_guid;
    recvData.readPackGUID(old_mover_guid);

    MovementInfo mi;
    ReadMovementInfo(recvData, &mi);

    mi.guid = old_mover_guid;

    _player->m_movementInfo = mi;
}

void WorldSession::HandleMountSpecialAnimOpcode(WorldPacket& /*recvData*/)
{
    WorldPacket data(SMSG_MOUNTSPECIAL_ANIM, 8);
    data << uint64(GetPlayer()->GetGUID());

    GetPlayer()->SendMessageToSet(&data, false);
}

void WorldSession::HandleMoveKnockBackAck(WorldPacket& recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "CMSG_MOVE_KNOCK_BACK_ACK");

    uint64 guid;
    recvData.readPackGUID(guid);

    if (_player->m_mover->GetGUID() != guid)
        return;

    recvData.read_skip<uint32>();                          // unk

    MovementInfo movementInfo;
    ReadMovementInfo(recvData, &movementInfo);

    _player->m_movementInfo = movementInfo;

    WorldPacket data(MSG_MOVE_KNOCK_BACK, 66);
    data.appendPackGUID(guid);
    _player->BuildMovementPacket(&data);

    // knockback specific info
    data << movementInfo.j_sinAngle;
    data << movementInfo.j_cosAngle;
    data << movementInfo.j_xyspeed;
    data << movementInfo.j_zspeed;

    _player->SendMessageToSet(&data, false);
}

void WorldSession::HandleMoveHoverAck(WorldPacket& recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "CMSG_MOVE_HOVER_ACK");

    uint64 guid;                                            // guid - unused
    recvData.readPackGUID(guid);

    recvData.read_skip<uint32>();                          // unk

    MovementInfo movementInfo;
    ReadMovementInfo(recvData, &movementInfo);

    recvData.read_skip<uint32>();                          // unk2
}

void WorldSession::HandleMoveWaterWalkAck(WorldPacket& recvData)
{
    sLog->outDebug(LOG_FILTER_NETWORKIO, "CMSG_MOVE_WATER_WALK_ACK");

    uint64 guid;                                            // guid - unused
    recvData.readPackGUID(guid);

    recvData.read_skip<uint32>();                          // unk

    MovementInfo movementInfo;
    ReadMovementInfo(recvData, &movementInfo);

    recvData.read_skip<uint32>();                          // unk2
}

void WorldSession::HandleSummonResponseOpcode(WorldPacket& recvData)
{
    if (!_player->isAlive() || _player->isInCombat())
        return;

    uint64 summoner_guid;
    bool agree;
    recvData >> summoner_guid;
    recvData >> agree;

    _player->SummonIfPossible(agree);
}
