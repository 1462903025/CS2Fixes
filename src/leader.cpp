/**
 * =============================================================================
 * CS2Fixes
 * Copyright (C) 2023-2024 Source2ZE
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "leader.h"
#include "common.h"
#include "commands.h"
#include "gameevents.pb.h"
#include "zombiereborn.h"
#include "networksystem/inetworkmessages.h"

#include "tier0/memdbgon.h"

extern IVEngineServer2 *g_pEngineServer2;
extern CGameEntitySystem *g_pEntitySystem;
extern CGlobalVars *gpGlobals;
extern IGameEventManager2 *g_gameEventManager;

LeaderColor LeaderColorMap[] = {
	{"white",		Color(255, 255, 255, 255)}, // default if color finding func doesn't match any other color
	{"blue",		Color(40, 100, 255, 255)}, // Default CT color and first leader index
	{"orange",		Color(185, 93, 63, 255)}, // Default T color
	{"green",		Color(100, 230, 100, 255)},
	{"yellow",		Color(200, 200, 0, 255)},
	{"purple",		Color(164, 73, 255, 255)},
	{"red",			Color(214, 39, 40, 255)}, // Last leader index
};

const size_t g_nLeaderColorMapSize = sizeof(LeaderColorMap) / sizeof(LeaderColor);
CUtlVector<ZEPlayerHandle> g_vecLeaders;
int g_iLeaderIndex = 0;

// CONVAR_TODO
bool g_bEnableLeader = false;
static float g_flLeaderVoteRatio = 0.15;
static bool g_bLeaderActionsHumanOnly = true;
static bool g_bMutePingsIfNoLeader = true;
static std::string g_szLeaderModelPath = "";
static int g_iMarkerCount = 0;

FAKE_BOOL_CVAR(cs2f_leader_enable, "Whether to enable Leader features", g_bEnableLeader, false, false)
FAKE_FLOAT_CVAR(cs2f_leader_vote_ratio, "Vote ratio needed for player to become a leader", g_flLeaderVoteRatio, 0.2f, false)
FAKE_BOOL_CVAR(cs2f_leader_actions_ct_only, "Whether to allow leader actions (like !ldbeacon) only from human team", g_bLeaderActionsHumanOnly, true, false)
FAKE_BOOL_CVAR(cs2f_leader_mute_ping_no_leader, "Whether to mute player pings whenever there's no leader", g_bMutePingsIfNoLeader, true, false)
FAKE_STRING_CVAR(cs2f_leader_model_path, "Path to player model to be used for leaders", g_szLeaderModelPath, false)

int Leader_GetNeededLeaderVoteCount()
{
	int iOnlinePlayers = 0;

	for (int i = 0; i < gpGlobals->maxClients; i++)
	{
		ZEPlayer* pPlayer = g_playerManager->GetPlayer(i);

		if (pPlayer && !pPlayer->IsFakeClient())
		{
			iOnlinePlayers++;
		}
	}

	return (int)(iOnlinePlayers * g_flLeaderVoteRatio) + 1;
}

Color Leader_ColorFromString(const char* pszColorName)
{
	int iColorIndex = V_StringToInt32(pszColorName, -1);

	if (iColorIndex > -1)
		return LeaderColorMap[MIN(iColorIndex, g_nLeaderColorMapSize-1)].clColor;

	for (int i = 0; i < g_nLeaderColorMapSize; i++)
	{
		if (!V_stricmp(pszColorName, LeaderColorMap[i].pszColorName))
		{
			return LeaderColorMap[i].clColor;
		}
	}

	return LeaderColorMap[0].clColor;
}

bool Leader_NoLeaders()
{
	int iValidLeaders = 0;
	FOR_EACH_VEC_BACK(g_vecLeaders, i)
	{
		if (g_vecLeaders[i].IsValid())
			iValidLeaders++;
		else
			g_vecLeaders.Remove(i);
	}

	return !((bool)iValidLeaders);
}

void Leader_ApplyLeaderVisuals(CCSPlayerPawn *pPawn)
{
	CCSPlayerController *pController = CCSPlayerController::FromPawn(pPawn);
	ZEPlayer *pPlayer = g_playerManager->GetPlayer(pController->GetPlayerSlot());

	if (!g_szLeaderModelPath.empty())
	{
		pPawn->SetModel(g_szLeaderModelPath.c_str());
		pPawn->AcceptInput("Skin", 0);
	}

	pPawn->m_clrRender = LeaderColorMap[pPlayer->GetLeaderIndex()].clColor;
}

void Leader_RemoveLeaderVisuals(CCSPlayerPawn *pPawn)
{
	g_pZRPlayerClassManager->ApplyPreferredOrDefaultHumanClassVisuals(pPawn);
}

bool Leader_CreateDefendMarker(ZEPlayer *pPlayer, Color clrTint, int iDuration)
{
	CCSPlayerController *pController = CCSPlayerController::FromSlot(pPlayer->GetPlayerSlot());
	CCSPlayerPawn *pPawn = (CCSPlayerPawn *)pController->GetPawn();

	if (g_iMarkerCount >= 5)
	{
		ClientPrint(pController, HUD_PRINTTALK, CHAT_PREFIX "\xE5\xB7\xB2\xE6\xBF\x80\xE6\xB4\xBB\xE7\x9A\x84\xE6\xA0\x87\xE7\x82\xB9\xE5\xA4\xAA\xE5\xA4\x9A!");
		return false;
	}

	g_iMarkerCount++;

	new CTimer(iDuration, false, false, []()
	{
		if (g_iMarkerCount > 0)
			g_iMarkerCount--;

		return -1.0f;
	});

	CParticleSystem *pMarker = CreateEntityByName<CParticleSystem>("info_particle_system");

	Vector vecOrigin = pPawn->GetAbsOrigin();
	vecOrigin.z += 10;

	CEntityKeyValues* pKeyValues = new CEntityKeyValues();

	pKeyValues->SetString("effect_name", "particles/cs2fixes/leader_defend_mark.vpcf");
	pKeyValues->SetInt("tint_cp", 1);
	pKeyValues->SetColor("tint_cp_color", clrTint);
	pKeyValues->SetVector("origin", vecOrigin);
	pKeyValues->SetBool("start_active", true);

	pMarker->DispatchSpawn(pKeyValues);

	UTIL_AddEntityIOEvent(pMarker, "DestroyImmediately", nullptr, nullptr, "", iDuration);
	UTIL_AddEntityIOEvent(pMarker, "Kill", nullptr, nullptr, "", iDuration + 0.02f);

	return true;
}

void Leader_PostEventAbstract_Source1LegacyGameEvent(const uint64 *clients, const CNetMessage *pData)
{
	if (!g_bEnableLeader)
		return;

	auto pPBData = pData->ToPB<CMsgSource1LegacyGameEvent>();
	
	static int player_ping_id = g_gameEventManager->LookupEventId("player_ping");

	if (pPBData->eventid() != player_ping_id)
		return;

	// Don't kill ping visual when there's no leader, only mute the ping depending on cvar
	if (Leader_NoLeaders())
	{
		if (g_bMutePingsIfNoLeader)
			*(uint64 *)clients = 0;

		return;
	}

	IGameEvent *pEvent = g_gameEventManager->UnserializeEvent(*pPBData);

	ZEPlayer *pPlayer = g_playerManager->GetPlayer(pEvent->GetPlayerSlot("userid"));
	CCSPlayerController *pController = CCSPlayerController::FromSlot(pEvent->GetPlayerSlot("userid"));
	CBaseEntity *pEntity = (CBaseEntity*)g_pEntitySystem->GetEntityInstance(pEvent->GetEntityIndex("entityid"));

	g_gameEventManager->FreeEvent(pEvent);

	// no reason to block zombie pings. sound affected by sound block cvar
	if (pController->m_iTeamNum == CS_TEAM_T)
	{
		if (g_bMutePingsIfNoLeader)
			*(uint64 *)clients = 0;

		return;
	}

	// allow leader human pings
	if (pPlayer->IsLeader())
		return;

	// Remove entity responsible for visual part of the ping
	pEntity->Remove();

	// Block clients from playing the ping sound
	*(uint64 *)clients = 0;
}

void Leader_OnRoundStart(IGameEvent *pEvent)
{
	for (int i = 0; i < gpGlobals->maxClients; i++)
	{
		ZEPlayer *pPlayer = g_playerManager->GetPlayer((CPlayerSlot)i);

		if (pPlayer && !pPlayer->IsLeader())
			pPlayer->SetLeaderTracer(0);
	}

	g_iMarkerCount = 0;
}

// revisit this later with a TempEnt implementation
void Leader_BulletImpact(IGameEvent *pEvent)
{
	ZEPlayer *pPlayer = g_playerManager->GetPlayer(pEvent->GetPlayerSlot("userid"));

	if (!pPlayer)
		return;

	int iTracerIndex = pPlayer->GetLeaderTracer();

	if (!iTracerIndex)
		return;

	CCSPlayerPawn *pPawn = (CCSPlayerPawn *)pEvent->GetPlayerPawn("userid");
	CBasePlayerWeapon *pWeapon = pPawn->m_pWeaponServices->m_hActiveWeapon.Get();

	CParticleSystem* particle = CreateEntityByName<CParticleSystem>("info_particle_system");

	// Teleport particle to muzzle_flash attachment of player's weapon
	particle->AcceptInput("SetParent", "!activator", pWeapon, nullptr);
	particle->AcceptInput("SetParentAttachment", "muzzle_flash");

	CEntityKeyValues* pKeyValues = new CEntityKeyValues();

	// Event contains other end of the particle
	Vector vecData = Vector(pEvent->GetFloat("x"), pEvent->GetFloat("y"), pEvent->GetFloat("z"));
	Color clTint = LeaderColorMap[iTracerIndex].clColor;

	pKeyValues->SetString("effect_name", "particles/cs2fixes/leader_tracer.vpcf");
	pKeyValues->SetInt("data_cp", 1);
	pKeyValues->SetVector("data_cp_value", vecData);
	pKeyValues->SetInt("tint_cp", 2);
	pKeyValues->SetColor("tint_cp_color", clTint);
	pKeyValues->SetBool("start_active", true);

	particle->DispatchSpawn(pKeyValues);

	UTIL_AddEntityIOEvent(particle, "DestroyImmediately", nullptr, nullptr, "", 0.1f);
	UTIL_AddEntityIOEvent(particle, "Kill", nullptr, nullptr, "", 0.12f);
}

void Leader_Precache(IEntityResourceManifest *pResourceManifest)
{
	if (!g_szLeaderModelPath.empty())
		pResourceManifest->AddResource(g_szLeaderModelPath.c_str());
	pResourceManifest->AddResource("particles/cs2fixes/leader_tracer.vpcf");
	pResourceManifest->AddResource("particles/cs2fixes/leader_defend_mark.vpcf");
}

CON_COMMAND_CHAT(glow, "<name> [duration] - toggle glow highlight on a player")
{
	int iPlayerSlot = player ? player->GetPlayerSlot() : -1;
	ZEPlayer* pPlayer = g_playerManager->GetPlayer((CPlayerSlot)iPlayerSlot);

	bool bIsAdmin;
	if (pPlayer)
		bIsAdmin = pPlayer->IsAdminFlagSet(ADMFLAG_GENERIC);
	else // console
		bIsAdmin = true;

	Color color;
	int iDuration = 0;
	if (args.ArgC() == 3)
		iDuration = V_StringToInt32(args[2], 0);

	int iNumClients = 0;
	int pSlots[MAXPLAYERS];
	ETargetType nTargetType = g_playerManager->TargetPlayerString(iPlayerSlot, args[1], iNumClients, pSlots);

	if (bIsAdmin) // Admin command logic
	{
		if (args.ArgC() < 2)
		{
			ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "\xE7\x94\xA8\xE6\xB3\x95: !glow <\xE5\x90\x8D\xE5\xAD\x97> [\xE6\x8C\x81\xE7\xBB\xAD\xE6\x97\xB6\xE9\x97\xB4]");
			return;
		}

		if (!iNumClients)
		{
			ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "\xE6\x9C\xAA\xE6\x89\xBE\xE5\x88\xB0\xE7\x9B\xAE\xE6\xA0\x87.");
			return;
		}

		const char* pszCommandPlayerName = player ? player->GetPlayerName() : "Console";

		for (int i = 0; i < iNumClients; i++)
		{
			CCSPlayerController* pTarget = CCSPlayerController::FromSlot(pSlots[i]);

			if (!pTarget)
				continue;

			if (pTarget->m_iTeamNum < CS_TEAM_T)
				continue;

			// Exception - Use LeaderIndex color if Admin is also a Leader
			if (pPlayer && pPlayer->IsLeader())
				color = LeaderColorMap[pPlayer->GetLeaderIndex()].clColor;
			else
				color = pTarget->m_iTeamNum == CS_TEAM_T ? LeaderColorMap[2].clColor/*orange*/ : LeaderColorMap[1].clColor/*blue*/;

			ZEPlayer *pPlayerTarget = g_playerManager->GetPlayer(pSlots[i]);

			if (!pPlayerTarget->GetGlowModel())
				pPlayerTarget->StartGlow(color, iDuration);
			else
				pPlayerTarget->EndGlow();

			if (nTargetType < ETargetType::ALL)
				PrintSingleAdminAction(pszCommandPlayerName, pTarget->GetPlayerName(), "toggled glow on", "", CHAT_PREFIX);
		}

		PrintMultiAdminAction(nTargetType, pszCommandPlayerName, "toggled glow on", "", CHAT_PREFIX);

		return;
	}

	// Leader command logic

	if (!g_bEnableLeader)
		return;

	if (!pPlayer->IsLeader())
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "\xE6\x82\xA8\xE5\xBF\x85\xE9\xA1\xBB\xE6\x98\xAF\xE6\x8C\x87\xE6\x8C\xA5\xE6\x88\x96\xE7\xAE\xA1\xE7\x90\x86\xE5\x91\x98\xE6\x89\x8D\xE8\x83\xBD\xE4\xBD\xBF\xE7\x94\xA8\xE6\xAD\xA4\xE5\x91\xBD\xE4\xBB\xA4.");
		return;
	}

	if (args.ArgC() < 2)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "\xE7\x94\xA8\xE6\xB3\x95: !glow <name> [duration]");
		return;
	}

	if (player->m_iTeamNum != CS_TEAM_CT && g_bLeaderActionsHumanOnly)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "\xE6\x82\xA8\xE5\xBF\x85\xE9\xA1\xBB\xE6\x98\xAF\xE4\xBA\xBA\xE7\xB1\xBB\xE6\x89\x8D\xE8\x83\xBD\xE4\xBD\xBF\xE7\x94\xA8\xE6\xAD\xA4\xE5\x91\xBD\xE4\xBB\xA4.");
		return;
	}

	if (nTargetType > ETargetType::SELF)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "\xE4\xBD\xA0\xE5\xBF\x85\xE9\xA1\xBB\xE7\x9E\x84\xE5\x87\x86\xE4\xB8\x80\xE4\xB8\xAA\xE6\x8C\x87\xE5\xAE\x9A\xE7\x9A\x84\xE7\x8E\xA9\xE5\xAE\xB6.");
		return;
	}

	if (!iNumClients)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Target not found.");
		return;
	}

	if (iNumClients > 1)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "More than one player fit the target name.");
		return;
	}

	CCSPlayerController* pTarget = CCSPlayerController::FromSlot(pSlots[0]);

	if (!pTarget)
		return;

	if (pTarget->m_iTeamNum != CS_TEAM_CT)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "You can only place Leader glow on a human.");
		return;
	}

	color = LeaderColorMap[pPlayer->GetLeaderIndex()].clColor;

	ZEPlayer *pPlayerTarget = g_playerManager->GetPlayer(pSlots[0]);

	if (!pPlayerTarget->GetGlowModel())
	{
		pPlayerTarget->StartGlow(color, iDuration);
		ClientPrintAll(HUD_PRINTTALK, CHAT_PREFIX "Leader %s enabled glow on %s.", player->GetPlayerName(), pTarget->GetPlayerName());
	}
	else
	{
		pPlayerTarget->EndGlow();
		ClientPrintAll(HUD_PRINTTALK, CHAT_PREFIX "Leader %s disabled glow on %s.", player->GetPlayerName(), pTarget->GetPlayerName());
	}
}

CON_COMMAND_CHAT(vl, "<name> - \xE6\x8A\x95\xE7\xA5\xA8\xE9\x80\x89\xE5\x87\xBA\xE4\xB8\x80\xE5\x90\x8D\xE7\x8E\xA9\xE5\xAE\xB6\xE6\x88\x90\xE4\xB8\xBA\xE6\x8C\x87\xE6\x8C\xA5\xEF\xBC\x88\x40\x6D\x65\x20\xE6\x8A\x95\xE7\xBB\x99\xE8\x87\xAA\xE5\xB7\xB1\xEF\xBC\x89")
{
	if (!g_bEnableLeader)
		return;

	if (!player)
	{
		ClientPrint(player, HUD_PRINTCONSOLE, CHAT_PREFIX "\xE6\x82\xA8\xE6\x97\xA0\xE6\xB3\x95\xE4\xBB\x8E\xE6\x9C\x8D\xE5\x8A\xA1\xE5\x99\xA8\xE6\x8E\xA7\xE5\x88\xB6\xE5\x8F\xB0\xE4\xBD\xBF\xE7\x94\xA8\xE6\xAD\xA4\xE5\x91\xBD\xE4\xBB\xA4.");
		return;
	}

	if (args.ArgC() < 2)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Usage: !vl <name>");
		return;
	}

	int iPlayerSlot = player->GetPlayerSlot();
	int iNumClients = 0;
	int pSlot[MAXPLAYERS];

	ZEPlayer* pPlayer = g_playerManager->GetPlayer(iPlayerSlot);

	if (pPlayer->GetLeaderVoteTime() + 30.0f > gpGlobals->curtime)
	{
		int iRemainingTime = (int)(pPlayer->GetLeaderVoteTime() + 30.0f - gpGlobals->curtime);
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Wait %i seconds before you can !vl again.", iRemainingTime);
		return;
	}

	ETargetType nTargetType = g_playerManager->TargetPlayerString(iPlayerSlot, args[1], iNumClients, pSlot);
	if (nTargetType > ETargetType::SELF)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "\xE4\xBD\xA0\xE5\xBF\x85\xE9\xA1\xBB\xE7\x9E\x84\xE5\x87\x86\xE4\xB8\x80\xE4\xB8\xAA\xE7\x89\xB9\xE5\xAE\x9A\xE7\x9A\x84\xE7\x8E\xA9\xE5\xAE\xB6.");
		return;
	}

	if (!iNumClients)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "\xE6\x9C\xAA\xE6\x89\xBE\xE5\x88\xB0\xE7\x9B\xAE\xE6\xA0\x87.");
		return;
	}

	if (iNumClients > 1)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "\xE5\xA4\x9A\xE4\xB8\xAA\xE7\x8E\xA9\xE5\xAE\xB6\xE7\xAC\xA6\xE5\x90\x88\xE7\x9B\xAE\xE6\xA0\x87\xE5\x90\x8D\xE7\xA7\xB0.");
		return;
	}

	CCSPlayerController* pTarget = CCSPlayerController::FromSlot(pSlot[0]);

	if (!pTarget)
		return;

	ZEPlayer* pPlayerTarget = g_playerManager->GetPlayer(pSlot[0]);

	if (pPlayerTarget->IsLeader())
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "%s \xE7\x8E\xB0\xE5\x9C\xA8\xE6\x98\xAF\xE6\x8C\x87\xE6\x8C\xA5\xE4\xBA\x86.", pTarget->GetPlayerName());
		return;
	}

	if (pPlayerTarget->HasPlayerVotedLeader(pPlayer))
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "\xE4\xBD\xA0\xE6\x8A\x95\xE7\xBB\x99\xE4\xBA\x86 %s \xE6\x88\x90\xE4\xB8\xBA\xE6\x8C\x87\xE6\x8C\xA5.", pTarget->GetPlayerName());
		return;
	}

	int iLeaderVoteCount = pPlayerTarget->GetLeaderVoteCount();
	int iNeededLeaderVoteCount = Leader_GetNeededLeaderVoteCount();

	pPlayer->SetLeaderVoteTime(gpGlobals->curtime);

	if (iLeaderVoteCount + 1 >= iNeededLeaderVoteCount)
	{
		pPlayerTarget->SetLeader(++g_iLeaderIndex);
		pPlayerTarget->PurgeLeaderVotes();
		pPlayerTarget->SetLeaderTracer(g_iLeaderIndex);
		g_vecLeaders.AddToTail(pPlayerTarget->GetHandle());

		if (pTarget->m_iTeamNum == CS_TEAM_CT)
		{
			CCSPlayerPawn *pPawn = (CCSPlayerPawn *)pTarget->GetPawn();
			Leader_ApplyLeaderVisuals(pPawn);
		}

		Message("%s was voted for Leader with %i vote(s). LeaderIndex = %i\n", pTarget->GetPlayerName(), iNeededLeaderVoteCount, g_iLeaderIndex);

		ClientPrintAll(HUD_PRINTTALK, CHAT_PREFIX "%s \xE7\x8E\xB0\xE5\x9C\xA8\xE6\x98\xAF\xE6\x8C\x87\xE6\x8C\xA5!", pTarget->GetPlayerName());
		
		ClientPrint(pTarget, HUD_PRINTTALK, CHAT_PREFIX "\xE4\xBD\xA0\xE6\x88\x90\xE4\xB8\xBA\xE4\xBA\x86\xE6\x8C\x87\xE6\x8C\xA5\x2C\xE4\xBD\xBF\xE7\x94\xA8\x21\x6C\x65\x61\x64\x65\x72\x68\x65\x6C\x70\x20\xE5\x92\x8C\x20\x21\x6C\x65\x61\x64\x65\x72\x63\x6F\x6C\x6F\x72\x73\xE5\x91\xBD\xE4\xBB\xA4\xE6\x9D\xA5\xE5\x88\x97\xE5\x87\xBA\xE5\x8F\xAF\xE7\x94\xA8\xE7\x9A\x84\xE6\x8C\x87\xE6\x8C\xA5\xE5\x91\xBD\xE4\xBB\xA4\xE5\x92\x8C\xE9\xA2\x9C\xE8\x89\xB2\xE2\x80\x9D");

		// apply apparent leader perks (like leader model, glow(?)) here
		// also run a timer somewhere (per player or global) to reapply them

		return;
	}

	pPlayerTarget->AddLeaderVote(pPlayer);
	ClientPrintAll(HUD_PRINTTALK, CHAT_PREFIX "%s \xE5\xB8\x8C\xE6\x9C\x9B %s \xE6\x88\x90\xE4\xB8\xBA\xE6\x8C\x87\xE6\x8C\xA5 (%i/%i \xE7\xA5\xA8).",\
				player->GetPlayerName(), pTarget->GetPlayerName(), iLeaderVoteCount+1, iNeededLeaderVoteCount);
}

CON_COMMAND_CHAT(defend, "[\xE5\x90\x8D\xE5\xAD\x97|\xE6\x8C\x81\xE7\xBB\xAD\xE6\x97\xB6\xE9\x97\xB4] [\xE6\x8C\x81\xE7\xBB\xAD\xE6\x97\xB6\xE9\x97\xB4] - \xE5\x9C\xA8\xE4\xBD\xA0\xE6\x88\x96\xE7\x9B\xAE\xE6\xA0\x87\xE7\x8E\xA9\xE5\xAE\xB6\xE8\xBA\xAB\xE4\xB8\x8A\xE6\x94\xBE\xE7\xBD\xAE\xE9\x98\xB2\xE5\xAE\x88\xE6\xA0\x87\xE8\xAE\xB0")
{
	if (!g_bEnableLeader)
		return;

	if (!player)
	{
		ClientPrint(player, HUD_PRINTCONSOLE, CHAT_PREFIX "\xE6\x82\xA8\xE6\x97\xA0\xE6\xB3\x95\xE4\xBB\x8E\xE6\x9C\x8D\xE5\x8A\xA1\xE5\x99\xA8\xE6\x8E\xA7\xE5\x88\xB6\xE5\x8F\xB0\xE4\xBD\xBF\xE7\x94\xA8\xE6\xAD\xA4\xE5\x91\xBD\xE4\xBB\xA4.");
		return;
	}

	int iPlayerSlot = player->GetPlayerSlot();

	ZEPlayer* pPlayer = g_playerManager->GetPlayer(iPlayerSlot);

	if (!pPlayer->IsLeader())
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "\xE6\x82\xA8\xE5\xBF\x85\xE9\xA1\xBB\xE6\x98\xAF\xE6\x8C\x87\xE6\x8C\xA5\xE6\x89\x8D\xE8\x83\xBD\xE4\xBD\xBF\xE7\x94\xA8\xE6\xAD\xA4\xE5\x91\xBD\xE4\xBB\xA4.");
		return;
	}

	if (player->m_iTeamNum != CS_TEAM_CT && g_bLeaderActionsHumanOnly)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "\xE6\x82\xA8\xE5\xBF\x85\xE9\xA1\xBB\xE6\x98\xAF\xE4\xBA\xBA\xE7\xB1\xBB\xE6\x89\x8D\xE8\x83\xBD\xE4\xBD\xBF\xE7\x94\xA8\xE6\xAD\xA4\xE5\x91\xBD\xE4\xBB\xA4.");
		return;
	}

	// no arguments, place default duration marker on player
	if (args.ArgC() < 2)
	{
		if (player->m_iTeamNum != CS_TEAM_CT)
		{
			ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "\xE4\xBD\xA0\xE5\x8F\xAA\xE8\x83\xBD\xE5\x9C\xA8\xE4\xBA\xBA\xE8\xBA\xAB\xE4\xB8\x8A\xE6\x94\xBE\xE7\xBD\xAE\xE9\x98\xB2\xE5\xBE\xA1\xE6\xA0\x87\xE8\xAE\xB0.");
			return;
		}

		if (Leader_CreateDefendMarker(pPlayer, LeaderColorMap[pPlayer->GetLeaderIndex()].clColor, 30))
			ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "\xE5\x9C\xA8\xE8\x87\xAA\xE5\xB7\xB1\xE8\xBA\xAB\xE4\xB8\x8A\xE6\x94\xBE\xE7\xBD\xAE\xE9\x98\xB2\xE5\xBE\xA1\xE6\xA0\x87\xE8\xAE\xB0\xEF\xBC\x8C\xE6\x8C\x81\xE7\xBB\xAD\x33\x30\xE7\xA7\x92.");

		return;
	}

	int iNumClients = 0;
	int pSlot[MAXPLAYERS];
	ETargetType nTargetType = g_playerManager->TargetPlayerString(iPlayerSlot, args[1], iNumClients, pSlot);

	if (nTargetType > ETargetType::SELF)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "You must target a specific player.");
		return;
	}

	if (iNumClients > 1)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "More than one player fit the target name.");
		return;
	}

	// 1 argument, check if it's target or duration
	if (args.ArgC() == 2)
	{
		if (iNumClients) // valid target
		{
			CCSPlayerController *pTarget = CCSPlayerController::FromSlot(pSlot[0]);

			if (!pTarget)
				return;

			if (pTarget->m_iTeamNum != CS_TEAM_CT)
			{
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "\xE4\xBD\xA0\xE5\x8F\xAA\xE8\x83\xBD\xE5\x9C\xA8\xE4\xBA\xBA\xE8\xBA\xAB\xE4\xB8\x8A\xE6\x94\xBE\xE7\xBD\xAE\xE9\x98\xB2\xE5\xBE\xA1\xE6\xA0\x87\xE8\xAE\xB0.");
				return;
			}

			ZEPlayer* pTargetPlayer = g_playerManager->GetPlayer(pSlot[0]);

			if (Leader_CreateDefendMarker(pTargetPlayer, LeaderColorMap[pPlayer->GetLeaderIndex()].clColor, 30))
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX " %s \xE6\x94\xBE\xE7\xBD\xAE\xE4\xBA\x86\xE9\x98\xB2\xE5\xBE\xA1\xE6\xA0\x87\xE8\xAE\xB0\xEF\xBC\x8C\xE6\x8C\x81\xE7\xBB\xAD\x33\x30\xE7\xA7\x92.", pTarget->GetPlayerName());

			return;
		}

		if (player->m_iTeamNum != CS_TEAM_CT)
		{
			ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "\xE4\xBD\xA0\xE5\x8F\xAA\xE8\x83\xBD\xE5\x9C\xA8\xE4\xBA\xBA\xE8\xBA\xAB\xE4\xB8\x8A\xE6\x94\xBE\xE7\xBD\xAE\xE9\x98\xB2\xE5\xBE\xA1\xE6\xA0\x87\xE8\xAE\xB0.");
			return;
		}

		int iArg1 = V_StringToInt32(args[1], -1);

		if (iArg1 == -1) // target not found AND assume it's not a valid number
		{
			ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Target not found.");
			return;
		}

		if (iArg1 < 1)
		{
			if (Leader_CreateDefendMarker(pPlayer, LeaderColorMap[pPlayer->GetLeaderIndex()].clColor, 30))
				ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "\xE5\x9C\xA8\xE8\x87\xAA\xE5\xB7\xB1\xE8\xBA\xAB\xE4\xB8\x8A\xE6\x94\xBE\xE7\xBD\xAE\xE9\x98\xB2\xE5\xBE\xA1\xE6\xA0\x87\xE8\xAE\xB0\xEF\xBC\x8C\xE6\x8C\x81\xE7\xBB\xAD\x33\x30\xE7\xA7\x92.");

			return;
		}
		iArg1 = MIN(iArg1, 60);

		if (Leader_CreateDefendMarker(pPlayer, LeaderColorMap[pPlayer->GetLeaderIndex()].clColor, iArg1))
			ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "\xE5\x9C\xA8\xE8\x87\xAA\xE5\xB7\xB1\xE8\xBA\xAB\xE4\xB8\x8A\xE6\x94\xBE\xE7\xBD\xAE\xE9\x98\xB2\xE5\xBE\xA1\xE6\xA0\x87\xE8\xAE\xB0\xEF\xBC\x8C\xE6\x8C\x81\xE7\xBB\xAD %i \xE7\xA7\x92.", iArg1);

		return;
	}

	// args.ArgC() > 2

	if (!iNumClients) // 2 args provided, so invalid target is No Target
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Target not found.");
		return;
	}

	CCSPlayerController *pTarget = CCSPlayerController::FromSlot(pSlot[0]);


	if (!pTarget)
		return;

	if (pTarget->m_iTeamNum != CS_TEAM_CT)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "You can only place defend marker on a human.");
		return;
	}

	ZEPlayer* pTargetPlayer = g_playerManager->GetPlayer(pSlot[0]);


	int iArg2 = V_StringToInt32(args[2], -1);

	if (iArg2 < 1) // assume it's not a valid number
	{
		if (Leader_CreateDefendMarker(pTargetPlayer, LeaderColorMap[pPlayer->GetLeaderIndex()].clColor, 30))
			ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "\xE5\x9C\xA8 %s \xE4\xB8\x8A\xE6\x94\xBE\xE7\xBD\xAE\xE9\x98\xB2\xE5\xBE\xA1\xE6\xA0\x87\xE8\xAE\xB0\xEF\xBC\x8C\xE6\x8C\x81\xE7\xBB\xAD\x33\x30\xE7\xA7\x92.", pTarget->GetPlayerName());

		return;
	}

	iArg2 = MIN(iArg2, 60);

	if (Leader_CreateDefendMarker(pTargetPlayer, LeaderColorMap[pPlayer->GetLeaderIndex()].clColor, iArg2))
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "\xE5\x9C\xA8 %s \xE4\xB8\x8A\xE6\x94\xBE\xE7\xBD\xAE\xE9\x98\xB2\xE5\xBE\xA1\xE6\xA0\x87\xE8\xAE\xB0\xEF\xBC\x8C\xE6\x8C\x81\xE7\xBB\xAD %i \xE7\xA7\x92.", pTarget->GetPlayerName(), iArg2);
}

CON_COMMAND_CHAT(tracer, "<name> [color] - toggle projectile tracers on a player")
{
	if (!g_bEnableLeader)
		return;

	if (!player)
	{
		ClientPrint(player, HUD_PRINTCONSOLE, CHAT_PREFIX "You cannot use this command from the server console.");
		return;
	}

	int iPlayerSlot = player->GetPlayerSlot();

	ZEPlayer* pPlayer = g_playerManager->GetPlayer(iPlayerSlot);

	if (!pPlayer->IsLeader())
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "\xE6\x82\xA8\xE5\xBF\x85\xE9\xA1\xBB\xE6\x98\xAF\xE6\x8C\x87\xE6\x8C\xA5\xE6\x89\x8D\xE8\x83\xBD\xE4\xBD\xBF\xE7\x94\xA8\xE6\xAD\xA4\xE5\x91\xBD\xE4\xBB\xA4.");
		return;
	}

	if (player->m_iTeamNum != CS_TEAM_CT && g_bLeaderActionsHumanOnly)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "You must be a human to use this command.");
		return;
	}

	if (args.ArgC() < 2)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Usage: !tracer <name> [color]");
		return;
	}

	int iNumClients = 0;
	int pSlot[MAXPLAYERS];
	ETargetType nTargetType = g_playerManager->TargetPlayerString(iPlayerSlot, args[1], iNumClients, pSlot);

	if (nTargetType > ETargetType::SELF)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "You must target a specific player.");
		return;
	}

	if (!iNumClients)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Target not found.");
		return;
	}

	if (iNumClients > 1)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "More than one player fit the target name.");
		return;
	}

	CCSPlayerController* pTarget = CCSPlayerController::FromSlot(pSlot[0]);

	if (!pTarget)
		return;

	if (pTarget->m_iTeamNum != CS_TEAM_CT)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "You can only toggle tracers on a human.");
		return;
	}

	ZEPlayer* pPlayerTarget = g_playerManager->GetPlayer(pSlot[0]);

	if (pPlayerTarget->GetLeaderTracer())
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Disabled tracers for player %s.", pTarget->GetPlayerName());
		pPlayerTarget->SetLeaderTracer(0);
		return;
	}

	int iTracerIndex = 0;
	if (args.ArgC() < 3)
		iTracerIndex = pPlayer->GetLeaderIndex();
	else
	{
		int iIndex = V_StringToInt32(args[2], -1);

		if (iIndex > -1)
			iTracerIndex = MIN(iIndex, g_nLeaderColorMapSize-1);
		else
		{
			for (int i = 0; i < g_nLeaderColorMapSize; i++)
			{
				if (!V_stricmp(args[2], LeaderColorMap[i].pszColorName))
				{
					iTracerIndex = i;
					break;
				}
			}
		}
	}

	ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Enabled tracers for player %s.", pTarget->GetPlayerName());
	pPlayerTarget->SetLeaderTracer(iTracerIndex);
}

CON_COMMAND_CHAT(beacon, "<name> [color] - toggle beacon on a player")
{
	int iPlayerSlot = player ? player->GetPlayerSlot() : -1;
	ZEPlayer* pPlayer = g_playerManager->GetPlayer((CPlayerSlot)iPlayerSlot);

	bool bIsAdmin;
	if (pPlayer)
		bIsAdmin = pPlayer->IsAdminFlagSet(ADMFLAG_GENERIC);
	else // console
		bIsAdmin = true;

	int iNumClients = 0;
	int pSlots[MAXPLAYERS];
	ETargetType nTargetType = g_playerManager->TargetPlayerString(iPlayerSlot, args[1], iNumClients, pSlots);

	if (bIsAdmin) // Admin beacon logic
	{
		if (args.ArgC() < 2)
		{
			ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Usage: !beacon <name> [color]");
			return;
		}

		if (!iNumClients)
		{
			ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Target not found.");
			return;
		}

		const char* pszCommandPlayerName = player ? player->GetPlayerName() : "Console";

		Color color;
		if (args.ArgC() == 3)
			color = Leader_ColorFromString(args[2]);

		for (int i = 0; i < iNumClients; i++)
		{
			CCSPlayerController* pTarget = CCSPlayerController::FromSlot(pSlots[i]);

			if (!pTarget)
				continue;

			if (pTarget->m_iTeamNum < CS_TEAM_T)
				continue;

			// Exception - Use LeaderIndex color if Admin is also a Leader
			if (args.ArgC() == 2 && pPlayer && pPlayer->IsLeader())
				color = LeaderColorMap[pPlayer->GetLeaderIndex()].clColor;
			else if (args.ArgC() == 2)
				color = pTarget->m_iTeamNum == CS_TEAM_T ? LeaderColorMap[2].clColor/*orange*/ : LeaderColorMap[1].clColor/*blue*/;

			ZEPlayer *pPlayerTarget = g_playerManager->GetPlayer(pSlots[i]);

			if (!pPlayerTarget->GetBeaconParticle())
				pPlayerTarget->StartBeacon(color, pPlayer->GetHandle());
			else
				pPlayerTarget->EndBeacon();

			if (nTargetType < ETargetType::ALL)
				PrintSingleAdminAction(pszCommandPlayerName, pTarget->GetPlayerName(), "toggled beacon on", "", CHAT_PREFIX);
		}

		PrintMultiAdminAction(nTargetType, pszCommandPlayerName, "toggled beacon on", "", CHAT_PREFIX);

		return;
	}

	// Leader beacon logic

	if (!g_bEnableLeader)
		return;

	if (!pPlayer->IsLeader())
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "\xE6\x82\xA8\xE5\xBF\x85\xE9\xA1\xBB\xE6\x98\xAF\xE6\x8C\x87\xE6\x8C\xA5\xE6\x88\x96\xE7\xAE\xA1\xE7\x90\x86\xE5\x91\x98\xE6\x89\x8D\xE8\x83\xBD\xE4\xBD\xBF\xE7\x94\xA8\xE6\xAD\xA4\xE5\x91\xBD\xE4\xBB\xA4.");
		return;
	}

	if (args.ArgC() < 2)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Usage: !beacon <name> [color]");
		return;
	}

	if (player->m_iTeamNum != CS_TEAM_CT && g_bLeaderActionsHumanOnly)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "You must be a human to use this command.");
		return;
	}

	if (nTargetType > ETargetType::SELF)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "You must target a specific player.");
		return;
	}

	if (!iNumClients)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Target not found.");
		return;
	}

	if (iNumClients > 1)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "More than one player fit the target name.");
		return;
	}

	CCSPlayerController* pTarget = CCSPlayerController::FromSlot(pSlots[0]);

	if (!pTarget)
		return;

	if (pTarget->m_iTeamNum != CS_TEAM_CT)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "You can only place Leader beacon on a human.");
		return;
	}

	Color color;
	if (args.ArgC() == 3)
		color = Leader_ColorFromString(args[2]);
	else
		color = LeaderColorMap[pPlayer->GetLeaderIndex()].clColor;

	ZEPlayer *pPlayerTarget = g_playerManager->GetPlayer(pSlots[0]);

	if (!pPlayerTarget->GetBeaconParticle())
	{
		pPlayerTarget->StartBeacon(color, pPlayer->GetHandle());
		ClientPrintAll(HUD_PRINTTALK, CHAT_PREFIX "Leader %s enabled beacon on %s.", player->GetPlayerName(), pTarget->GetPlayerName());
	}
	else
	{
		pPlayerTarget->EndBeacon();
		ClientPrintAll(HUD_PRINTTALK, CHAT_PREFIX "Leader %s disabled beacon on %s.", player->GetPlayerName(), pTarget->GetPlayerName());
	}
}

CON_COMMAND_CHAT(leaders, "- list all current leaders")
{
	if (!g_bEnableLeader)
		return;

	int iDestination = player ? HUD_PRINTTALK : HUD_PRINTCONSOLE;

	if (Leader_NoLeaders()) // also wipes any invalid entries from g_vecLeaders
	{
		ClientPrint(player, iDestination, CHAT_PREFIX "There are currently no leaders.");
		return;
	}

	ClientPrint(player, iDestination, CHAT_PREFIX "List of current leaders:");

	FOR_EACH_VEC(g_vecLeaders, i)
	{
		ZEPlayer *pLeader = g_vecLeaders[i].Get();
		CCSPlayerController *pController = CCSPlayerController::FromSlot((CPlayerSlot) pLeader->GetPlayerSlot());

		ClientPrint(player, iDestination, CHAT_PREFIX "%s", pController->GetPlayerName());
	}
}

CON_COMMAND_CHAT(leaderhelp, "- list leader commands in chat")
{
	if (!g_bEnableLeader)
		return;

	int iDestination = player ? HUD_PRINTTALK : HUD_PRINTCONSOLE;

	ClientPrint(player, iDestination, CHAT_PREFIX "List of leader commands:");
	ClientPrint(player, iDestination, CHAT_PREFIX "!beacon <name> [color] - place a beacon on player");
	ClientPrint(player, iDestination, CHAT_PREFIX "!tracer <name> [color] - give player tracers");
	ClientPrint(player, iDestination, CHAT_PREFIX "!defend [name|duration] [duration] - place defend mark on player");
	ClientPrint(player, iDestination, CHAT_PREFIX "!glow <name> [duration] - toggle glow highlight on a player");
}

CON_COMMAND_CHAT(leadercolors, "- list leader colors in chat")
{
	if (!g_bEnableLeader)
		return;

	int iDestination = player ? HUD_PRINTTALK : HUD_PRINTCONSOLE;

	ClientPrint(player, iDestination, CHAT_PREFIX "List of leader colors:");
	for (int i = 0; i < g_nLeaderColorMapSize; i++)
	{
		ClientPrint(player, iDestination, CHAT_PREFIX "%i - %s", i, LeaderColorMap[i].pszColorName);
	}
}

CON_COMMAND_CHAT_FLAGS(forceld, "<name> [color]- forces leader status on a player", ADMFLAG_GENERIC)
{
	if (!g_bEnableLeader)
		return;

	if (args.ArgC() < 2)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Usage: !forceld <name> [index]");
		return;
	}

	int iPlayerSlot = player ? player->GetPlayerSlot() : -1;
	int iNumClients = 0;
	int pSlot[MAXPLAYERS];

	ETargetType nTargetType = g_playerManager->TargetPlayerString(iPlayerSlot, args[1], iNumClients, pSlot);
	if (nTargetType > ETargetType::SELF)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "You can only force specific player to be a leader.");
		return;
	}

	if (!iNumClients)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Target not found.");
		return;
	}

	if (iNumClients > 1)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "More than one player fit the target name.");
		return;
	}

	const char* pszCommandPlayerName = player ? player->GetPlayerName() : "Console";

	CCSPlayerController* pTarget = CCSPlayerController::FromSlot(pSlot[0]);

	if (!pTarget)
		return;

	ZEPlayer* pPlayerTarget = g_playerManager->GetPlayer(pSlot[0]);

	if (pPlayerTarget->IsLeader())
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "%s is already a leader.", pTarget->GetPlayerName());
		return;
	}

	if (args.ArgC() < 3)
	{
		pPlayerTarget->SetLeader(++g_iLeaderIndex);
		pPlayerTarget->SetLeaderTracer(g_iLeaderIndex);
	}
	else
	{
		int iColorIndex = V_StringToInt32(args[2], -1);

		if (iColorIndex > -1)
		{
			iColorIndex = MIN(iColorIndex, g_nLeaderColorMapSize-1);
		}
		else
		{
			for (int i = 0; i < g_nLeaderColorMapSize; i++)
			{
				if (!V_stricmp(args[2], LeaderColorMap[i].pszColorName))
				{
					iColorIndex = i;
					break;
				}
			}
		}

		if (iColorIndex > -1)
		{
			pPlayerTarget->SetLeader(iColorIndex);
			pPlayerTarget->SetLeaderTracer(iColorIndex);
		}
		else
		{
			pPlayerTarget->SetLeader(++g_iLeaderIndex);
			pPlayerTarget->SetLeaderTracer(g_iLeaderIndex);
		}
	}

	if (pTarget->m_iTeamNum == CS_TEAM_CT)
		Leader_ApplyLeaderVisuals((CCSPlayerPawn *)pTarget->GetPawn());

	pPlayerTarget->PurgeLeaderVotes();
	g_vecLeaders.AddToTail(pPlayerTarget->GetHandle());

	PrintSingleAdminAction(pszCommandPlayerName, pTarget->GetPlayerName(), "forced", " to be a Leader", CHAT_PREFIX);
}

CON_COMMAND_CHAT_FLAGS(stripld, "<name> - strips leader status from a player", ADMFLAG_GENERIC)
{
	if (!g_bEnableLeader)
		return;

	if (args.ArgC() < 2)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Usage: !stripld <name>");
		return;
	}

	int iPlayerSlot = player ? player->GetPlayerSlot() : -1;
	int iNumClients = 0;
	int pSlot[MAXPLAYERS];

	ETargetType nTargetType = g_playerManager->TargetPlayerString(iPlayerSlot, args[1], iNumClients, pSlot);
	if (nTargetType > ETargetType::SELF)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "You can only strip leader from a specific player.");
		return;
	}

	if (!iNumClients)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "Target not found.");
		return;
	}

	if (iNumClients > 1)
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "More than one player fit the target name.");
		return;
	}

	const char* pszCommandPlayerName = player ? player->GetPlayerName() : "Console";

	CCSPlayerController* pTarget = CCSPlayerController::FromSlot(pSlot[0]);

	if (!pTarget)
		return;

	ZEPlayer* pPlayerTarget = g_playerManager->GetPlayer(pSlot[0]);

	if (!pPlayerTarget->IsLeader())
	{
		ClientPrint(player, HUD_PRINTTALK, CHAT_PREFIX "%s is not a leader.", pTarget->GetPlayerName());
		return;
	}

	pPlayerTarget->SetLeader(0);
	pPlayerTarget->SetLeaderTracer(0);
	FOR_EACH_VEC(g_vecLeaders, i)
	{
		if (g_vecLeaders[i] == pPlayerTarget)
		{
			g_vecLeaders.Remove(i);
			break;
		}
	}

	if (pTarget->m_iTeamNum == CS_TEAM_CT)
		Leader_RemoveLeaderVisuals((CCSPlayerPawn *)pTarget->GetPawn());

	PrintSingleAdminAction(pszCommandPlayerName, pTarget->GetPlayerName(), "stripped leader from ", "", CHAT_PREFIX);
}