// virtual NV2A register for HLE API handler
#   define NV097_HLE_API                                      0x00000080

// enum for xbox D3DDevice APIs
typedef enum _X_D3DAPI_ENUM {

    X_CDevice_SetStateUP,
    X_CDevice_SetStateUP_4,
    X_CDevice_SetStateUP_0__LTCG_esi1,
    X_CDevice_SetStateVB,
    X_CDevice_SetStateVB_8,
    X_D3DDevice_ApplyStateBlock,
    X_D3DDevice_Begin,
    X_D3DDevice_BeginPush,
    X_D3DDevice_BeginPush_4,
    X_D3DDevice_BeginPush_8,
    X_D3DDevice_BeginPushBuffer,
    X_D3DDevice_BeginScene,
    X_D3DDevice_BeginState,
    X_D3DDevice_BeginStateBig,
    X_D3DDevice_BeginStateBlock,
    X_D3DDevice_BeginVisibilityTest,
    X_D3DDevice_BlockOnFence,
    X_D3DDevice_BlockUntilIdle,
    X_D3DDevice_BlockUntilVerticalBlank,
    X_D3DDevice_CaptureStateBlock,
    X_D3DDevice_Clear,
    X_D3DDevice_CopyRects,
    X_D3DDevice_CreateCubeTexture,
    X_D3DDevice_CreateDepthStencilSurface,
    X_D3DDevice_CreateFixup,
    X_D3DDevice_CreateImageSurface,
    X_D3DDevice_CreateIndexBuffer,
    X_D3DDevice_CreatePalette,
    X_D3DDevice_CreatePixelShader,
    X_D3DDevice_CreatePushBuffer,
    X_D3DDevice_CreateRenderTarget,
    X_D3DDevice_CreateStateBlock,
    X_D3DDevice_CreateTexture,
    X_D3DDevice_CreateTexture2,
    X_D3DDevice_CreateVertexBuffer,
    X_D3DDevice_CreateVertexBuffer2,
    X_D3DDevice_CreateVertexShader,
    X_D3DDevice_CreateVolumeTexture,
    X_D3DDevice_DeletePatch,
    X_D3DDevice_DeletePixelShader,
    X_D3DDevice_DeleteStateBlock,
    X_D3DDevice_DeleteVertexShader, X_D3DDevice_DeleteVertexShader_0,
    X_D3DDevice_DrawIndexedPrimitive,
    X_D3DDevice_DrawIndexedPrimitiveUP,
    X_D3DDevice_DrawIndexedVertices,
    X_D3DDevice_DrawIndexedVerticesUP,
    X_D3DDevice_DrawPrimitive,
    X_D3DDevice_DrawPrimitiveUP,
    X_D3DDevice_DrawRectPatch,
    X_D3DDevice_DrawTriPatch,
    X_D3DDevice_DrawVertices, X_D3DDevice_DrawVertices_4__LTCG_ecx2_eax3, X_D3DDevice_DrawVertices_8__LTCG_eax3,
    X_D3DDevice_DrawVerticesUP, X_D3DDevice_DrawVerticesUP_12__LTCG_ebx3,
    X_D3DDevice_EnableOverlay,
    X_D3DDevice_End,
    X_D3DDevice_EndPush,
    X_D3DDevice_EndPushBuffer,
    X_D3DDevice_EndScene,
    X_D3DDevice_EndState,
    X_D3DDevice_EndStateBlock,
    X_D3DDevice_EndVisibilityTest, X_D3DDevice_EndVisibilityTest_0,
    X_D3DDevice_FlushVertexCache,
    X_D3DDevice_GetBackBuffer, X_D3DDevice_GetBackBuffer2, X_D3DDevice_GetBackBuffer2_0__LTCG_eax1,
    X_D3DDevice_GetBackBufferScale,
    X_D3DDevice_GetBackMaterial,
    X_D3DDevice_GetCopyRectsState,
    X_D3DDevice_GetCreationParameters,
    X_D3DDevice_GetDebugMarker,
    X_D3DDevice_GetDepthClipPlanes,
    X_D3DDevice_GetDepthStencilSurface, X_D3DDevice_GetDepthStencilSurface2,
    X_D3DDevice_GetDeviceCaps,
    X_D3DDevice_GetDirect3D,
    X_D3DDevice_GetDisplayFieldStatus,
    X_D3DDevice_GetDisplayMode,
    X_D3DDevice_GetGammaRamp,
    X_D3DDevice_GetIndices,
    X_D3DDevice_GetLight,
    X_D3DDevice_GetLightEnable,
    X_D3DDevice_GetMaterial,
    X_D3DDevice_GetModelView,
    X_D3DDevice_GetOverlayUpdateStatus,
    X_D3DDevice_GetOverscanColor,
    X_D3DDevice_GetPalette,
    X_D3DDevice_GetPersistedSurface,
    X_D3DDevice_GetPixelShader,
    X_D3DDevice_GetPixelShaderConstant,
    X_D3DDevice_GetPixelShaderFunction,
    X_D3DDevice_GetProjectionViewportMatrix,
    X_D3DDevice_GetPushBufferOffset,
    X_D3DDevice_GetPushDistance,
    X_D3DDevice_GetRasterStatus,
    X_D3DDevice_GetRenderState,
    X_D3DDevice_GetRenderTarget, X_D3DDevice_GetRenderTarget2,
    X_D3DDevice_GetScissors,
    X_D3DDevice_GetScreenSpaceOffset,
    X_D3DDevice_GetShaderConstantMode,
    X_D3DDevice_GetStipple,
    X_D3DDevice_GetStreamSource,
    X_D3DDevice_GetTexture,
    X_D3DDevice_GetTextureStageState,
    X_D3DDevice_GetTile,
    X_D3DDevice_GetTileCompressionTags,
    X_D3DDevice_GetTransform,
    X_D3DDevice_GetVertexBlendModelView,
    X_D3DDevice_GetVertexShader,
    X_D3DDevice_GetVertexShaderConstant,
    X_D3DDevice_GetVertexShaderDeclaration,
    X_D3DDevice_GetVertexShaderFunction,
    X_D3DDevice_GetVertexShaderInput,
    X_D3DDevice_GetVertexShaderSize,
    X_D3DDevice_GetVertexShaderType,
    X_D3DDevice_GetViewport,
    X_D3DDevice_GetViewportOffsetAndScale,
    X_D3DDevice_GetVisibilityTestResult,
    X_D3DDevice_InsertCallback,
    X_D3DDevice_InsertFence,
    X_D3DDevice_IsBusy,
    X_D3DDevice_IsFencePending,
    X_D3DDevice_KickPushBuffer,
    X_D3DDevice_LightEnable,
    X_D3DDevice_LoadVertexShader, X_D3DDevice_LoadVertexShader_0__LTCG_eax_Address_ecx_Handle, X_D3DDevice_LoadVertexShader_0__LTCG_eax_Address_edx_Handle, X_D3DDevice_LoadVertexShader_4,
    X_D3DDevice_LoadVertexShaderProgram,
    X_D3DDevice_MultiplyTransform,
    X_D3DDevice_Nop,
    X_D3DDevice_PersistDisplay,
    X_D3DDevice_Present,
    X_D3DDevice_PrimeVertexCache,
    X_D3DDevice_Reset, X_D3DDevice_Reset_0__LTCG_edi1, X_D3DDevice_Reset_0__LTCG_ebx1,
    X_D3DDevice_RunPushBuffer,
    X_D3DDevice_RunVertexStateShader,
    X_D3DDevice_SelectVertexShader, X_D3DDevice_SelectVertexShader_0__LTCG_eax1_ebx2, X_D3DDevice_SelectVertexShader_4__LTCG_eax1,
    X_D3DDevice_SelectVertexShaderDirect,
    X_D3DDevice_SetBackBufferScale,
    X_D3DDevice_SetBackMaterial,
    X_D3DDevice_SetCopyRectsState,
    X_D3DDevice_SetDebugMarker,
    X_D3DDevice_SetDepthClipPlanes,
    X_D3DDevice_SetFlickerFilter, X_D3DDevice_SetFlickerFilter_0,
    X_D3DDevice_SetGammaRamp,
    X_D3DDevice_SetIndices, X_D3DDevice_SetIndices_4,
    X_D3DDevice_SetLight,
    X_D3DDevice_SetMaterial,
    X_D3DDevice_SetModelView,
    X_D3DDevice_SetOverscanColor,
    X_D3DDevice_SetPalette, X_D3DDevice_SetPalette_4,
    X_D3DDevice_SetPixelShader, X_D3DDevice_SetPixelShader_0__LTCG_eax_handle,
    X_D3DDevice_SetPixelShaderConstant, X_D3DDevice_SetPixelShaderConstant_4,
    X_D3DDevice_SetPixelShaderProgram,
    X_D3DDevice_SetRenderState, X_D3DDevice_SetRenderState_Simple,
    X_D3DDevice_SetRenderStateNotInline,
    X_D3DDevice_SetRenderTarget, X_D3DDevice_SetRenderTarget_0,
    X_D3DDevice_SetRenderTargetFast,
    X_D3DDevice_SetScissors,
    X_D3DDevice_SetScreenSpaceOffset,
    X_D3DDevice_SetShaderConstantMode, X_D3DDevice_SetShaderConstantMode_0__LTCG_eax1,
    X_D3DDevice_SetSoftDisplayFilter,
    X_D3DDevice_SetStipple,
    X_D3DDevice_SetStreamSource, X_D3DDevice_SetStreamSource_0__LTCG_eax_StreamNumber_edi_pStreamData_ebx_Stride, X_D3DDevice_SetStreamSource_4, X_D3DDevice_SetStreamSource_8, X_D3DDevice_SetStreamSource_8__LTCG_edx_StreamNumber,
    X_D3DDevice_SetSwapCallback,
    X_D3DDevice_SetTexture, X_D3DDevice_SetTexture_4__LTCG_eax_pTexture, X_D3DDevice_SetTexture_4__LTCG_eax_Stage,
    X_D3DDevice_SetTextureStageState,
    X_D3DDevice_SetTextureStageStateNotInline,
    X_D3DDevice_SetTile,
    X_D3DDevice_SetTimerCallback,
    X_D3DDevice_SetTransform, X_D3DDevice_SetTransform_0__LTCG_eax1_edx2,
    X_D3DDevice_SetVertexBlendModelView,
    X_D3DDevice_SetVertexData2f,
    X_D3DDevice_SetVertexData2s,
    X_D3DDevice_SetVertexData4f, X_D3DDevice_SetVertexData4f_16,
    X_D3DDevice_SetVertexData4s,
    X_D3DDevice_SetVertexData4ub,
    X_D3DDevice_SetVertexDataColor,
    X_D3DDevice_SetVertexShader, X_D3DDevice_SetVertexShader_0,
    X_D3DDevice_SetVertexShaderConstant, X_D3DDevice_SetVertexShaderConstant_8, X_D3DDevice_SetVertexShaderConstant1, X_D3DDevice_SetVertexShaderConstant4,
    X_D3DDevice_SetVertexShaderConstantFast, X_D3DDevice_SetVertexShaderConstant1Fast, X_D3DDevice_SetVertexShaderConstantNotInline, X_D3DDevice_SetVertexShaderConstantNotInlineFast,
    X_D3DDevice_SetVertexShaderInput,
    X_D3DDevice_SetVertexShaderInputDirect,
    X_D3DDevice_SetVerticalBlankCallback,
    X_D3DDevice_SetViewport,
    X_D3DDevice_SetWaitCallback,
    X_D3DDevice_Swap, X_D3DDevice_Swap_0,
    X_D3DDevice_SwitchTexture,
    X_D3DDevice_UpdateOverlay,
    X_D3DResource_BlockUntilNotBusy,
    X_D3D_BlockOnTime, X_D3D_BlockOnTime_4,
    X_D3D_CommonSetRenderTarget,
    X_D3D_DestroyResource, X_D3D_DestroyResource__LTCG,
    X_D3D_LazySetPointParams,
    X_D3D_SetCommonDebugRegisters,
    X_Direct3D_CreateDevice, X_Direct3D_CreateDevice_16__LTCG_eax_BehaviorFlags_ebx_ppReturnedDeviceInterface, X_Direct3D_CreateDevice_16__LTCG_eax_BehaviorFlags_ecx_ppReturnedDeviceInterface, X_Direct3D_CreateDevice_4,
    X_Lock2DSurface,
    X_Lock3DSurface,
    X_D3DAPI_FORCE_DWORD = 0x7fffffff,

} X_D3DAPI_ENUM;

#define HLE_API_PUSHBFFER_COMMAND 0x403C0080
#define NV097_HLE_API 0x00000080


/*
//template for xbox D3D API patch routine

    API_name(args)
{
    // init pushbuffer related pointers
    DWORD * pPush_local = (DWORD *)*g_pXbox_pPush;         //pointer to current pushbuffer
    DWORD * pPush_limit = (DWORD *)*g_pXbox_pPushLimit;    //pointer to the end of current pushbuffer
    if ( (unsigned int)pPush_local+64 >= (unsigned int)pPush_limit )//check if we still have enough space
        pPush_local = (DWORD *)CxbxrImpl_MakeSpace(); //make new pushbuffer space and get the pointer to it.

    // process xbox D3D API enum and arguments and push them to pushbuffer for pgraph to handle later.
    pPush_local[0]=HLE_API_PUSHBFFER_COMMAND;
    pPush_local[1]=X_D3DAPI_ENUM::X_APINAME;//enum of this patched API
    pPush_local[2] = (DWORD) arg[0]; //total 14 DWORD space for arguments.
    pPush_local[3] = (DWORD) arg[1];
    pPush_local[4] = (DWORD) arg[2];
    pPush_local[5] = (DWORD) arg[3]; 
    pPush_local[6] = (DWORD) arg[4];
    pPush_local[7] = (DWORD) arg[5];
    pPush_local[8] =  *(DWORD*) &  arg[6];// use this to copy float arg to pushbuffer

    //set pushbuffer pointer to the new beginning
    // always reserve 1 command DWORD, 1 API enum, and 14 argmenet DWORDs.
    *(DWORD **)g_pXbox_pPush+=0x10; 

}

//template for xbox D3D API handler in pgraph

HLEApi=arg[0];
switch (HLEApi)
    
{
    case X_APIName:
        // convert arg[1]... to corresponded arg[0] of CabxrImpl_APIName() handler.
        call CxbxrImpl_APIName(arg[1]....);

    break;
}

*/
static inline DWORD FtoDW(FLOAT f) { return *((DWORD*)&f); }
static inline FLOAT DWtoF(DWORD f) { return *((FLOAT*)&f); }

// prototypes for xbox D3DDevice API HLE handlers in NV2A pgraph, implemented in Direct3D9.cpp, called in EmuNV2A_PGRAPH.cpp
// we create defines for each patched api in general format USEPGRAPH_ + apu post names without D3DDevice_. define it as 1 to enable the patch and prgaph handler, as 0 to keep original Cxbxr behavior. this is to speed up the test which api is not feasible for this POC.
void CxbxrImpl_Begin(xbox::X_D3DPRIMITIVETYPE PrimitiveType);
//void CxbxrImpl_Clear(xbox::dword_xt Count, CONST D3DRECT* pRects, xbox::dword_xt Flags, D3DCOLOR Color, float Z, xbox::dword_xt Stencil);
#define USEPGRAPH_Clear 0
//void CxbxrImpl_CopyRects(xbox::X_D3DSurface* pSourceSurface, CONST RECT* pSourceRectsArray, xbox::uint_xt cRects, xbox::X_D3DSurface* pDestinationSurface, CONST POINT* pDestPointsArray);
#define USEPGRAPH_CopyRects 0 /*CopyRects() is not permitted in pushbuffer recording.*/
void WINAPI CxbxrImpl_DrawIndexedVertices(xbox::X_D3DPRIMITIVETYPE  PrimitiveType, xbox::uint_xt VertexCount, CONST PWORD pIndexData);
void WINAPI CxbxrImpl_DrawIndexedVerticesUP(xbox::X_D3DPRIMITIVETYPE  PrimitiveType, xbox::uint_xt VertexCount, CONST PVOID pIndexData, CONST PVOID pVertexStreamZeroData, xbox::uint_xt VertexStreamZeroStride);
void WINAPI CxbxrImpl_DrawRectPatch(xbox::uint_xt	Handle, CONST xbox::float_xt* pNumSegs, CONST D3DRECTPATCH_INFO* pRectPatchInfo);
void WINAPI CxbxrImpl_DrawTriPatch(xbox::uint_xt	Handle, CONST xbox::float_xt* pNumSegs, CONST D3DTRIPATCH_INFO* pTriPatchInfo);
void WINAPI CxbxrImpl_DrawVertices(xbox::X_D3DPRIMITIVETYPE PrimitiveType, xbox::uint_xt StartVertex, xbox::uint_xt VertexCount);
#define USEPGRAPH_DrawVertices 0
void WINAPI CxbxrImpl_DrawVerticesUP(xbox::X_D3DPRIMITIVETYPE  PrimitiveType, xbox::uint_xt VertexCount, CONST PVOID pVertexStreamZeroData, xbox::uint_xt VertexStreamZeroStride);
#define USEPGRAPH_DrawVerticesUP 0
void CxbxrImpl_End();
void CxbxrImpl_InsertCallback(xbox::X_D3DCALLBACKTYPE Type, xbox::X_D3DCALLBACK pCallback, xbox::dword_xt Context);
void WINAPI CxbxrImpl_LightEnable(xbox::dword_xt Index, xbox::bool_xt bEnable);
void CxbxrImpl_LoadVertexShader(DWORD Handle, DWORD ProgramRegister);
#define USEPGRAPH_LoadVertexShader 0
void CxbxrImpl_LoadVertexShaderProgram(CONST DWORD* pFunction, DWORD Address);
#define USEPGRAPH_LoadVertexShaderProgram 0
void WINAPI CxbxrImpl_RunVertexStateShader(xbox::dword_xt Address, CONST xbox::float_xt* pData);
void CxbxrImpl_SelectVertexShader(DWORD Handle, DWORD Address);
#define USEPGRAPH_SelectVertexShader 0
void WINAPI CxbxrImpl_SetBackBufferScale(xbox::float_xt x, xbox::float_xt y);
void WINAPI CxbxrImpl_SetLight(xbox::dword_xt Index, CONST xbox::X_D3DLIGHT8* pLight);
void WINAPI CxbxrImpl_SetMaterial(CONST xbox::X_D3DMATERIAL8* pMaterial);
void WINAPI CxbxrImpl_SetModelView(CONST D3DMATRIX* pModelView, CONST D3DMATRIX* pInverseModelView, CONST D3DMATRIX* pComposite);
void CxbxrImpl_SetPalette(xbox::dword_xt Stage, xbox::X_D3DPalette* pPalette);
void CxbxrImpl_SetPixelShader(xbox::dword_xt Handle);
void WINAPI CxbxrImpl_SetRenderState_Simple(xbox::dword_xt Method, xbox::dword_xt Value);
//void CxbxrImpl_SetRenderTarget(xbox::X_D3DSurface* pRenderTarget, xbox::X_D3DSurface* pNewZStencil);
#define USEPGRAPH_SetRenderTarget 0 /*SetRenderTarget() is used in D3DDevice_Create() and we need it to be implemented right away. to do: */
//void CxbxrImpl_SetScreenSpaceOffset(float x, float y);
#define USEPGRAPH_SetScreenSpaceOffset 0
//void CxbxrImpl_SetStreamSource(UINT StreamNumber, xbox::X_D3DVertexBuffer* pStreamData, UINT Stride);
//#define USEPGRAPH_SetStreamSource 0 /*not permitted in pushbuffer recording*/
void WINAPI CxbxrImpl_SetTexture(xbox::dword_xt Stage, xbox::X_D3DBaseTexture* pTexture);
//void WINAPI CxbxrImpl_SetTransform(xbox::X_D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX* pMatrix);
void CxbxrImpl_SetVertexData4f(int Register, FLOAT a, FLOAT b, FLOAT c, FLOAT d);
void CxbxrImpl_SetVertexShader(DWORD Handle);
#define USEPGRAPH_SetVertexShader 0
//all SetVertexShaderConstant variants are unpatched now. but we have to use CxbxrImpl_SetVertexShaderConstant() to handle the constant change in pgraph.
void CxbxrImpl_SetVertexShaderConstant(INT Register, PVOID pConstantData, DWORD ConstantCount);
void CxbxrImpl_SetVertexShaderInput(DWORD Handle, UINT StreamCount, xbox::X_STREAMINPUT* pStreamInputs);
void WINAPI CxbxrImpl_SetVertexShaderInputDirect(xbox::X_VERTEXATTRIBUTEFORMAT* pVAF, UINT StreamCount, xbox::X_STREAMINPUT* pStreamInputs);
void CxbxrImpl_SetViewport(xbox::X_D3DVIEWPORT8* pViewport);
#define USEPGRAPH_SetViewport 1
// Present() also calls Swap(), patched LTCG version of Swap also calls Swap(). so we only handle Swap().
// xbox::dword_xt WINAPI CxbxrImpl_Swap(xbox::dword_xt Flags);
#define USEPGRAPH_Swap 0 /*Present() calles Swap() implementation. Present() is not permitted in pushbuffer recording*/
void WINAPI CxbxrImpl_SwitchTexture(xbox::dword_xt Method, xbox::dword_xt Data, xbox::dword_xt Format);
#define PGRAPHUSE_EmuKickOff 0
#define PGRAPHUSE_EmuKickOffWait 0
xbox::dword_xt* CxbxrImpl_MakeSpace(void);
