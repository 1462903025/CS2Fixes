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

#include "idlemanager.h"
#include "commands.h"
#include <vprof.h>

extern IVEngineServer2 *g_pEngineServer2;
extern CGlobalVars *gpGlobals;
extern CPlayerManager* g_playerManager;

CIdleSystem* g_pIdleSystem = nullptr;

float g_fIdleKickTime = 0.0f;
static int g_iMinClientsForIdleKicks = 0;
static bool g_bKickAdmins = true;

FAKE_FLOAT_CVAR(cs2f_idle_kick_time, "Amount of minutes before kicking idle players. 0 to disable afk kicking.", g_fIdleKickTime, 0.0f, false)
FAKE_INT_CVAR(cs2f_idle_kick_min_players, "Minimum amount of connected clients to kick idle players.", g_iMinClientsForIdleKicks, 0, false)
FAKE_BOOL_CVAR(cs2f_idle_kick_admins, "Whether to kick idle players with ADMFLAG_GENERIC", g_bKickAdmins, true, false)

void CIdleSystem::CheckForIdleClients()
{
	if (m_bPaused || g_fIdleKickTime <= 0.0f)
		return;

	int iClientNum = 0;
	for (int i = 0; i < gpGlobals->maxClients; i++)
	{
		ZEPlayer* zPlayer = g_playerManager->GetPlayer(i);

		if (!zPlayer || zPlayer->IsFakeClient())
			continue;

		iClientNum++;
	}

	for (int i = 0; i < gpGlobals->maxClients; i++)
	{
		ZEPlayer* zPlayer = g_playerManager->GetPlayer(i);

		if (!zPlayer || zPlayer->IsFakeClient())
			continue;

		if (zPlayer->IsAuthenticated() && !g_bKickAdmins)
		{
			auto admin = g_pAdminSystem->FindAdmin(zPlayer->GetSteamId64());
			if (admin && admin->GetFlags() & ADMFLAG_GENERIC)
				continue;
		}

		time_t iIdleTimeLeft = (g_fIdleKickTime * 60) - (std::time(0) - zPlayer->GetLastInputTime());

		if (iIdleTimeLeft > 0)
		{
			if (iIdleTimeLeft < 60)
			{
				CCSPlayerController* pPlayer = CCSPlayerController::FromSlot(zPlayer->GetPlayerSlot());
				if (pPlayer)
					ClientPrint(pPlayer, HUD_PRINTTALK, CHAT_PREFIX "\xE4\xBD\xA0\xE6\x8C\x82\xE6\x9C\xBA\xE4\xBA\x86\x2C\xE5\xBF\xAB\xE5\x8A\xA8\xE4\xB8\x80\xE5\x8A\xA8,\xE5\xB0\x86\xE5\x9C\xA8\2 %i\1 \xE7\xA7\x92\xE5\x90\x8E\xE8\xB8\xA2\xE5\x87\xBA.", iIdleTimeLeft);
			}
			continue;
		}

		if (iClientNum < g_iMinClientsForIdleKicks)
			continue;

		g_pEngineServer2->DisconnectClient(zPlayer->GetPlayerSlot(), NETWORK_DISCONNECT_KICKED_IDLE);
	}
}

// Logged inputs and time for the logged inputs are updated every time this function is run.
void CIdleSystem::UpdateIdleTimes()
{
	if (g_fIdleKickTime <= 0.0f)
		return;

	VPROF("CIdleSystem::UpdateIdleTimes");

	for (int i = 0; i < gpGlobals->maxClients; i++)
	{
		ZEPlayer* pPlayer = g_playerManager->GetPlayer(i);

		if (!pPlayer)
			continue;

		CBasePlayerController* pTarget = (CBasePlayerController*)g_pEntitySystem->GetEntityInstance((CEntityIndex)(pPlayer->GetPlayerSlot().Get() + 1));
		if (!pTarget)
			continue;

		const auto pPawn = pTarget->m_hPawn();
		if (!pPawn)
			continue;

		const auto pMovement = pPawn->m_pMovementServices();
		uint64 iCurrentMovement = IN_NONE;
		if (pMovement)
		{
			const auto buttonStates = pMovement->m_nButtons().m_pButtonStates();
			iCurrentMovement = buttonStates[0];
		}
		const auto buttonsChanged = pPlayer->GetLastInputs() ^ iCurrentMovement;

		if (!buttonsChanged)
			continue;

		pPlayer->SetLastInputs(iCurrentMovement);
		pPlayer->UpdateLastInputTime();
	}
}

void CIdleSystem::Reset()
{
	m_bPaused = false;

	for (int i = 0; i < gpGlobals->maxClients; i++)
	{
		ZEPlayer* pPlayer = g_playerManager->GetPlayer(i);

		if (!pPlayer || pPlayer->IsFakeClient())
			continue;

		pPlayer->SetLastInputs(IN_NONE);
		pPlayer->UpdateLastInputTime();
	}
}