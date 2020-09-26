// ******************************************************************
// *
// *  This file is part of the Cxbx project.
// *
// *  Cxbx and Cxbe are free software; you can redistribute them
// *  and/or modify them under the terms of the GNU General Public
// *  License as published by the Free Software Foundation; either
// *  version 2 of the license, or (at your option) any later version.
// *
// *  This program is distributed in the hope that it will be useful,
// *  but WITHOUT ANY WARRANTY; without even the implied warranty of
// *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// *  GNU General Public License for more details.
// *
// *  You should have recieved a copy of the GNU General Public License
// *  along with this program; see the file COPYING.
// *  If not, write to the Free Software Foundation, Inc.,
// *  59 Temple Place - Suite 330, Bostom, MA 02111-1307, USA.
// *
// *  (c) 2002-2003 Aaron Robinson <caustik@caustik.com>
// *
// *  All rights reserved
// *
// ******************************************************************
#ifndef DIRECT3D9_H
#define DIRECT3D9_H

#include "core\hle\XAPI\Xapi.h" // For EMUPATCH

// NOTE: this is necessary or else d3dx9mesh.h fails to compile because of undefined VOID macros
#ifndef VOID
#define VOID void
#endif

#include "core\hle\D3D8\XbD3D8Types.h"

#define DIRECTDRAW_VERSION 0x0700
#include <ddraw.h>

extern void LookupTrampolines();

// initialize render window
extern void CxbxInitWindow(bool bFullInit);

extern void CxbxSetPixelContainerHeader
(
	xbox::X_D3DPixelContainer* pPixelContainer,
	DWORD           	Common,
	UINT				Width,
	UINT				Height,
	UINT				Levels,
	xbox::X_D3DFORMAT	Format,
	UINT				Dimensions,
	UINT				Pitch
);

extern uint8_t *ConvertD3DTextureToARGB(
	xbox::X_D3DPixelContainer *pXboxPixelContainer,
	uint8_t *pSrc,
	int *pWidth, int *pHeight,
	int TextureStage = 0
);

void CxbxUpdateNativeD3DResources();

// initialize direct3d
extern void EmuD3DInit();

// cleanup direct3d
extern void EmuD3DCleanup();

extern IDirect3DDevice *g_pD3DDevice;

extern xbox::dword_t g_Xbox_VertexShader_Handle;

extern xbox::X_PixelShader *g_pXbox_PixelShader;

extern xbox::X_D3DBaseTexture *g_pXbox_SetTexture[xbox::X_D3DTS_STAGECOUNT];

namespace xbox {

// ******************************************************************
// * patch: Direct3D_CreateDevice
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(Direct3D_CreateDevice)
(
    UINT                        Adapter,
    D3DDEVTYPE                  DeviceType,
    HWND                        hFocusWindow,
    dword_t                       BehaviorFlags,
    X_D3DPRESENT_PARAMETERS    *pPresentationParameters,
    IDirect3DDevice           **ppReturnedDeviceInterface
);

xbox::hresult_t WINAPI EMUPATCH(Direct3D_CreateDevice_16)
(
    UINT                        Adapter,
    D3DDEVTYPE                  DeviceType,
    HWND                        hFocusWindow,
    X_D3DPRESENT_PARAMETERS    *pPresentationParameters
);

xbox::hresult_t WINAPI EMUPATCH(Direct3D_CreateDevice_4)
(
    X_D3DPRESENT_PARAMETERS    *pPresentationParameters
);

// ******************************************************************
// * patch: IDirect3DResource8_IsBusy
// ******************************************************************
BOOL WINAPI EMUPATCH(D3DDevice_IsBusy)();

// ******************************************************************
// * patch: D3DDevice_GetCreationParameters
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_GetCreationParameters)
(
	D3DDEVICE_CREATION_PARAMETERS *pParameters
);

#if 0 // patch disabled
// ******************************************************************
// * patch: D3D_CheckDeviceFormat
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3D_CheckDeviceFormat)
(
    UINT                        Adapter,
    D3DDEVTYPE                  DeviceType,
    X_D3DFORMAT                 AdapterFormat,
    dword_t                       Usage,
    X_D3DRESOURCETYPE           RType,
    X_D3DFORMAT                 CheckFormat
);
#endif

#if 0 // patch disabled
// ******************************************************************
// * patch: D3DDevice_GetDeviceCaps
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_GetDeviceCaps)
(
    X_D3DCAPS                   *pCaps
);
#endif

// ******************************************************************
// * patch: D3DDevice_GetDisplayFieldStatus
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_GetDisplayFieldStatus)
(
	X_D3DFIELD_STATUS *pFieldStatus
);

// ******************************************************************
// * patch: D3DDevice_BeginPush
// ******************************************************************
xbox::PDWORD WINAPI EMUPATCH(D3DDevice_BeginPush)(dword_t Count);

// ******************************************************************
// * patch: D3DDevice_BeginPush2  //two arg version for xdk before 4531
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_BeginPush2)(dword_t Count, dword_t **ppPush);

// ******************************************************************
// * patch: D3DDevice_EndPush
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_EndPush)(dword_t *pPush);

// ******************************************************************
// * patch: D3DDevice_BeginVisibilityTest
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3DDevice_BeginVisibilityTest)();

// ******************************************************************
// * patch: D3DDevice_EndVisibilityTest
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3DDevice_EndVisibilityTest)
(
    dword_t                       Index
);

xbox::hresult_t __stdcall EMUPATCH(D3DDevice_EndVisibilityTest_0)();

// ******************************************************************
// * patch: D3DDevice_GetVisibilityTestResult
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3DDevice_GetVisibilityTestResult)
(
    dword_t                       Index,
    UINT                       *pResult,
    ULONGLONG                  *pTimeStamp
);

// ******************************************************************
// * patch: D3DDevice_SetBackBufferScale
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetBackBufferScale)(FLOAT x, FLOAT y);

// ******************************************************************
// * patch: D3DDevice_LoadVertexShader
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_LoadVertexShader)
(
    dword_t                       Handle,
    dword_t                       Address
);

xbox::void_t __stdcall EMUPATCH(D3DDevice_LoadVertexShader_0)();
xbox::void_t WINAPI EMUPATCH(D3DDevice_LoadVertexShader_4)
(
    dword_t                       Address
);

// ******************************************************************
// * patch: D3DDevice_SelectVertexShader
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SelectVertexShader)
(
    dword_t                       Handle,
    dword_t                       Address
);

xbox::void_t __stdcall EMUPATCH(D3DDevice_SelectVertexShader_0)();
xbox::void_t __stdcall EMUPATCH(D3DDevice_SelectVertexShader_4)
(
    dword_t                       Address
);

// ******************************************************************
// * patch: D3D_KickOffAndWaitForIdle
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3D_KickOffAndWaitForIdle)();

// ******************************************************************
// * patch: D3D_KickOffAndWaitForIdle
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3D_KickOffAndWaitForIdle2)(dword_t dwDummy1, dword_t dwDummy2);

// ******************************************************************
// * patch: D3DDevice_SetGammaRamp
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetGammaRamp)
(
    dword_t                   dwFlags,
    CONST X_D3DGAMMARAMP   *pRamp
);

// ******************************************************************
// * patch: D3DDevice_AddRef
// ******************************************************************
xbox::ulong_t WINAPI EMUPATCH(D3DDevice_AddRef)();

// ******************************************************************
// * patch: D3DDevice_BeginStateBlock
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_BeginStateBlock)();

// ******************************************************************
// * patch: D3DDevice_CaptureStateBlock
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_CaptureStateBlock)(dword_t Token);

// ******************************************************************
// * patch: D3DDevice_ApplyStateBlock
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_ApplyStateBlock)(dword_t Token);

// ******************************************************************
// * patch: D3DDevice_EndStateBlock
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3DDevice_EndStateBlock)(dword_t *pToken);

// ******************************************************************
// * patch: D3DDevice_CopyRects
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_CopyRects)
(
    X_D3DSurface       *pSourceSurface,
    CONST RECT         *pSourceRectsArray,
    UINT                cRects,
    X_D3DSurface       *pDestinationSurface,
    CONST POINT        *pDestPointsArray
);

// ******************************************************************
// * patch: D3DDevice_CreateImageSurface
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3DDevice_CreateImageSurface)
(
    UINT                Width,
    UINT                Height,
    X_D3DFORMAT         Format,
    X_D3DSurface      **ppBackBuffer
);

// ******************************************************************
// * patch: D3DDevice_GetGammaRamp
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_GetGammaRamp)
(
    X_D3DGAMMARAMP     *pRamp
);

// ******************************************************************
// * patch: D3DDevice_GetBackBuffer2
// ******************************************************************
X_D3DSurface* WINAPI EMUPATCH(D3DDevice_GetBackBuffer2)
(
    INT                 BackBuffer
);

// ******************************************************************
// * patch: D3DDevice_GetBackBuffer
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_GetBackBuffer)
(
    INT                 BackBuffer,
    D3DBACKBUFFER_TYPE  Type,
    X_D3DSurface      **ppBackBuffer
);

// ******************************************************************
// * patch: D3DDevice_SetViewport
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetViewport)
(
    CONST X_D3DVIEWPORT8 *pViewport
);

// ******************************************************************
// * patch: D3DDevice_GetViewport
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_GetViewport)
(
    X_D3DVIEWPORT8 *pViewport
);

// ******************************************************************
// * patch: D3DDevice_GetViewportOffsetAndScale
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_GetViewportOffsetAndScale)
(
	X_D3DXVECTOR4 *pOffset,
	X_D3DXVECTOR4 *pScale
);

xbox::void_t __stdcall EMUPATCH(D3DDevice_GetViewportOffsetAndScale_0)();

// ******************************************************************
// * patch: D3DDevice_SetShaderConstantMode
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetShaderConstantMode)
(
    xbox::X_VERTEXSHADERCONSTANTMODE Mode
);

xbox::void_t __stdcall EMUPATCH(D3DDevice_SetShaderConstantMode_0)();

// ******************************************************************
// * patch: D3DDevice_Reset
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3DDevice_Reset)
(
    X_D3DPRESENT_PARAMETERS *pPresentationParameters
);

// ******************************************************************
// * patch: D3DDevice_GetRenderTarget
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3DDevice_GetRenderTarget)
(
    X_D3DSurface  **ppRenderTarget
);

// ******************************************************************
// * patch: D3DDevice_GetRenderTarget
// ******************************************************************
X_D3DSurface * WINAPI EMUPATCH(D3DDevice_GetRenderTarget2)();

// ******************************************************************
// * patch: D3DDevice_GetDepthStencilSurface
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3DDevice_GetDepthStencilSurface)
(
    X_D3DSurface  **ppZStencilSurface
);

// ******************************************************************
// * patch: D3DDevice_GetDepthStencilSurface
// ******************************************************************
X_D3DSurface * WINAPI EMUPATCH(D3DDevice_GetDepthStencilSurface2)();

// ******************************************************************
// * patch: D3DDevice_GetTile
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_GetTile)
(
    dword_t           Index,
    X_D3DTILE      *pTile
);

// ******************************************************************
// * patch: D3DDevice_SetTile
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetTile)
(
    dword_t               Index,
    CONST X_D3DTILE    *pTile
);

// ******************************************************************
// * patch: D3DDevice_CreateVertexShader
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3DDevice_CreateVertexShader)
(
    CONST dword_t    *pDeclaration,
    CONST dword_t    *pFunction,
    dword_t          *pHandle,
    dword_t           Usage
);

// ******************************************************************
// * patch: D3DDevice_SetPixelShaderConstant
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetPixelShaderConstant)
(
    dword_t       Register,
    CONST PVOID pConstantData,
    dword_t       ConstantCount
);

xbox::void_t WINAPI EMUPATCH(D3DDevice_SetPixelShaderConstant_4)
(
    CONST PVOID pConstantData
);

// ******************************************************************
// * patch: D3DDevice_SetVertexShaderConstant
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetVertexShaderConstant)
(
    INT         Register,
    CONST PVOID pConstantData,
    dword_t       ConstantCount
);

xbox::void_t __stdcall EMUPATCH(D3DDevice_SetVertexShaderConstant_8)();

// ******************************************************************
// * patch: D3DDevice_SetVertexShaderConstant1
// ******************************************************************
xbox::void_t __fastcall EMUPATCH(D3DDevice_SetVertexShaderConstant1)
(
    INT         Register,
    CONST PVOID pConstantData
);

// ******************************************************************
// * patch: D3DDevice_SetVertexShaderConstant1Fast
// ******************************************************************
xbox::void_t __fastcall EMUPATCH(D3DDevice_SetVertexShaderConstant1Fast)
(
    INT         Register,
    CONST PVOID pConstantData
);

// ******************************************************************
// * patch: D3DDevice_SetVertexShaderConstant4
// ******************************************************************
xbox::void_t __fastcall EMUPATCH(D3DDevice_SetVertexShaderConstant4)
(
    INT         Register,
    CONST PVOID pConstantData
);

// ******************************************************************
// * patch: D3DDevice_SetVertexShaderConstantNotInline
// ******************************************************************
xbox::void_t __fastcall EMUPATCH(D3DDevice_SetVertexShaderConstantNotInline)
(
    INT         Register,
    CONST PVOID pConstantData,
    dword_t       ConstantCount
);

// ******************************************************************
// * patch: D3DDevice_SetVertexShaderConstantNotInlineFast
// ******************************************************************
xbox::void_t __fastcall EMUPATCH(D3DDevice_SetVertexShaderConstantNotInlineFast)
(
    INT         Register,
    CONST PVOID pConstantData,
    dword_t       ConstantCount
);

// ******************************************************************
// * patch: D3DDevice_DeletePixelShader
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_DeletePixelShader)
(
    dword_t          Handle
);

// ******************************************************************
// * patch: D3DDevice_CreatePixelShader
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3DDevice_CreatePixelShader)
(
    X_D3DPIXELSHADERDEF    *pPSDef,
    dword_t				   *pHandle
);

// ******************************************************************
// * patch: D3DDevice_SetPixelShader
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetPixelShader)
(
    dword_t           Handle
);

xbox::void_t WINAPI EMUPATCH(D3DDevice_SetPixelShader_0)();

// ******************************************************************
// * patch: D3DDevice_CreateTexture2
// ******************************************************************
X_D3DResource * WINAPI EMUPATCH(D3DDevice_CreateTexture2)
(
    UINT                Width,
    UINT                Height,
    UINT                Depth,
    UINT                Levels,
    dword_t               Usage,
    X_D3DFORMAT         Format,
    X_D3DRESOURCETYPE   D3DResource
);

// ******************************************************************
// * patch: D3DDevice_CreateTexture
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3DDevice_CreateTexture)
(
    UINT                Width,
    UINT                Height,
    UINT                Levels,
    dword_t               Usage,
    X_D3DFORMAT         Format,
    D3DPOOL             Pool,
    X_D3DTexture      **ppTexture
);

// ******************************************************************
// * patch: D3DDevice_CreateVolumeTexture
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3DDevice_CreateVolumeTexture)
(
    UINT                 Width,
    UINT                 Height,
    UINT                 Depth,
    UINT                 Levels,
    dword_t                Usage,
    X_D3DFORMAT          Format,
    D3DPOOL              Pool,
    X_D3DVolumeTexture **ppVolumeTexture
);

// ******************************************************************
// * patch: D3DDevice_CreateCubeTexture
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3DDevice_CreateCubeTexture)
(
    UINT                 EdgeLength,
    UINT                 Levels,
    dword_t                Usage,
    X_D3DFORMAT          Format,
    D3DPOOL              Pool,
    X_D3DCubeTexture  **ppCubeTexture
);

// ******************************************************************
// * patch: D3DDevice_CreateIndexBuffer
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3DDevice_CreateIndexBuffer)
(
    UINT                 Length,
    dword_t                Usage,
    X_D3DFORMAT          Format,
    D3DPOOL              Pool,
    X_D3DIndexBuffer   **ppIndexBuffer
);

// ******************************************************************
// * patch: D3DDevice_CreateIndexBuffer2
// ******************************************************************
X_D3DIndexBuffer * WINAPI EMUPATCH(D3DDevice_CreateIndexBuffer2)(UINT Length);

// ******************************************************************
// * patch: D3DDevice_SetIndices
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetIndices)
(
    X_D3DIndexBuffer   *pIndexData,
    UINT                BaseVertexIndex
);

// ******************************************************************
// * patch: D3DDevice_SetIndices_4
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetIndices_4)
(
    UINT                BaseVertexIndex
);

// ******************************************************************
// * patch: D3DDevice_SetTexture
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetTexture)
(
    dword_t           Stage,
	X_D3DBaseTexture  *pTexture
);

xbox::void_t WINAPI EMUPATCH(D3DDevice_SetTexture_4)
(
	X_D3DBaseTexture  *pTexture
);

// ******************************************************************
// * patch: D3DDevice_SwitchTexture
// ******************************************************************
xbox::void_t __fastcall EMUPATCH(D3DDevice_SwitchTexture)
(
    dword_t           Method,
    dword_t           Data,
    dword_t           Format
);

// ******************************************************************
// * patch: D3DDevice_GetDisplayMode
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_GetDisplayMode)
(
    X_D3DDISPLAYMODE         *pModes
);

// ******************************************************************
// * patch: D3DDevice_Begin
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_Begin)
(
    X_D3DPRIMITIVETYPE     PrimitiveType
);

// ******************************************************************
// * patch: D3DDevice_SetVertexData2f
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetVertexData2f)
(
    int     Register,
    FLOAT   a,
    FLOAT   b
);

// ******************************************************************
// * patch: D3DDevice_SetVertexData2s
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetVertexData2s)
(
    int     Register,
    short_t   a,
    short_t   b
);

// ******************************************************************
// * patch: D3DDevice_SetVertexData4f
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetVertexData4f)
(
    int     Register,
    FLOAT   a,
    FLOAT   b,
    FLOAT   c,
    FLOAT   d
);

// ******************************************************************
// * patch: D3DDevice_SetVertexData4f_16
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetVertexData4f_16)
(
	FLOAT   a,
	FLOAT   b,
	FLOAT   c,
	FLOAT   d
);

// ******************************************************************
// * patch: D3DDevice_SetVertexData4ub
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetVertexData4ub)
(
	INT		Register,
	byte_t	a,
	byte_t	b,
	byte_t	c,
	byte_t	d
);

// ******************************************************************
// * patch: D3DDevice_SetVertexData4s
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetVertexData4s)
(
	INT		Register,
	short_t	a,
	short_t	b,
	short_t	c,
	short_t	d
);

// ******************************************************************
// * patch: D3DDevice_SetVertexDataColor
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetVertexDataColor)
(
    int         Register,
    D3DCOLOR    Color
);

// ******************************************************************
// * patch: D3DDevice_End
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_End)();

// ******************************************************************
// * patch: D3DDevice_RunPushBuffer
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_RunPushBuffer)
(
    X_D3DPushBuffer       *pPushBuffer,
    X_D3DFixup            *pFixup
);

// ******************************************************************
// * patch: D3DDevice_Clear
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_Clear)
(
    dword_t                  Count,
    CONST D3DRECT         *pRects,
    dword_t                  Flags,
    D3DCOLOR               Color,
    float                  Z,
    dword_t                  Stencil
);

// ******************************************************************
// * patch: D3DDevice_Present
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_Present)
(
    CONST RECT* pSourceRect,
    CONST RECT* pDestRect,
    PVOID       pDummy1,
    PVOID       pDummy2
);

// ******************************************************************
// * patch: D3DDevice_Swap
// ******************************************************************
dword_t WINAPI EMUPATCH(D3DDevice_Swap)
(
    dword_t Flags
);

dword_t EMUPATCH(D3DDevice_Swap_0)();

// ******************************************************************
// * patch: IDirect3DResource8_Register
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DResource_Register)
(
    X_D3DResource      *pThis,
    PVOID               pBase
);

// ******************************************************************
// * patch: IDirect3DResource8_Release
// ******************************************************************
xbox::ulong_t WINAPI EMUPATCH(D3DResource_Release)
(
    X_D3DResource      *pThis
);

#if 0 // patch disabled
// ******************************************************************
// * patch: IDirect3DResource8_GetType
// ******************************************************************
X_D3DRESOURCETYPE WINAPI EMUPATCH(D3DResource_GetType)
(
    X_D3DResource      *pThis
);
#endif

// ******************************************************************
// * patch: IDirect3DResource8_AddRef
// ******************************************************************
xbox::ulong_t WINAPI EMUPATCH(D3DResource_AddRef)
(
    X_D3DResource      *pThis
);

// ******************************************************************
// * patch: IDirect3DResource8_IsBusy
// ******************************************************************
BOOL WINAPI EMUPATCH(D3DResource_IsBusy)
(
    X_D3DResource      *pThis
);

// ******************************************************************
// * patch: Lock2DSurface
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(Lock2DSurface)
(
    X_D3DPixelContainer *pPixelContainer,
    D3DCUBEMAP_FACES     FaceType,
    UINT                 Level,
    D3DLOCKED_RECT      *pLockedRect,
    RECT                *pRect,
    dword_t                Flags
);

// ******************************************************************
// * patch: Lock3DSurface
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(Lock3DSurface)
(
    X_D3DPixelContainer *pPixelContainer,
    UINT				Level,
	D3DLOCKED_BOX		*pLockedVolume,
	D3DBOX				*pBox,
	dword_t				Flags
);

#if 0 // patch disabled
// ******************************************************************
// * patch: Get2DSurfaceDesc
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(Get2DSurfaceDesc)
(
    X_D3DPixelContainer *pPixelContainer,
    dword_t                dwLevel,
    X_D3DSURFACE_DESC   *pDesc
);

// ******************************************************************
// * patch: IDirect3DSurface8_GetDesc
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DSurface_GetDesc)
(
    X_D3DResource      *pThis,
    X_D3DSURFACE_DESC  *pDesc
);

// ******************************************************************
// * patch: IDirect3DSurface8_LockRect
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DSurface_LockRect)
(
    X_D3DResource  *pThis,
    D3DLOCKED_RECT *pLockedRect,
    CONST RECT     *pRect,
    dword_t           Flags
);

// ******************************************************************
// * patch: IDirect3DBaseTexture8_GetLevelCount
// ******************************************************************
xbox::dword_t WINAPI EMUPATCH(D3DBaseTexture_GetLevelCount)
(
    X_D3DBaseTexture   *pThis
);

// ******************************************************************
// * patch: IDirect3DTexture8_GetSurfaceLevel2
// ******************************************************************
X_D3DSurface * WINAPI EMUPATCH(D3DTexture_GetSurfaceLevel2)
(
    X_D3DTexture   *pThis,
    UINT            Level
);

// ******************************************************************
// * patch: IDirect3DTexture8_LockRect
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DTexture_LockRect)
(
    X_D3DTexture   *pThis,
    UINT            Level,
    D3DLOCKED_RECT *pLockedRect,
    CONST RECT     *pRect,
    dword_t           Flags
);

// ******************************************************************
// * patch: IDirect3DTexture8_GetSurfaceLevel
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3DTexture_GetSurfaceLevel)
(
    X_D3DTexture   *pThis,
    UINT            Level,
    X_D3DSurface  **ppSurfaceLevel
);

// ******************************************************************
// * patch: IDirect3DVolumeTexture8_LockBox
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DVolumeTexture_LockBox)
(
    X_D3DVolumeTexture *pThis,
    UINT                Level,
    D3DLOCKED_BOX      *pLockedVolume,
    CONST D3DBOX       *pBox,
    dword_t               Flags
);

// ******************************************************************
// * patch: IDirect3DCubeTexture8_LockRect
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DCubeTexture_LockRect)
(
    X_D3DCubeTexture   *pThis,
    D3DCUBEMAP_FACES    FaceType,
    UINT                Level,
    D3DLOCKED_RECT     *pLockedBox,
    CONST RECT         *pRect,
    dword_t               Flags
);

// ******************************************************************
// * patch: D3DDevice_CreateVertexBuffer
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3DDevice_CreateVertexBuffer)
(
    UINT                Length,
    dword_t               Usage,
    dword_t               FVF,
    D3DPOOL             Pool,
    X_D3DVertexBuffer **ppVertexBuffer
);
#endif

#if 0 // patch disabled
// ******************************************************************
// * patch: D3DDevice_CreateVertexBuffer2
// ******************************************************************
X_D3DVertexBuffer* WINAPI EMUPATCH(D3DDevice_CreateVertexBuffer2)
(
    UINT Length
);
#endif

// ******************************************************************
// * patch: D3DDevice_EnableOverlay
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_EnableOverlay)
(
    BOOL Enable
);

// ******************************************************************
// * patch: D3DDevice_UpdateOverlay
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_UpdateOverlay)
(
    X_D3DSurface *pSurface,
    CONST RECT   *SrcRect,
    CONST RECT   *DstRect,
    BOOL          EnableColorKey,
    D3DCOLOR      ColorKey
);

// ******************************************************************
// * patch: D3DDevice_GetOverlayUpdateStatus
// ******************************************************************
BOOL WINAPI EMUPATCH(D3DDevice_GetOverlayUpdateStatus)();

// ******************************************************************
// * patch: D3DDevice_BlockUntilVerticalBlank
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_BlockUntilVerticalBlank)();

// ******************************************************************
// * patch: D3DDevice_SetVerticalBlankCallback
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetVerticalBlankCallback)
(
    X_D3DVBLANKCALLBACK pCallback
);

// ******************************************************************
// * patch: D3DDevice_SetTextureState_TexCoordIndex
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetTextureState_TexCoordIndex)
(
    dword_t Stage,
    dword_t Value
);

xbox::void_t __stdcall EMUPATCH(D3DDevice_SetTextureState_TexCoordIndex_0)();
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetTextureState_TexCoordIndex_4)
(
    dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_SetRenderState_TwoSidedLighting
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetRenderState_TwoSidedLighting)
(
    dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_SetRenderState_BackFillMode
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetRenderState_BackFillMode)
(
    dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_SetTextureState_BorderColor
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetTextureState_BorderColor)
(
    dword_t Stage,
    dword_t Value
);

xbox::void_t EMUPATCH(D3DDevice_SetTextureState_BorderColor_0)();
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetTextureState_BorderColor_4)
(
    dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_SetTextureState_ColorKeyColor
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetTextureState_ColorKeyColor)
(
    dword_t Stage,
    dword_t Value
);

xbox::void_t EMUPATCH(D3DDevice_SetTextureState_ColorKeyColor_0)();
xbox::void_t __stdcall EMUPATCH(D3DDevice_SetTextureState_ColorKeyColor_4)
(
    dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_SetTextureState_BumpEnv
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetTextureState_BumpEnv)
(
    dword_t                      Stage,
    X_D3DTEXTURESTAGESTATETYPE Type,
    dword_t                      Value
);

xbox::void_t WINAPI EMUPATCH(D3DDevice_SetTextureState_BumpEnv_8)
(
    X_D3DTEXTURESTAGESTATETYPE Type,
    dword_t                      Value
);

// ******************************************************************
// * patch: D3DDevice_SetRenderState_FrontFace
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetRenderState_FrontFace)
(
    dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_SetRenderState_LogicOp
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetRenderState_LogicOp)
(
    dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_SetRenderState_NormalizeNormals
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetRenderState_NormalizeNormals)
(
    dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_SetRenderState_TextureFactor
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetRenderState_TextureFactor)
(
    dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_SetRenderState_ZBias
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetRenderState_ZBias)
(
    dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_SetRenderState_EdgeAntiAlias
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetRenderState_EdgeAntiAlias)
(
    dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_SetRenderState_FillMode
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetRenderState_FillMode)
(
    dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_SetRenderState_FogColor
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetRenderState_FogColor)
(
    dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_SetRenderState_Dxt1NoiseEnable
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetRenderState_Dxt1NoiseEnable)
(
    dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_SetRenderState_Simple
// ******************************************************************
xbox::void_t __fastcall EMUPATCH(D3DDevice_SetRenderState_Simple)
(
    dword_t Method,
    dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_SetRenderState_VertexBlend
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetRenderState_VertexBlend)
(
    dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_SetRenderState_PSTextureModes
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetRenderState_PSTextureModes)
(
    dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_SetRenderState_CullMode
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetRenderState_CullMode)
(
    dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_SetRenderState_LineWidth
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetRenderState_LineWidth)
(
    dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_SetRenderState_StencilFail
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetRenderState_StencilFail)
(
    dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_SetRenderState_OcclusionCullEnable
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetRenderState_OcclusionCullEnable)
(
    dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_SetRenderState_StencilCullEnable
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetRenderState_StencilCullEnable)
(
    dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_SetRenderState_RopZCmpAlwaysRead
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetRenderState_RopZCmpAlwaysRead)
(
    dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_SetRenderState_RopZRead
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetRenderState_RopZRead)
(
    dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_SetRenderState_DoNotCullUncompressed
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetRenderState_DoNotCullUncompressed)
(
    dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_SetRenderState_ZEnable
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetRenderState_ZEnable)
(
    dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_SetRenderState_StencilEnable
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetRenderState_StencilEnable)
(
    dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_SetRenderState_MultiSampleMask
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetRenderState_MultiSampleMask)
(
    dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_SetRenderState_MultiSampleMode
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetRenderState_MultiSampleMode)
(
    dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_SetRenderState_MultiSampleRenderTargetMode
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetRenderState_MultiSampleRenderTargetMode)
(
    dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_SetRenderState_MultiSampleAntiAlias
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetRenderState_MultiSampleAntiAlias)
(
    dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_SetRenderState_ShadowFunc
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetRenderState_ShadowFunc)
(
    dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_SetTransform
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetTransform)
(
    D3DTRANSFORMSTATETYPE State,
    CONST D3DMATRIX      *pMatrix
);

xbox::void_t __stdcall EMUPATCH(D3DDevice_SetTransform_0)();

// ******************************************************************
// * patch: D3DDevice_MultiplyTransform
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_MultiplyTransform)
(
	D3DTRANSFORMSTATETYPE State,
	CONST D3DMATRIX      *pMatrix
);

// ******************************************************************
// * patch: D3DDevice_GetTransform
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_GetTransform)
(
    D3DTRANSFORMSTATETYPE State,
    D3DMATRIX            *pMatrix
);

// ******************************************************************
// * patch: IDirect3DVertexBuffer8_Lock
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DVertexBuffer_Lock)
(
    X_D3DVertexBuffer   *pVertexBuffer,
    UINT                OffsetToLock,
    UINT                SizeToLock,
    byte_t              **ppbData,
    dword_t               Flags
);

// ******************************************************************
// * patch: IDirect3DVertexBuffer8_Lock2
// ******************************************************************
byte_t* WINAPI EMUPATCH(D3DVertexBuffer_Lock2)
(
    X_D3DVertexBuffer  *pVertexBuffer,
    dword_t               Flags
);

// ******************************************************************
// * patch: D3DDevice_GetStreamSource2
// ******************************************************************
xbox::X_D3DVertexBuffer* WINAPI EMUPATCH(D3DDevice_GetStreamSource2)
(
    UINT  StreamNumber,
    UINT *pStride
);

// ******************************************************************
// * patch: D3DDevice_SetStreamSource
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetStreamSource)
(
    UINT                StreamNumber,
    X_D3DVertexBuffer  *pStreamData,
    UINT                Stride
);

xbox::void_t WINAPI EMUPATCH(D3DDevice_SetStreamSource_4)
(
    UINT                Stride
);

xbox::void_t WINAPI EMUPATCH(D3DDevice_SetStreamSource_8)
(
    X_D3DVertexBuffer  *pStreamData,
    UINT                Stride
);

// ******************************************************************
// * patch: D3DDevice_SetVertexShader
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetVertexShader)
(
    dword_t            Handle
);

// ******************************************************************
// * patch: D3DDevice_DrawVertices
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_DrawVertices)
(
    X_D3DPRIMITIVETYPE  PrimitiveType,
    UINT                StartVertex,
    UINT                VertexCount
);

// ******************************************************************
// * patch: D3DDevice_DrawVertices_4
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_DrawVertices_4)
(
    X_D3DPRIMITIVETYPE  PrimitiveType
);

// ******************************************************************
// * patch: D3DDevice_DrawVerticesUP
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_DrawVerticesUP)
(
    X_D3DPRIMITIVETYPE  PrimitiveType,
    UINT                VertexCount,
    CONST PVOID         pVertexStreamZeroData,
    UINT                VertexStreamZeroStride
);

// ******************************************************************
// * patch: D3DDevice_DrawIndexedVertices
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_DrawIndexedVertices)
(
    X_D3DPRIMITIVETYPE  PrimitiveType,
    UINT                VertexCount,
    CONST PWORD         pIndexData
);

// ******************************************************************
// * patch: D3DDevice_DrawIndexedVerticesUP
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_DrawIndexedVerticesUP)
(
    X_D3DPRIMITIVETYPE  PrimitiveType,
    UINT                VertexCount,
    CONST PVOID         pIndexData,
    CONST PVOID         pVertexStreamZeroData,
    UINT                VertexStreamZeroStride
);

// ******************************************************************
// * patch: D3DDevice_GetLight
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_GetLight)
(
    dword_t            Index,
    X_D3DLIGHT8       *pLight
);

// ******************************************************************
// * patch: D3DDevice_SetLight
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3DDevice_SetLight)
(
    dword_t            Index,
    CONST X_D3DLIGHT8 *pLight
);

// ******************************************************************
// * patch: D3DDevice_SetMaterial
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetMaterial)
(
    CONST X_D3DMATERIAL8 *pMaterial
);

// ******************************************************************
// * patch: D3DDevice_LightEnable
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3DDevice_LightEnable)
(
    dword_t            Index,
    BOOL             bEnable
);

// ******************************************************************
// * patch: D3DDevice_Release
// ******************************************************************
xbox::ulong_t WINAPI EMUPATCH(D3DDevice_Release)();

// ******************************************************************
// * patch: D3DDevice_CreatePalette
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3DDevice_CreatePalette)
(
    X_D3DPALETTESIZE    Size,
    X_D3DPalette      **ppPalette
);

// ******************************************************************
// * patch: D3DDevice_CreatePalette2
// ******************************************************************
X_D3DPalette * WINAPI EMUPATCH(D3DDevice_CreatePalette2)
(
    X_D3DPALETTESIZE    Size
);

// ******************************************************************
// * patch: D3DDevice_SetRenderTarget
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetRenderTarget)
(
    X_D3DSurface    *pRenderTarget,
    X_D3DSurface    *pNewZStencil
);

// ******************************************************************
// * patch: D3DDevice_SetPalette
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetPalette)
(
    dword_t         Stage,
    X_D3DPalette *pPalette
);

xbox::void_t __stdcall EMUPATCH(D3DDevice_SetPalette_4)();

// ******************************************************************
// * patch: D3DDevice_SetFlickerFilter
// ******************************************************************
void WINAPI EMUPATCH(D3DDevice_SetFlickerFilter)
(
    dword_t         Filter
);

xbox::void_t __stdcall EMUPATCH(D3DDevice_SetFlickerFilter_0)();

// ******************************************************************
// * patch: D3DDevice_SetSoftDisplayFilter
// ******************************************************************
void WINAPI EMUPATCH(D3DDevice_SetSoftDisplayFilter)
(
    BOOL Enable
);

// ******************************************************************
// * patch: IDirect3DPalette8_Lock
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DPalette_Lock)
(
    X_D3DPalette   *pThis,
    D3DCOLOR      **ppColors,
    dword_t           Flags
);

// ******************************************************************
// * patch: IDirect3DPalette8_Lock2
// ******************************************************************
D3DCOLOR * WINAPI EMUPATCH(D3DPalette_Lock2)
(
    X_D3DPalette   *pThis,
    dword_t           Flags
);

// ******************************************************************
// * patch: D3DDevice_GetVertexShaderSize
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_GetVertexShaderSize)
(
    dword_t Handle,
    UINT* pSize
);

// ******************************************************************
// * patch: D3DDevice_DeleteVertexShader
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_DeleteVertexShader)
(
    dword_t Handle
);

xbox::void_t __stdcall EMUPATCH(D3DDevice_DeleteVertexShader_0)();

// ******************************************************************
// * patch: D3DDevice_SelectVertexShaderDirect
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SelectVertexShaderDirect)
(
    X_VERTEXATTRIBUTEFORMAT *pVAF,
    dword_t                    Address
);

// ******************************************************************
// * patch: D3DDevice_GetShaderConstantMode
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_GetShaderConstantMode)
(
    dword_t *pMode
);

// ******************************************************************
// * patch: D3DDevice_GetVertexShader
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_GetVertexShader)
(
    dword_t *pHandle
);

// ******************************************************************
// * patch: D3DDevice_GetVertexShaderConstant
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_GetVertexShaderConstant)
(
    INT   Register,
    void  *pConstantData,
    dword_t ConstantCount
);

// ******************************************************************
// * patch: D3DDevice_SetVertexShaderInputDirect
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetVertexShaderInputDirect)
(
    X_VERTEXATTRIBUTEFORMAT *pVAF,
    UINT                     StreamCount,
    X_STREAMINPUT           *pStreamInputs
);

// ******************************************************************
// * patch: D3DDevice_GetVertexShaderInput
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3DDevice_GetVertexShaderInput)
(
    dword_t              *pHandle,
    UINT               *pStreamCount,
    X_STREAMINPUT      *pStreamInputs
);

// ******************************************************************
// * patch: D3DDevice_GetVertexShaderInput
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetVertexShaderInput)
(
    dword_t              Handle,
    UINT               StreamCount,
    X_STREAMINPUT     *pStreamInputs
);

// ******************************************************************
// * patch: D3DDevice_RunVertexStateShader
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_RunVertexStateShader)
(
    dword_t        Address,
    CONST FLOAT *pData
);

// ******************************************************************
// * patch: D3DDevice_LoadVertexShaderProgram
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_LoadVertexShaderProgram)
(
    CONST dword_t *pFunction,
    dword_t        Address
);

// ******************************************************************
// * patch: D3DDevice_GetVertexShaderType
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_GetVertexShaderType)
(
    dword_t  Handle,
    dword_t *pType
);

// ******************************************************************
// * patch: D3DDevice_GetVertexShaderDeclaration
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3DDevice_GetVertexShaderDeclaration)
(
    dword_t  Handle,
    PVOID  pData,
    dword_t *pSizeOfData
);

// ******************************************************************
// * patch: D3DDevice_GetVertexShaderFunction
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3DDevice_GetVertexShaderFunction)
(
    dword_t  Handle,
    PVOID *pData,
    dword_t *pSizeOfData
);

// ******************************************************************
// * patch: D3DDevice_SetDepthClipPlanes
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3DDevice_SetDepthClipPlanes)
(
    FLOAT Near,
    FLOAT Far,
    dword_t Flags
);

#if 0 // DISABLED (Just calls MmAllocateContiguousMemory)
// ******************************************************************
// * patch: D3D_AllocContiguousMemory
// ******************************************************************
PVOID WINAPI EMUPATCH(D3D_AllocContiguousMemory)
(
    SIZE_T dwSize,
    dword_t dwAllocAttributes
);
#endif

#if 0 // DISABLED (Just calls Get2DSurfaceDesc)
// ******************************************************************
// * patch: D3DTexture_GetLevelDesc
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3DTexture_GetLevelDesc)
(
    UINT Level,
    X_D3DSURFACE_DESC* pDesc
);
#endif

#if 0 // patch disabled
// ******************************************************************
// * patch: Direct3D_CheckDeviceMultiSampleType
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(Direct3D_CheckDeviceMultiSampleType)
(
    UINT                 Adapter,
    D3DDEVTYPE           DeviceType,
    X_D3DFORMAT          SurfaceFormat,
    BOOL                 Windowed,
    D3DMULTISAMPLE_TYPE  MultiSampleType
);
#endif

#if 0 // patch disabled
// ******************************************************************
// * patch: D3D_GetDeviceCaps
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3D_GetDeviceCaps)
(
    UINT        Adapter,
    D3DDEVTYPE  DeviceType,
    X_D3DCAPS  *pCaps
);
#endif

#if 0 // patch disabled
// ******************************************************************
// * patch: D3D_SetPushBufferSize
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3D_SetPushBufferSize)
(
    dword_t PushBufferSize,
    dword_t KickOffSize
);
#endif

// ******************************************************************
// * patch: D3DDevice_InsertFence
// ******************************************************************
xbox::dword_t WINAPI EMUPATCH(D3DDevice_InsertFence)();

// ******************************************************************
// * patch: D3DDevice_IsFencePending
// ******************************************************************
BOOL WINAPI EMUPATCH(D3DDevice_IsFencePending)
(
    dword_t Fence
);

// ******************************************************************
// * patch: D3DDevice_BlockOnFence
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_BlockOnFence)
(
    dword_t Fence
);

// ******************************************************************
// * patch: D3DResource_BlockUntilNotBusy
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DResource_BlockUntilNotBusy)
(
    X_D3DResource *pThis
);

#if 0 // patch DISABLED
// ******************************************************************
// * patch: D3DVertexBuffer_GetDesc
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DVertexBuffer_GetDesc)
(
    X_D3DVertexBuffer    *pThis,
    X_D3DVERTEXBUFFER_DESC *pDesc
);
#endif

// ******************************************************************
// * patch: D3DDevice_SetScissors
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetScissors)
(
    dword_t          Count,
    BOOL           Exclusive,
    CONST D3DRECT  *pRects
);

// ******************************************************************
// * patch: D3DDevice_SetScreenSpaceOffset
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetScreenSpaceOffset)
(
    FLOAT x,
    FLOAT y
);

// ******************************************************************
// * patch: D3DDevice_SetPixelShaderProgram
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetPixelShaderProgram)
(
	X_D3DPIXELSHADERDEF *pPSDef
);

// ******************************************************************
// * patch: D3DDevice_CreateStateBlock
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3DDevice_CreateStateBlock)
(
	D3DSTATEBLOCKTYPE Type,
	dword_t			  *pToken
);

// ******************************************************************
// * patch: D3DDevice_InsertCallback
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_InsertCallback)
(
	X_D3DCALLBACKTYPE	Type,
	X_D3DCALLBACK		pCallback,
	dword_t				Context
);

// ******************************************************************
// * patch: D3DDevice_DrawRectPatch
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3DDevice_DrawRectPatch)
(
	UINT					Handle,
	CONST FLOAT				*pNumSegs,
	CONST D3DRECTPATCH_INFO *pRectPatchInfo
);

// ******************************************************************
// * patch: D3DDevice_DrawTriPatch
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3DDevice_DrawTriPatch)
(
	UINT					Handle,
	CONST FLOAT				*pNumSegs,
	CONST D3DTRIPATCH_INFO* pTriPatchInfo
);

// ******************************************************************
// * patch: D3DDevice_GetProjectionViewportMatrix
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_GetProjectionViewportMatrix)
(
	D3DXMATRIX *pProjectionViewport
);

// ******************************************************************
// * patch: D3DDevice_KickOff (D3D::CDevice::KickOff)
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_KickOff)();

// ******************************************************************
// * patch: D3DDevice_KickPushBuffer
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_KickPushBuffer)();

// ******************************************************************
// * patch: D3DDevice_GetTexture2
// ******************************************************************
X_D3DBaseTexture* WINAPI EMUPATCH(D3DDevice_GetTexture2)(dword_t Stage);

// ******************************************************************
// * patch: D3DDevice_GetTexture
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_GetTexture)
(
	dword_t           Stage,
	X_D3DBaseTexture  **pTexture
);

// ******************************************************************
// * patch: D3DDevice_SetStateVB (D3D::CDevice::SetStateVB)
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetStateVB)( xbox::ulong_t Unknown1 );

// ******************************************************************
// * patch: D3DDevice_SetStateUP (D3D::CDevice::SetStateUP)
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetStateUP)();

// ******************************************************************
// * patch: D3DDevice_SetStipple
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetStipple)( dword_t* pPattern );

// ******************************************************************
// * patch: D3DDevice_SetSwapCallback
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetSwapCallback)
(
	X_D3DSWAPCALLBACK		pCallback
);

// ******************************************************************
// * patch: D3DDevice_PersistDisplay
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3DDevice_PersistDisplay)();

// ******************************************************************
// * patch: D3DDevice_GetPersistedSurface
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_GetPersistedSurface)(X_D3DSurface **ppSurface);
X_D3DSurface* WINAPI EMUPATCH(D3DDevice_GetPersistedSurface2)();

// ******************************************************************
// * patch: D3D_CMiniport_GetDisplayCapabilities
// ******************************************************************
xbox::dword_t WINAPI EMUPATCH(D3D_CMiniport_GetDisplayCapabilities)();

// ******************************************************************
// * patch: D3DDevice_PrimeVertexCache
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_PrimeVertexCache)
(
	UINT VertexCount,
	word_t *pIndexData
);

// ******************************************************************
// * patch: D3DDevice_SetRenderState_SampleAlpha
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3DDevice_SetRenderState_SampleAlpha)
(
	dword_t dwSampleAlpha
);

// ******************************************************************
// * patch: D3DDevice_SetRenderState_Deferred
// ******************************************************************
xbox::void_t __fastcall EMUPATCH(D3DDevice_SetRenderState_Deferred)
(
	dword_t State,
	dword_t Value
);

// ******************************************************************
// * patch: D3DDevice_DeleteStateBlock
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_DeleteStateBlock)
(
	dword_t Token
);

// ******************************************************************
// * patch: D3DDevice_SetModelView
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetModelView)
(
	CONST D3DMATRIX *pModelView, 
	CONST D3DMATRIX *pInverseModelView, 
	CONST D3DMATRIX *pComposite
);

// ******************************************************************
// * patch: D3DDevice_FlushVertexCache
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_FlushVertexCache)();

// ******************************************************************
// * patch: D3DDevice_BeginPushBuffer
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_BeginPushBuffer)
(
	X_D3DPushBuffer *pPushBuffer
);

// ******************************************************************
// * patch: D3DDevice_EndPushBuffer
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3DDevice_EndPushBuffer)();

// ******************************************************************
// * patch: XMETAL_StartPush
// ******************************************************************
PDWORD WINAPI EMUPATCH(XMETAL_StartPush)(void* Unknown);

// ******************************************************************
// * patch: D3DDevice_GetModelView
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3DDevice_GetModelView)(D3DXMATRIX* pModelView);

// ******************************************************************
// * patch: D3DDevice_SetBackMaterial
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetBackMaterial)
(
	X_D3DMATERIAL8* pMaterial
);

#if 0 // patch disabled
// ******************************************************************
// * patch: D3D_GetAdapterIdentifier
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3D_GetAdapterIdentifier)
(
	UINT					Adapter,
	dword_t					Flags,
	X_D3DADAPTER_IDENTIFIER *pIdentifier
);
#endif

// ******************************************************************
// * patch: D3D::MakeRequestedSpace
// ******************************************************************
PDWORD WINAPI EMUPATCH(D3D_MakeRequestedSpace)
(
	dword_t MinimumSpace,
	dword_t RequestedSpace
);

// ******************************************************************
// * patch: D3DDevice_MakeSpace
// ******************************************************************
void WINAPI EMUPATCH(D3DDevice_MakeSpace)();

// ******************************************************************
// * patch: D3D_SetCommonDebugRegisters
// ******************************************************************
void WINAPI EMUPATCH(D3D_SetCommonDebugRegisters)();

// ******************************************************************
// * patch: D3D_BlockOnTime
// ******************************************************************
void WINAPI EMUPATCH(D3D_BlockOnTime)( dword_t Unknown1, int Unknown2 );

// ******************************************************************
// * patch: D3D_BlockOnResource
// ******************************************************************
void WINAPI EMUPATCH(D3D_BlockOnResource)( X_D3DResource* pResource );

// ******************************************************************
// * patch: D3D_DestroyResource
// ******************************************************************
void WINAPI EMUPATCH(D3D_DestroyResource)( X_D3DResource* pResource );

// ******************************************************************
// * patch: D3D_DestroyResource__LTCG
// ******************************************************************
void WINAPI EMUPATCH(D3D_DestroyResource__LTCG)();


// ******************************************************************
// * patch: D3DDevice_GetPushBufferOffset
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_GetPushBufferOffset)
(
	dword_t *pOffset
);

// ******************************************************************
// * patch: IDirect3DCubeTexture8_GetCubeMapSurface
// ******************************************************************
xbox::hresult_t WINAPI EMUPATCH(D3DCubeTexture_GetCubeMapSurface)
(
	X_D3DCubeTexture*	pThis,
	D3DCUBEMAP_FACES	FaceType,
	UINT				Level,
	X_D3DSurface**		ppCubeMapSurface
);

// ******************************************************************
// * patch: IDirect3DCubeTexture8_GetCubeMapSurface2
// ******************************************************************
X_D3DSurface* WINAPI EMUPATCH(D3DCubeTexture_GetCubeMapSurface2)
(
	X_D3DCubeTexture*	pThis,
	D3DCUBEMAP_FACES	FaceType,
	UINT				Level
);

// ******************************************************************
// * patch: D3DDevice_GetPixelShader
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_GetPixelShader)
(
	dword_t  Name,
	dword_t* pHandle
);

// ******************************************************************
// * patch: D3DDevice_SetRenderTargetFast
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_SetRenderTargetFast)
(
    X_D3DSurface	*pRenderTarget,
    X_D3DSurface	*pNewZStencil,
    dword_t			Flags
);

// ******************************************************************
// * patch: D3DDevice_GetScissors
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_GetScissors)
(
	dword_t	*pCount, 
	BOOL	*pExclusive, 
	D3DRECT *pRects
);
// ******************************************************************
// * patch: D3DDevice_GetBackMaterial
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_GetBackMaterial)
(
	X_D3DMATERIAL8* pMaterial
);

// ******************************************************************
// * patch: D3D::LazySetPointParams
// ******************************************************************
void WINAPI EMUPATCH(D3D_LazySetPointParams)( void* Device );

// ******************************************************************
// * patch: D3DDevice_GetMaterial
// ******************************************************************
xbox::void_t WINAPI EMUPATCH(D3DDevice_GetMaterial)
(
	X_D3DMATERIAL8* pMaterial
);

} // end of namespace xbox

#endif
