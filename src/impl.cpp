/*
# impl.cpp

Implementation of hooks and other funcitons
*/

#include <subhook.h>
#include <BitStream.h>
#include <NetworkTypes.h>
#include <cstdint>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#else
#include <unistd.h>
#include <sys/mman.h>
#endif

#include <amx/amx.h>
#include <plugincommon.h>

#include "impl.hpp"
#include "addresses.hpp"
#include "common.hpp"

#ifdef _WIN32
#define STDCALL __stdcall
#define THISCALL __thiscall
#define FASTCALL __fastcall
#else
#define STDCALL
#define THISCALL
#define FASTCALL
#define CDECL
#endif

#define SUBHOOK_REMOVE(hookname) \
	if(hookname) \
	{ \
		subhook_remove(hookname); \
		subhook_free(hookname); \
	} \

subhook_t ClientJoin_hook;
subhook_t amx_Register_hook;

typedef void(CDECL* FUNC_ClientJoin_t)(RPCParameters *rpcParams);

constexpr int RPC_ClientJoin = 25;
constexpr int RPC_SetPlayerSkin = 153;
constexpr int RPC_WorldPlayerAdd = 32;
constexpr int RPC_RequestClass = 128;
constexpr int RPC_SetSpawnInfo = 68;
constexpr int RPC_ShowActor = 171;
constexpr int RPC_CreateObject = 44;
constexpr int RPC_ModelData = 179;

typedef int (THISCALL *RakNet__GetIndexFromPlayerID_t)(void* ppRakServer, PlayerID playerId);
RakNet__GetIndexFromPlayerID_t pfn__RakNet__GetIndexFromPlayerID = NULL;

inline bool IsPlayerConnected(int playerid)
{
	if (playerid < 0 || playerid >= MAX_PLAYERS || !pNetGame->pPlayerPool->bIsPlayerConnected[playerid])
		return false;

	return true;
}

// Y_Less - original YSF
bool Unlock(void *address, size_t len)
{
#ifdef _WIN32
	DWORD
		oldp;
	// Shut up the warnings :D
	return !!VirtualProtect(address, len, PAGE_EXECUTE_READWRITE, &oldp);
#else
	size_t
		iPageSize = getpagesize(),
		iAddr = ((reinterpret_cast <uint32_t>(address) / iPageSize) * iPageSize);
	return !mprotect(reinterpret_cast <void*>(iAddr), len, PROT_READ | PROT_WRITE | PROT_EXEC);
#endif
}

void CDECL HOOK_ClientJoin(RPCParameters *rpcParams)
{
	//4057 - 0.3.7-R2, 4062 - 0.3.DL-R1

	subhook_remove(ClientJoin_hook);

	int playerid = pfn__RakNet__GetIndexFromPlayerID(pRakServer, rpcParams->sender);

	logprintf("HOOK_ClientJoin called for playerid %d", playerid);

	int *ver = (int*)rpcParams->input;
	logprintf("*ver is %d", *ver);

	if (*ver == iCompatVersion)
	{
		logprintf("*ver == iCompatVersion succeeded, flagging player as compatibile", *ver);

		PlayerCompat[playerid] = true;
		logprintf("Client joined with version %d.", *ver);
		unsigned char namelen = rpcParams->input[5];
		unsigned int *resp = (unsigned int*)(rpcParams->input + 6 + namelen);
		logprintf("Resp %d.", *resp);
		*resp = *resp ^ *ver ^ iNetVersion;
		*ver = iNetVersion;
	}
	else
	{
		PlayerCompat[playerid] = false;
	}

	((FUNC_ClientJoin_t)Addresses::FUNC_ClientJoin)(rpcParams);

	subhook_install(ClientJoin_hook);
}

typedef bool (THISCALL *RakNet__RPC_t)(void* ppRakServer, BYTE* uniqueID, RakNet::BitStream* parameters, PacketPriority priority, PacketReliability reliability, unsigned orderingChannel, PlayerID playerId, bool broadcast, bool shiftTimestamp);
RakNet__RPC_t pfn__RakNet__RPC = NULL;


class RakHooks
{
public:
	static bool THISCALL RPC_037(void* ppRakServer, BYTE* uniqueID, RakNet::BitStream* parameters, PacketPriority priority, PacketReliability reliability, unsigned orderingChannel, PlayerID playerId, bool broadcast, bool shiftTimestamp)
	{
		if (playerId.binaryAddress == UNASSIGNED_PLAYER_ID.binaryAddress)
		{
			return pfn__RakNet__RPC(ppRakServer, uniqueID, parameters, priority, reliability, orderingChannel, playerId, broadcast, shiftTimestamp);
		}

		if (PlayerCompat[pfn__RakNet__GetIndexFromPlayerID(ppRakServer, playerId)])
		{

			switch (*uniqueID)
			{
				case RPC_SetPlayerSkin:
				{
					RakNet::BitStream bs(parameters->GetData(), parameters->GetNumberOfBytesUsed(), true);

					uint32_t wPlayerID, dSkinID;
					bs.Read(wPlayerID);
					bs.Read(dSkinID);

					bs.Reset();

					bs.Write((short)wPlayerID);
					bs.Write(dSkinID);
					bs.Write((uint32_t)0);

					return pfn__RakNet__RPC(ppRakServer, uniqueID, &bs, priority, reliability, orderingChannel, playerId, broadcast, shiftTimestamp);
				}
				case RPC_WorldPlayerAdd:
				{
					RakNet::BitStream bs(parameters->GetData(), parameters->GetNumberOfBytesUsed(), true);

					uint16_t wPlayerID;
					uint8_t team;
					uint32_t dSkinID;
					float PosX,
						PosY,
						PosZ,
						facing_angle;
					uint32_t player_color;
					uint8_t fighting_style;

					bs.Read(wPlayerID);
					bs.Read(team);
					bs.Read(dSkinID);
					bs.Read(PosX);
					bs.Read(PosY);
					bs.Read(PosZ);
					bs.Read(facing_angle);
					bs.Read(player_color);
					bs.Read(fighting_style);

					bs.Reset();

					bs.Write(wPlayerID);
					bs.Write(team);
					bs.Write(dSkinID);
					bs.Write((uint32_t)0); // Custom skin (although unused)
					bs.Write(PosX);
					bs.Write(PosY);
					bs.Write(PosZ);
					bs.Write(facing_angle);
					bs.Write(player_color);
					bs.Write(fighting_style);

					return pfn__RakNet__RPC(ppRakServer, uniqueID, &bs, priority, reliability, orderingChannel, playerId, broadcast, shiftTimestamp);
				}
				case RPC_ShowActor:
				{
					RakNet::BitStream bs(parameters->GetData(), parameters->GetNumberOfBytesUsed(), true);

					uint16_t wActorID;
					uint32_t SkinID;
					float X,
						Y,
						Z,
						Angle,
						health;

					bs.Read(wActorID);
					bs.Read(SkinID);
					bs.Read(X);
					bs.Read(Y);
					bs.Read(Z);
					bs.Read(Angle);
					bs.Read(health);

					bs.Reset();

					bs.Write(wActorID);
					bs.Write(SkinID);
					bs.Write((uint32_t)0); // Custom actor skin
					bs.Write(X);
					bs.Write(Y);
					bs.Write(Z);
					bs.Write(Angle);
					bs.Write(health);

					return pfn__RakNet__RPC(ppRakServer, uniqueID, &bs, priority, reliability, orderingChannel, playerId, broadcast, shiftTimestamp);
				}
				case RPC_RequestClass:
				{
					RakNet::BitStream bs(parameters->GetData(), parameters->GetNumberOfBytesUsed(), true);

					uint8_t byteRequestOutcome,
						byteTeam;
					int32_t iSkin;
					uint8_t unk;
					float vecPosX,
						vecPosY,
						vecPosZ,
						fRotation;
					int32_t iSpawnWeapons0,
						iSpawnWeapons1,
						iSpawnWeapons2,
						iSpawnWeaponsAmmo0,
						iSpawnWeaponsAmmo1,
						iSpawnWeaponsAmmo2;

					bs.Read(byteRequestOutcome);
					bs.Read(byteTeam);
					bs.Read(iSkin);
					bs.Read(unk);
					bs.Read(vecPosX);
					bs.Read(vecPosY);
					bs.Read(vecPosZ);
					bs.Read(fRotation);
					bs.Read(iSpawnWeapons0);
					bs.Read(iSpawnWeapons1);
					bs.Read(iSpawnWeapons2);
					bs.Read(iSpawnWeaponsAmmo0);
					bs.Read(iSpawnWeaponsAmmo1);
					bs.Read(iSpawnWeaponsAmmo2);

					bs.Reset();

					bs.Write(byteRequestOutcome);
					bs.Write(byteTeam);
					bs.Write(iSkin);
					bs.Write((uint32_t)0); // Custom actor skin (unused)
					bs.Write(unk);
					bs.Write(vecPosX);
					bs.Write(vecPosY);
					bs.Write(vecPosZ);
					bs.Write(fRotation);
					bs.Write(iSpawnWeapons0);
					bs.Write(iSpawnWeapons1);
					bs.Write(iSpawnWeapons2);
					bs.Write(iSpawnWeaponsAmmo0);
					bs.Write(iSpawnWeaponsAmmo1);
					bs.Write(iSpawnWeaponsAmmo2);

					return pfn__RakNet__RPC(ppRakServer, uniqueID, &bs, priority, reliability, orderingChannel, playerId, broadcast, shiftTimestamp);
				}
				case RPC_SetSpawnInfo:
				{
					RakNet::BitStream bs(parameters->GetData(), parameters->GetNumberOfBytesUsed(), true);

					uint8_t byteTeam;
					int32_t iSkin;
					uint8_t unk;
					float vecPosX,
						vecPosY,
						vecPosZ,
						fRotation;
					int32_t iSpawnWeapons0,
						iSpawnWeapons1,
						iSpawnWeapons2,
						iSpawnWeaponsAmmo0,
						iSpawnWeaponsAmmo1,
						iSpawnWeaponsAmmo2;

					bs.Read(byteTeam);
					bs.Read(iSkin);
					bs.Read(unk);
					bs.Read(vecPosX);
					bs.Read(vecPosY);
					bs.Read(vecPosZ);
					bs.Read(fRotation);
					bs.Read(iSpawnWeapons0);
					bs.Read(iSpawnWeapons1);
					bs.Read(iSpawnWeapons2);
					bs.Read(iSpawnWeaponsAmmo0);
					bs.Read(iSpawnWeaponsAmmo1);
					bs.Read(iSpawnWeaponsAmmo2);

					bs.Reset();

					bs.Write(byteTeam);
					bs.Write(iSkin);
					bs.Write((uint32_t)0); // Custom actor skin (unused)
					bs.Write(unk);
					bs.Write(vecPosX);
					bs.Write(vecPosY);
					bs.Write(vecPosZ);
					bs.Write(fRotation);
					bs.Write(iSpawnWeapons0);
					bs.Write(iSpawnWeapons1);
					bs.Write(iSpawnWeapons2);
					bs.Write(iSpawnWeaponsAmmo0);
					bs.Write(iSpawnWeaponsAmmo1);
					bs.Write(iSpawnWeaponsAmmo2);

					return pfn__RakNet__RPC(ppRakServer, uniqueID, &bs, priority, reliability, orderingChannel, playerId, broadcast, shiftTimestamp);
				}
			}
		}

		return pfn__RakNet__RPC(ppRakServer, uniqueID, parameters, priority, reliability, orderingChannel, playerId, broadcast, shiftTimestamp);
	}

	static bool THISCALL RPC_03DL(void* ppRakServer, BYTE* uniqueID, RakNet::BitStream* parameters, PacketPriority priority, PacketReliability reliability, unsigned orderingChannel, PlayerID playerId, bool broadcast, bool shiftTimestamp)
	{
		if (playerId.binaryAddress == UNASSIGNED_PLAYER_ID.binaryAddress)
		{
			return pfn__RakNet__RPC(ppRakServer, uniqueID, parameters, priority, reliability, orderingChannel, playerId, broadcast, shiftTimestamp);
		}
		if (PlayerCompat[pfn__RakNet__GetIndexFromPlayerID(ppRakServer, playerId)])
		{
			switch (*uniqueID)
			{
				case RPC_ModelData:
				{
					return true;
				}
				case RPC_SetPlayerSkin:
				{
					RakNet::BitStream bs(parameters->GetData(), parameters->GetNumberOfBytesUsed(), true);

					uint16_t wPlayerID;
					uint32_t dSkinID;

					bs.Read(wPlayerID);
					bs.Read(dSkinID);

					bs.Reset();

					bs.Write((uint32_t)wPlayerID);
					bs.Write(dSkinID);

					return pfn__RakNet__RPC(ppRakServer, uniqueID, &bs, priority, reliability, orderingChannel, playerId, broadcast, shiftTimestamp);
				}
				case RPC_WorldPlayerAdd:
				{
					RakNet::BitStream bs(parameters->GetData(), parameters->GetNumberOfBytesUsed(), true);

					uint16_t wPlayerID;
					uint8_t team;
					uint32_t dSkinID;
					// BS_IgnoreBits(bs, 32);
					float PosX,
						PosY,
						PosZ,
						facing_angle;
					uint32_t player_color;
					uint8_t fighting_style;

					bs.Read(wPlayerID);
					bs.Read(team);
					bs.Read(dSkinID);
					bs.IgnoreBits(32);
					bs.Read(PosX);
					bs.Read(PosY);
					bs.Read(PosZ);
					bs.Read(facing_angle);
					bs.Read(player_color);
					bs.Read(fighting_style);

					bs.Reset();

					bs.Write(wPlayerID);
					bs.Write(team);
					bs.Write(dSkinID);
					bs.Write(PosX);
					bs.Write(PosY);
					bs.Write(PosZ);
					bs.Write(facing_angle);
					bs.Write(player_color);
					bs.Write(fighting_style);

					return pfn__RakNet__RPC(ppRakServer, uniqueID, &bs, priority, reliability, orderingChannel, playerId, broadcast, shiftTimestamp);
				}
				case RPC_ShowActor:
				{
					RakNet::BitStream bs(parameters->GetData(), parameters->GetNumberOfBytesUsed(), true);

					uint16_t wActorID;
					uint32_t SkinID;
					float X,
						Y,
						Z,
						Angle,
						health;

					bs.Read(wActorID);
					bs.Read(SkinID);
					bs.IgnoreBits(32);
					bs.Read(X);
					bs.Read(Y);
					bs.Read(Z);
					bs.Read(Angle);
					bs.Read(health);

					bs.Reset();

					bs.Write(wActorID);
					bs.Write(SkinID);
					bs.Write(X);
					bs.Write(Y);
					bs.Write(Z);
					bs.Write(Angle);
					bs.Write(health);

					return pfn__RakNet__RPC(ppRakServer, uniqueID, &bs, priority, reliability, orderingChannel, playerId, broadcast, shiftTimestamp);
				}
				case RPC_SetSpawnInfo:
				{
					RakNet::BitStream bs(parameters->GetData(), parameters->GetNumberOfBytesUsed(), true);

					uint8_t byteTeam;
					int32_t iSkin;
					uint8_t unk;
					float vecPosX,
						vecPosY,
						vecPosZ,
						fRotation;
					int32_t iSpawnWeapons0,
						iSpawnWeapons1,
						iSpawnWeapons2,
						iSpawnWeaponsAmmo0,
						iSpawnWeaponsAmmo1,
						iSpawnWeaponsAmmo2;

					bs.Read(byteTeam);
					bs.Read(iSkin);
					bs.IgnoreBits(32);
					bs.Read(unk);
					bs.Read(vecPosX);
					bs.Read(vecPosY);
					bs.Read(vecPosZ);
					bs.Read(fRotation);
					bs.Read(iSpawnWeapons0);
					bs.Read(iSpawnWeapons1);
					bs.Read(iSpawnWeapons2);
					bs.Read(iSpawnWeaponsAmmo0);
					bs.Read(iSpawnWeaponsAmmo1);
					bs.Read(iSpawnWeaponsAmmo2);

					bs.Reset();

					bs.Write(byteTeam);
					bs.Write(iSkin);
					bs.Write(unk);
					bs.Write(vecPosX);
					bs.Write(vecPosY);
					bs.Write(vecPosZ);
					bs.Write(fRotation);
					bs.Write(iSpawnWeapons0);
					bs.Write(iSpawnWeapons1);
					bs.Write(iSpawnWeapons2);
					bs.Write(iSpawnWeaponsAmmo0);
					bs.Write(iSpawnWeaponsAmmo1);
					bs.Write(iSpawnWeaponsAmmo2);

					return pfn__RakNet__RPC(ppRakServer, uniqueID, &bs, priority, reliability, orderingChannel, playerId, broadcast, shiftTimestamp);
				}
				case RPC_RequestClass:
				{
					RakNet::BitStream bs(parameters->GetData(), parameters->GetNumberOfBytesUsed(), true);

					uint8_t byteRequestOutcome;
					uint8_t byteTeam;
					int32_t iSkin;
					uint8_t unk;
					FLOAT vecPosX,
						vecPosY,
						vecPosZ,
						fRotation;
					int32_t iSpawnWeapons0,
						iSpawnWeapons1,
						iSpawnWeapons2,
						iSpawnWeaponsAmmo0,
						iSpawnWeaponsAmmo1,
						iSpawnWeaponsAmmo2;

					bs.Read(byteRequestOutcome);
					bs.Read(byteTeam);
					bs.Read(iSkin);
					bs.IgnoreBits(32);
					bs.Read(unk);
					bs.Read(vecPosX);
					bs.Read(vecPosY);
					bs.Read(vecPosZ);
					bs.Read(fRotation);
					bs.Read(iSpawnWeapons0);
					bs.Read(iSpawnWeapons1);
					bs.Read(iSpawnWeapons2);
					bs.Read(iSpawnWeaponsAmmo0);
					bs.Read(iSpawnWeaponsAmmo1);
					bs.Read(iSpawnWeaponsAmmo2);

					bs.Reset();

					bs.Write(byteRequestOutcome);
					bs.Write(byteTeam);
					bs.Write(iSkin);
					bs.Write(unk);
					bs.Write(vecPosX);
					bs.Write(vecPosY);
					bs.Write(vecPosZ);
					bs.Write(fRotation);
					bs.Write(iSpawnWeapons0);
					bs.Write(iSpawnWeapons1);
					bs.Write(iSpawnWeapons2);
					bs.Write(iSpawnWeaponsAmmo0);
					bs.Write(iSpawnWeaponsAmmo1);
					bs.Write(iSpawnWeaponsAmmo2);

					return pfn__RakNet__RPC(ppRakServer, uniqueID, &bs, priority, reliability, orderingChannel, playerId, broadcast, shiftTimestamp);
				}
				case RPC_CreateObject:
				{
					unsigned int* wModelID = (unsigned int*)(parameters->GetData() + 2); // we can replace the packet itself here since it's never shown to more players at once

					if (*wModelID < 0)
						*wModelID = 18631; // NoModelFile

					return pfn__RakNet__RPC(ppRakServer, uniqueID, parameters, priority, reliability, orderingChannel, playerId, broadcast, shiftTimestamp);
				}
			}
		}
		return pfn__RakNet__RPC(ppRakServer, uniqueID, parameters, priority, reliability, orderingChannel, playerId, broadcast, shiftTimestamp);
	}
};

AMX_NATIVE ORIGINAL_n_SetPlayerVirtualWorld = NULL;

cell AMX_NATIVE_CALL HOOK_n_SetPlayerVirtualWorld(AMX* amx, cell* params)
{
	logprintf("HOOK_n_SetPlayerVirtualWorld(%d, %d) called", params[1], params[2]);

	if (!IsPlayerConnected(params[1]))
		return logprintf("player is not connected, returned 0"), 0;

	if (!Impl::IsPlayerCompat(params[1]))
		return logprintf("player is not compat, return original function"), ORIGINAL_n_SetPlayerVirtualWorld(amx, params);

	logprintf("player is under compat, use custom raw function");
	pNetGame->pPlayerPool->dwVirtualWorld[params[1]] = params[2];

	return 1;
}

typedef BYTE(*FUNC_amx_Register)(AMX *amx, AMX_NATIVE_INFO *nativelist, int number);
int AMXAPI HOOK_amx_Register(AMX *amx, AMX_NATIVE_INFO *nativelist, int number)
{
	// amx_Register hook for redirect natives
	static bool bSPVW_Hooked = false;

	if (bSPVW_Hooked)
	{
		for (int i = 0; nativelist[i].name; i++)
		{
			// If one matches GetGravity
			if (!strcmp(nativelist[i].name, "SetPlayerVirtualWorld"))
			{
				// Hook it
				bSPVW_Hooked = true;
				ORIGINAL_n_SetPlayerVirtualWorld = nativelist[i].func;
				nativelist[i].func = HOOK_n_SetPlayerVirtualWorld;
				break;
			}

			if (i == number - 1) break;
		}
	}

	return ((FUNC_amx_Register)subhook_get_trampoline(amx_Register_hook))(amx, nativelist, number);
}

void Impl::InstallPreHooks()
{
	ClientJoin_hook = subhook_new(reinterpret_cast<void*>(Addresses::FUNC_ClientJoin), reinterpret_cast<void*>(HOOK_ClientJoin), static_cast<subhook_flags_t>(0));
	subhook_install(ClientJoin_hook);

	if (currentVersion == SAMPVersion::VERSION_03DL_R1)
	{
		amx_Register_hook = subhook_new(reinterpret_cast<void*>(*(DWORD*)((DWORD)pAMXFunctions + (PLUGIN_AMX_EXPORT_Register * 4))), reinterpret_cast<void*>(HOOK_amx_Register), static_cast<subhook_flags_t>(0));
		subhook_install(amx_Register_hook);
	}
}

void Impl::InstallPostHooks()
{
	int *pRakServer_VTBL = ((int*)(*(void**)pRakServer));

	pfn__RakNet__RPC = (RakNet__RPC_t)pRakServer_VTBL[RAKNET_RPC_OFFSET];
	pfn__RakNet__GetIndexFromPlayerID = (RakNet__GetIndexFromPlayerID_t)pRakServer_VTBL[RAKNET_GET_INDEX_FROM_PLAYERID_OFFSET];

	Unlock((void*)&pRakServer_VTBL[RAKNET_RPC_OFFSET], 4);
	
	switch (currentVersion)
	{
		case SAMPVersion::VERSION_037_R2:
		{
			pRakServer_VTBL[RAKNET_RPC_OFFSET] = reinterpret_cast<int>(RakHooks::RPC_037);
			break;
		}
		case SAMPVersion::VERSION_03DL_R1:
		{
			pRakServer_VTBL[RAKNET_RPC_OFFSET] = reinterpret_cast<int>(RakHooks::RPC_03DL);
			break;
		}
	}
}

void Impl::UninstallHooks()
{
	SUBHOOK_REMOVE(ClientJoin_hook);

	if (currentVersion == SAMPVersion::VERSION_03DL_R1)
	{
		SUBHOOK_REMOVE(amx_Register_hook);
	}


}

bool Impl::IsPlayerCompat(int playerid)
{
	if (!IsPlayerConnected(playerid))
		return false;

	return PlayerCompat[playerid];
}