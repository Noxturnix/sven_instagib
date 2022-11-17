#include <interface.h>
#include <ISvenModAPI.h>
#include <ICommandLine.h>
#include <IEngineClient.h>
#include <IInventory.h>
#include <IFileSystem.h>
#include <IRegistry.h>
#include <ILoggingSystem.h>
#include <IMemoryUtils.h>
#include <IDetoursAPI.h>
#include <IVideoMode.h>
#include <ICvar.h>

#include <dbg.h>
#include <convar.h>
#include <svenmod_version.h>
#include <memutils/patterns.h>

#include <hl_sdk/engine/APIProxy.h>
#include <hl_sdk/common/netmsg.h>
#include <hl_sdk/common/sizebuf.h>
#include <hl_sdk/common/r_studioint.h>
#include <hl_sdk/pm_shared/pm_shared.h>
#include <hl_sdk/cl_dll/StudioModelRenderer.h>
#include <hl_sdk/cl_dll/ammohistory.h>
#include <hl_sdk/cl_dll/hud.h>

#include "svenmod.h"
#include "cvar.h"
#include "utils.h"
#include "vgui_iface.h"
#include "gameui_iface.h"
#include "inventory.h"
#include "plugins_manager.h"
#include "gamedata_finder.h"
#include "game_hooks.h"
#include "patterns.h"

// Game interfaces
cl_enginefuncs_t g_EngineFuncs;

cl_enginefuncs_t *g_pEngineFuncs = NULL;
cl_clientfuncs_t *g_pClientFuncs = NULL;
engine_studio_api_t *g_pEngineStudio = NULL;
r_studio_interface_t *g_pStudioAPI = NULL;
CStudioModelRenderer *g_pStudioRenderer = NULL;
IEngineClient *g_pEngineClient = NULL;
triangleapi_t *g_pTriangleAPI = NULL;
efx_api_t *g_pEffectsAPI = NULL;
event_api_t *g_pEventAPI = NULL;
demo_api_t *g_pDemoAPI = NULL;
net_api_t *g_pNetAPI = NULL;
IVoiceTweak *g_pVoiceTweak = NULL;
playermove_t *g_pPlayerMove = NULL;
IVideoMode *g_pVideoMode = NULL;
IVideoMode **g_ppVideoMode = NULL;
usermsg_t **g_ppClientUserMsgs = NULL;
netmsg_t *g_pNetworkMessages = NULL;

WeaponsResource *g_pWeaponsResource = NULL;
extra_player_info_t *g_pPlayerExtraInfo = NULL;

// net_message
sizebuf_t *g_pNetMessage = NULL;
int *g_pNetMessageReadCount = NULL;
int *g_pNetMessageBadRead = NULL;

// Common stuff
double *g_pRealtime = NULL;
double *g_pClientTime = NULL;
double *g_pFrametime = NULL;

const char *g_pszBaseDirectory = NULL;

static const char *s_pszClientVersion = NULL;
client_version_s g_ClientVersion;
int g_iEngineBuild = 0;
int g_iProtocolVersion = 0;

int *g_pClientState = NULL;
modules_s g_Modules;

// Disassemble struct
ud_t g_inst;

// Shared interfaces
ICommandLine *g_pCommandLine = NULL;
IFileSystem *g_pFileSystem = NULL;
IRegistry *g_pRegistry = NULL;

// Singleton
CSvenMod g_SvenMod;

//-----------------------------------------------------------------------------
// Function pointer
//-----------------------------------------------------------------------------

DECLARE_FUNC_PTR(int, __cdecl, build_number);

//-----------------------------------------------------------------------------
// Hooks
//-----------------------------------------------------------------------------

DECLARE_HOOK(int, __cdecl, Sys_InitGame, CreateInterfaceFn, const char *, void *, int);
DECLARE_HOOK(qboolean, __cdecl, Host_FilterTime, float);
DECLARE_HOOK(void, __cdecl, Host_Shutdown);
DECLARE_HOOK(int, __cdecl, Initialize, cl_enginefuncs_t *, int);
DECLARE_HOOK(void, __cdecl, LoadClientDLL, char *);

DECLARE_FUNC(int, __cdecl, HOOKED_Sys_InitGame, CreateInterfaceFn appSystemFactory, const char *pBaseDir, void *pwnd, int bIsDedicated)
{
	if ( bIsDedicated )
	{
		Sys_Error("[SvenMod] Running as dedicated server is not allowed");
	}

	int initialized = ORIG_Sys_InitGame(appSystemFactory, pBaseDir, pwnd, bIsDedicated);

	if ( initialized )
	{
		g_pszBaseDirectory = strdup(pBaseDir);
		g_SvenMod.SystemPostInit();
	}
	else
	{
		Sys_Error("[SvenMod] Engine failed initialization");
	}

	DetoursAPI()->RemoveDetour( g_SvenMod.m_hSys_InitGame );

	return initialized;
}

DECLARE_FUNC(qboolean, __cdecl, HOOKED_Host_FilterTime, float time)
{
	qboolean simulate = ORIG_Host_FilterTime(time);

	if ( simulate )
	{
		g_PluginsManager.Frame( *g_pClientState, *g_pFrametime, false );
	}

	return simulate;
}

DECLARE_FUNC(void, __cdecl, HOOKED_Host_Shutdown)
{
	// Unload all plugins before client DLL will be unloaded
	g_PluginsManager.UnloadPlugins();
	g_GameHooksHandler.Shutdown();

	g_CVar.DisablePrint();

	ORIG_Host_Shutdown();
}

DECLARE_FUNC(int, __cdecl, HOOKED_Initialize, cl_enginefuncs_t *pEngineFuncs, int iVersion)
{
	memcpy(&g_EngineFuncs, pEngineFuncs, sizeof(cl_enginefuncs_t));
	g_pEngineFuncs = &g_EngineFuncs;

	if ( iVersion != CLDLL_INTERFACE_VERSION )
	{
		LogWarning("Client's DLL version differs.\n");
	}

	int initialized = ORIG_Initialize(pEngineFuncs, iVersion);

	if ( !initialized )
	{
		Sys_Error("Client's DLL failed initialization");
	}

	g_pClientFuncs->Initialize = ORIG_Initialize;

	return initialized;
}

DECLARE_FUNC(void, __cdecl, HOOKED_LoadClientDLL, char *pszLibFileName)
{
	ORIG_LoadClientDLL(pszLibFileName);

	// If we were able to get here then client's library was linked correctly

	g_GameDataFinder.FindClientFuncs( &g_pClientFuncs, g_SvenMod.m_pfnLoadClientDLL );

	ORIG_Initialize = g_pClientFuncs->Initialize;
	g_pClientFuncs->Initialize = HOOKED_Initialize;

	DetoursAPI()->RemoveDetour( g_SvenMod.m_hLoadClientDLL );
}

//-----------------------------------------------------------------------------
// SvenMod implementation
//-----------------------------------------------------------------------------

bool CSvenMod::Init(ICommandLine *pCommandLine, IFileSystem *pFileSystem, IRegistry *pRegistry)
{
	// Sequence of calls
	// Launcher >> CSvenMod::Init >> LoadClientDLL (hook) >> cl_clientfuncs_s::Initialize (hook) >> Sys_InitGame (hook) >> CSvenMod::SystemPostInit >> CSvenMod::StartSystems

	g_pCommandLine = pCommandLine;
	g_pFileSystem = pFileSystem;
	g_pRegistry = pRegistry;

	DetoursAPI()->Init();

	if ( !LoggingSystem()->Init() )
		return false;

	LogMsg("Initializing SvenMod...\n");

	if ( !FindSignatures() )
		return false;

	if ( !AttachDetours() )
		return false;

	return true;
}

// Initialize the rest part
void CSvenMod::SystemPostInit()
{
	// Collect modules
#ifdef PLATFORM_WINDOWS
	HMODULE hSvenMod = Sys_GetModuleHandle("svenmod.dll");
	HMODULE hClientDLL = Sys_GetModuleHandle("client.dll");
	HMODULE hFileSystem = Sys_GetModuleHandle("filesystem_stdio.dll");
	HMODULE hGameUI = Sys_GetModuleHandle("GameUI.dll");
	HMODULE hVGUI = Sys_GetModuleHandle("vgui.dll");
	HMODULE hVGUI2 = Sys_GetModuleHandle("vgui2.dll");
	HMODULE hSDL2 = Sys_GetModuleHandle("SDL2.dll");
	HMODULE hVSTDLib = Sys_GetModuleHandle("vstdlib.dll");
	HMODULE hTier0 = Sys_GetModuleHandle("tier0.dll");
	HMODULE hOpenGL = Sys_GetModuleHandle("opengl32.dll");
	HMODULE hSteamAPI = Sys_GetModuleHandle("steam_api.dll");
#else
	HMODULE hSvenMod = Sys_GetModuleHandle("svenmod.so");
	HMODULE hClientDLL = Sys_GetModuleHandle("client.so");
	HMODULE hFileSystem = Sys_GetModuleHandle("filesystem_stdio.so");
	HMODULE hGameUI = Sys_GetModuleHandle("gameui.so");
	HMODULE hVGUI = Sys_GetModuleHandle("vgui.so");
	HMODULE hVGUI2 = Sys_GetModuleHandle("vgui2.so");
	HMODULE hSDL2 = Sys_GetModuleHandle("libSDL2-2.0.so.0");
	HMODULE hVSTDLib = Sys_GetModuleHandle("libvstdlib.so");
	HMODULE hTier0 = Sys_GetModuleHandle("libtier0.so");
	HMODULE hOpenGL = Sys_GetModuleHandle("libGL.so");
	HMODULE hSteamAPI = Sys_GetModuleHandle("steam_api.so");
#endif

	g_Modules.SvenMod = hSvenMod;
	g_Modules.Client = hClientDLL;
	g_Modules.FileSystem = hFileSystem;
	g_Modules.GameUI = hGameUI;
	g_Modules.VGUI = hVGUI;
	g_Modules.VGUI2 = hVGUI2;
	g_Modules.SDL2 = hSDL2;
	g_Modules.VSTDLib = hVSTDLib;
	g_Modules.Tier0 = hTier0;
	g_Modules.OpenGL = hOpenGL;
	g_Modules.SteamAPI = hSteamAPI;

	g_GameDataFinder.FindClientVersion( &s_pszClientVersion );
	CheckClientVersion();

	g_GameDataFinder.FindFrametime( &g_pRealtime, &g_pClientTime, &g_pFrametime, m_pfnHost_FilterTime );
	g_GameDataFinder.FindProtocolVersion( &g_iProtocolVersion );
	g_GameDataFinder.FindClientState( &g_pClientState );

	g_GameDataFinder.FindEngineStudio( &g_pEngineStudio, &g_pStudioAPI, m_pfnEngineStudioInit );
	g_GameDataFinder.FindEngineClient( &g_pEngineClient);
	g_GameDataFinder.FindStudioModelRenderer( &g_pStudioRenderer );
	g_GameDataFinder.FindPlayerMove( &g_pPlayerMove );
	g_GameDataFinder.FindVideoMode( &g_ppVideoMode, &g_pVideoMode, m_pfnVideoMode_Create );
	g_GameDataFinder.FindUserMessages( &g_ppClientUserMsgs );
	g_GameDataFinder.FindNetworkMessages( &g_pNetworkMessages, &g_pNetMessage, &g_pNetMessageReadCount, &g_pNetMessageBadRead );
	g_GameDataFinder.FindExtraPlayerInfo( &g_pPlayerExtraInfo );
	g_GameDataFinder.FindWeaponsResource( &g_pWeaponsResource );

	g_pTriangleAPI = g_pEngineFuncs->pTriAPI;
	g_pEffectsAPI = g_pEngineFuncs->pEfxAPI;
	g_pEventAPI = g_pEngineFuncs->pEventAPI;
	g_pDemoAPI = g_pEngineFuncs->pDemoAPI;
	g_pNetAPI = g_pEngineFuncs->pNetAPI;
	g_pVoiceTweak = g_pEngineFuncs->pVoiceTweak;

	g_VGUI.Init();
	g_VGameUI.Init();
	g_Inventory.Init();
	g_Utils.Init();
	g_CVar.Init();

#ifdef PLATFORM_WINDOWS
	// Let's not change engine functions so easily.. Just detour them for proper work of the game and SvenMod
	MemoryUtils()->VirtualProtect( g_pEngineStudio, sizeof(engine_studio_api_t), PAGE_EXECUTE_READ, NULL );
	MemoryUtils()->VirtualProtect( g_pStudioAPI, sizeof(r_studio_interface_t), PAGE_EXECUTE_READ, NULL );
	MemoryUtils()->VirtualProtect( g_pEngineFuncs, sizeof(cl_enginefuncs_t), PAGE_EXECUTE_READ, NULL );
	MemoryUtils()->VirtualProtect( g_pClientFuncs, sizeof(cl_clientfuncs_t), PAGE_EXECUTE_READ, NULL );

	MemoryUtils()->VirtualProtect( g_pTriangleAPI, sizeof(triangleapi_t), PAGE_EXECUTE_READ, NULL );
	MemoryUtils()->VirtualProtect( g_pEffectsAPI, sizeof(efx_api_t), PAGE_EXECUTE_READ, NULL );
	MemoryUtils()->VirtualProtect( g_pEventAPI, sizeof(event_api_t), PAGE_EXECUTE_READ, NULL );
	MemoryUtils()->VirtualProtect( g_pDemoAPI, sizeof(demo_api_t), PAGE_EXECUTE_READ, NULL );
	MemoryUtils()->VirtualProtect( g_pNetAPI, sizeof(net_api_t), PAGE_EXECUTE_READ, NULL );
	MemoryUtils()->VirtualProtect( g_pVoiceTweak, sizeof(IVoiceTweak), PAGE_EXECUTE_READ, NULL );

	MemoryUtils()->VirtualProtect( &g_ClientVersion, sizeof(client_version_s), PAGE_EXECUTE_READ, NULL );
	MemoryUtils()->VirtualProtect( &g_Modules, sizeof(modules_s), PAGE_EXECUTE_READ, NULL );
#endif

	g_iEngineBuild = build_number();

	LogMsg("Initialization finished.\n");

	StartSystems();
}

bool CSvenMod::StartSystems()
{
	ConColorMsg({ 40, 255, 40, 255 }, "Loaded SvenMod v%s (API version: %s)\n", SVENMOD_VERSION_STRING, SVENMOD_API_VERSION_STRING);
	LogMsg("SvenMod v%s.\n", SVENMOD_VERSION_STRING_FULL);

	ConVar_Register();

	g_GameHooksHandler.Init();
	g_PluginsManager.LoadPlugins();

	g_pEngineFuncs->ClientCmd("exec svenmod.cfg\n");

	return true;
}

void CSvenMod::Shutdown()
{
	DetoursAPI()->RemoveDetour( m_hHost_FilterTime );
	DetoursAPI()->RemoveDetour( m_hHost_Shutdown );

	ConVar_Unregister();

	g_CVar.Shutdown();
	g_pLoggingSystem->Shutdown();

	free( (void *)g_pszBaseDirectory );
	free( (void *)g_ClientVersion.string );
}

//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------

bool CSvenMod::FindSignatures()
{
#ifdef PLATFORM_WINDOWS
	HMODULE hHardwareDLL = Sys_GetModuleHandle("hw.dll");
#else
	HMODULE hHardwareDLL = Sys_GetModuleHandle("hw.so");
#endif

	g_Modules.Hardware = hHardwareDLL;

	if ( (build_number = (build_numberFn)MemoryUtils()->FindPattern( hHardwareDLL, Patterns::Hardware::build_number )) == NULL )
	{
		Sys_ErrorMessage("[SvenMod] Couldn't find function \"build_number\"");
		return false;
	}
	
	if ( (m_pfnLoadClientDLL = MemoryUtils()->FindPattern( hHardwareDLL, Patterns::Hardware::LoadClientDLL )) == NULL )
	{
		Sys_ErrorMessage("[SvenMod] Couldn't find function \"LoadClientDLL\"");
		return false;
	}
	
	if ( (m_pfnClientDLL_Init = MemoryUtils()->FindPattern( hHardwareDLL, Patterns::Hardware::ClientDLL_Init )) == NULL )
	{
		Sys_ErrorMessage("[SvenMod] Can't locate \"playermove\"");
		return false;
	}
	
	if ( (m_pfnSys_InitGame = MemoryUtils()->FindPattern( hHardwareDLL, Patterns::Hardware::Sys_InitGame )) == NULL )
	{
		Sys_ErrorMessage("[SvenMod] Couldn't find function \"Sys_InitGame\"");
		return false;
	}
	
	if ( (m_pfnHost_FilterTime = MemoryUtils()->FindPattern( hHardwareDLL, Patterns::Hardware::Host_FilterTime )) == NULL )
	{
		Sys_ErrorMessage("[SvenMod] Couldn't find function \"Host_FilterTime\"");
		return false;
	}
	
	if ( (m_pfnHost_Shutdown = MemoryUtils()->FindPattern( hHardwareDLL, Patterns::Hardware::Host_Shutdown )) == NULL )
	{
		Sys_ErrorMessage("[SvenMod] Couldn't find function \"Host_Shutdown\"");
		return false;
	}
	
	if ( (m_pfnEngineStudioInit = MemoryUtils()->FindPattern( hHardwareDLL, Patterns::Hardware::V_EngineStudio_Init )) == NULL )
	{
		Sys_ErrorMessage("[SvenMod] Can't locate \"engine_studio_api\"");
		return false;
	}
	
	if ( (m_pfnVideoMode_Create = MemoryUtils()->FindPattern( hHardwareDLL, Patterns::Hardware::V_VideoMode_Create )) == NULL )
	{
		Sys_ErrorMessage("[SvenMod] Can't locate \"videomode\"");
		return false;
	}

	return true;
}

bool CSvenMod::AttachDetours()
{
	m_hLoadClientDLL = DetoursAPI()->DetourFunction( m_pfnLoadClientDLL, HOOKED_LoadClientDLL, GET_FUNC_PTR(ORIG_LoadClientDLL) );

	if ( m_hLoadClientDLL == DETOUR_INVALID_HANDLE )
	{
		Sys_ErrorMessage("[SvenMod] Failed to hook function \"LoadClientDLL\"");
		return false;
	}

	m_hSys_InitGame = DetoursAPI()->DetourFunction( m_pfnSys_InitGame, HOOKED_Sys_InitGame, GET_FUNC_PTR(ORIG_Sys_InitGame) );
	
	if ( m_hSys_InitGame == DETOUR_INVALID_HANDLE )
	{
		Sys_ErrorMessage("[SvenMod] Failed to hook function \"Sys_InitGame\"");
		return false;
	}

	m_hHost_FilterTime = DetoursAPI()->DetourFunction( m_pfnHost_FilterTime, HOOKED_Host_FilterTime, GET_FUNC_PTR(ORIG_Host_FilterTime) );
	
	if ( m_hHost_FilterTime == DETOUR_INVALID_HANDLE )
	{
		Sys_ErrorMessage("[SvenMod] Failed to hook function \"Host_FilterTime\"");
		return false;
	}
	
	m_hHost_Shutdown = DetoursAPI()->DetourFunction( m_pfnHost_Shutdown, HOOKED_Host_Shutdown, GET_FUNC_PTR(ORIG_Host_Shutdown) );
	
	if ( m_hHost_Shutdown == DETOUR_INVALID_HANDLE )
	{
		Sys_ErrorMessage("[SvenMod] Failed to hook function \"Host_Shutdown\"");
		return false;
	}

	return true;
}

void CSvenMod::CheckClientVersion()
{
	if ( !s_pszClientVersion )
	{
		Sys_Error("[SvenMod] Failed to get client's version");
		return;
	}

	g_ClientVersion.string = strdup(s_pszClientVersion);

	if ( !g_ClientVersion.string )
	{
		Sys_Error("[SvenMod] Failed to get client's version");
		return;
	}

	char *buffer = const_cast<char *>(g_ClientVersion.string);
	char *pszFirstSeparator = NULL;

	while (*buffer)
	{
		if ( *buffer == '.' )
		{
			pszFirstSeparator = buffer;
			break;
		}

		buffer++;
	}

	if ( !pszFirstSeparator )
	{
		LogWarning("Tried to find client's version in format 'XX.XX' but got this: '%s'.\n", g_ClientVersion.string);
		g_ClientVersion.major_version = atoi(g_ClientVersion.string);
	}
	else
	{
		*pszFirstSeparator = 0;
		g_ClientVersion.major_version = atoi(g_ClientVersion.string);
		*pszFirstSeparator = '.';

		pszFirstSeparator += 1;

		if ( !*pszFirstSeparator )
		{
			Sys_Error("[SvenMod] Unexpected end of client's version");
		}
		else
		{
			g_ClientVersion.minor_version = atoi(pszFirstSeparator);
		}
	}

	g_ClientVersion.version = g_ClientVersion.major_version * 1000 + g_ClientVersion.minor_version;
}

//-----------------------------------------------------------------------------
// Export the interfaces
//-----------------------------------------------------------------------------

ICommandLine *CommandLine()
{
	return g_pCommandLine;
}

IFileSystem *FileSystem()
{
	return g_pFileSystem;
}

IRegistry *Registry()
{
	return g_pRegistry;
}

IVideoMode *VideoMode()
{
	if ( *g_ppVideoMode )
	{
		return *g_ppVideoMode;
	}

	Sys_Error("[SvenMod] VideoMode is NULL");

	return NULL;
}

EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CSvenMod, ISvenMod, SVENMOD_INTERFACE_VERSION, g_SvenMod);