#include "instagib.h"

#include <string>

#include <interface.h>
#include <dbg.h>
#include <convar.h>
#include <messagebuffer.h>

using namespace std;

CInstaGib g_InstaGib;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CInstaGib, IClientPlugin, CLIENT_PLUGIN_INTERFACE_VERSION, g_InstaGib);

CMessageBuffer DamageBuffer;

UserMsgHookFn ORIG_UserMsgHook_Damage = NULL;
DetourHandle_t hUserMsgHook_Damage = 0;

int UserMsgHook_Damage(const char* pszName, int iSize, void* pBuffer)
{
	DamageBuffer.Init(pBuffer, iSize, true);

	DamageBuffer.ReadByte(); // armor (unused by plugin)
	int damageTaken = DamageBuffer.ReadByte();
	long bitsDamage = DamageBuffer.ReadLong();

	vec3_t vecFrom;
	for (int i = 0; i < 3; i++) vecFrom[i] = DamageBuffer.ReadCoord();

	if (damageTaken > 0 || (vecFrom[0] == 0 && vecFrom[1] == 0 && vecFrom[2] == 0)) g_pEngineFuncs->ClientCmd("gibme\n");

	return ORIG_UserMsgHook_Damage(pszName, iSize, pBuffer);
}

api_version_s CInstaGib::GetAPIVersion()
{
	return SVENMOD_API_VER;
}

bool CInstaGib::Load(CreateInterfaceFn pfnSvenModFactory, ISvenModAPI* pSvenModAPI, IPluginHelpers* pPluginHelpers)
{
	BindApiToGlobals(pSvenModAPI);

	hUserMsgHook_Damage = g_pHooks->HookUserMessage("Damage", UserMsgHook_Damage, &ORIG_UserMsgHook_Damage);

	return true;
}

void CInstaGib::PostLoad(bool bGlobalLoad)
{
}

void CInstaGib::Unload(void)
{
	g_pHooks->UnhookUserMessage(hUserMsgHook_Damage);
}

bool CInstaGib::Pause(void)
{
	return true;
}

void CInstaGib::Unpause(void)
{
}

void CInstaGib::GameFrame(client_state_t state, double frametime, bool bPostRunCmd)
{
}

PLUGIN_RESULT CInstaGib::Draw(void)
{
	return PLUGIN_CONTINUE;
}

PLUGIN_RESULT CInstaGib::DrawHUD(float time, int intermission)
{
	return PLUGIN_CONTINUE;
}

const char* CInstaGib::GetName(void)
{
	return "InstaGib";
}

const char* CInstaGib::GetAuthor(void)
{
	return "Steve (Noxturnix)";
}

const char* CInstaGib::GetVersion(void)
{
	return "1.0.0";
}

const char* CInstaGib::GetDescription(void)
{
	return "Automatically gib when getting damaged";
}

const char* CInstaGib::GetURL(void)
{
	return "https://github.com/Noxturnix/sven_instagib";
}

const char* CInstaGib::GetDate(void)
{
	return SVENMOD_BUILD_TIMESTAMP;
}

const char* CInstaGib::GetLogTag(void)
{
	return "INSTAGIB";
}
