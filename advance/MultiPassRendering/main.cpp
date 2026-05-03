#pragma comment( lib, "d3d9.lib" )
#pragma comment( lib, "winmm.lib" )
#if defined(DEBUG) || defined(_DEBUG)
#pragma comment( lib, "d3dx9d.lib" )
#else
#pragma comment( lib, "d3dx9.lib" )
#endif

#include <d3d9.h>
#include <d3dx9.h>
#include <mmsystem.h>
#include <string>
#include <tchar.h>
#include <cassert>
#include <crtdbg.h>
#include <vector>

#define SAFE_RELEASE(p) { if (p) { (p)->Release(); (p) = NULL; } }

static const UINT kScreenWidth = 1600;
static const UINT kScreenHeight = 900;
static const int kGridCountPerAxis = 6;
static const float kGridSpacing = 10.0f;
static const float kGridOriginOffset = ((float)kGridCountPerAxis - 1.0f) * kGridSpacing * 0.5f;
static const float kCameraMoveSpeed = 15.0f;
static const float kCameraMouseSensitivity = 0.0025f;
static const float kCameraPitchLimit = D3DX_PI * 0.49f;
static const float kCameraMinDistance = 5.0f;
static const float kCameraMaxDistance = 120.0f;
static const float kMotionVectorFrameSeconds = 1.0f / 60.0f;
static const float kBlurScale = 2.0f;
static const float kMaxBlurPixels = 240.0f;
static const int kDebugViewMode = 0;
static const float kBackdropCubeSize = 200.0f;
static const float kMotionBlurTranslationThreshold = 0.001f;
static const float kMotionBlurRotationThreshold = 0.01f;
static const float kRotationOnlyBlurScale = 0.3f;

HWND g_hWnd = NULL;
LPDIRECT3D9 g_pD3D = NULL;
LPDIRECT3DDEVICE9 g_pd3dDevice = NULL;
LPD3DXFONT g_pFont = NULL;
LPD3DXFONT g_pLargeFont = NULL;
LPD3DXMESH g_pMesh = NULL;
LPD3DXMESH g_pBackdropCube = NULL;
LPDIRECT3DTEXTURE9 g_pBackdropTexture = NULL;

LPD3DXMESH g_pMeshSphere = NULL;

std::vector<D3DMATERIAL9> g_pMaterials;
std::vector<LPDIRECT3DTEXTURE9> g_pTextures;
DWORD g_dwNumMaterials = 0;
LPD3DXEFFECT g_pEffect1 = NULL;
LPD3DXEFFECT g_pEffect2 = NULL;

bool g_bClose = false;
bool g_bHasPrevViewProj = false;
bool g_bMotionBlurEnabled = true;
bool g_bApplyMotionBlurThisFrame = true;
float g_fMotionBlurScaleThisFrame = kBlurScale;
bool g_bTimerPeriodChanged = false;
bool g_bCameraMouseReady = false;

D3DXVECTOR3 g_vCameraEye(0.0f, 0.0f, -25.0f);
D3DXVECTOR3 g_vCameraTarget(0.0f, 0.0f, 0.0f);
float g_fCameraYaw = 0.0f;
float g_fCameraPitch = 0.0f;
float g_fCameraDistance = 25.0f;

// カメラモーションブラー用の行列を保持する。
D3DXMATRIX g_matCurrentViewProj;
D3DXMATRIX g_matPrevViewProj;
D3DXMATRIX g_matInvCurrentViewProj;

// === 変更: RT を 2 枚用意 ===
LPDIRECT3DTEXTURE9 g_pRenderTarget = NULL;
LPDIRECT3DTEXTURE9 g_pRenderTarget2 = NULL;

// フルスクリーンクアッド用
LPDIRECT3DVERTEXDECLARATION9 g_pQuadDecl = NULL;

struct QuadVertex
{
    float x, y, z, w; // クリップ空間（-1..1, w=1）
    float u, v;       // テクスチャ座標
};

static void TextDraw(LPD3DXFONT pFont, const TCHAR* text, int X, int Y, D3DCOLOR color);
static void InitD3D(HWND hWnd);
static void Cleanup();

static void RenderPass1();
static void RenderPass2();
static void DrawFullscreenQuad();
static void ReverseMeshWinding(LPD3DXMESH pMesh);
static void UpdateCamera(D3DXVECTOR3& eye,
                         D3DXVECTOR3& at,
                         D3DXVECTOR3& prevEye,
                         D3DXVECTOR3& prevAt);
static void ResetCameraMouse();
static float UpdateFps();

LRESULT WINAPI MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

extern int WINAPI _tWinMain(_In_ HINSTANCE hInstance,
                            _In_opt_ HINSTANCE hPrevInstance,
                            _In_ LPTSTR lpCmdLine,
                            _In_ int nCmdShow);

int WINAPI _tWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPTSTR lpCmdLine,
                     _In_ int nCmdShow)
{
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    // Improve Sleep timing precision while this sample is running.
    g_bTimerPeriodChanged = (timeBeginPeriod(1) == TIMERR_NOERROR);

    WNDCLASSEX wc { };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = MsgProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hIcon = NULL;
    wc.hCursor = NULL;
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = _T("Window1");
    wc.hIconSm = NULL;

    ATOM atom = RegisterClassEx(&wc);
    assert(atom != 0);

    RECT rect;
    SetRect(&rect, 0, 0, kScreenWidth, kScreenHeight);
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    rect.right = rect.right - rect.left;
    rect.bottom = rect.bottom - rect.top;
    rect.top = 0;
    rect.left = 0;

    HWND hWnd = CreateWindow(_T("Window1"),
                             _T("Hello DirectX9 World !!"),
                             WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT,
                             CW_USEDEFAULT,
                             rect.right,
                             rect.bottom,
                             NULL,
                             NULL,
                             wc.hInstance,
                             NULL);
    g_hWnd = hWnd;

    InitD3D(hWnd);
    ShowWindow(hWnd, SW_SHOWDEFAULT);
    UpdateWindow(hWnd);

    MSG msg;

    while (true)
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            DispatchMessage(&msg);
        }
        else
        {
            Sleep(15);

            RenderPass1();
            RenderPass2();
        }

        if (g_bClose)
        {
            break;
        }
    }

    Cleanup();

    if (g_bTimerPeriodChanged)
    {
        timeEndPeriod(1);
    }

    UnregisterClass(_T("Window1"), wc.hInstance);
    return 0;
}

void TextDraw(LPD3DXFONT pFont, const TCHAR* text, int X, int Y, D3DCOLOR color)
{
    RECT rect = { X, Y, 0, 0 };

    HRESULT hResult = pFont->DrawText(NULL,
                                      text,
                                      -1,
                                      &rect,
                                      DT_LEFT | DT_NOCLIP,
                                      color);

    assert((int)hResult >= 0);
}

void InitD3D(HWND hWnd)
{
    HRESULT hResult = E_FAIL;

    g_pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    assert(g_pD3D != NULL);

    D3DPRESENT_PARAMETERS d3dpp;
    ZeroMemory(&d3dpp, sizeof(d3dpp));
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
    d3dpp.BackBufferCount = 1;
    d3dpp.MultiSampleType = D3DMULTISAMPLE_NONE;
    d3dpp.MultiSampleQuality = 0;
    d3dpp.EnableAutoDepthStencil = TRUE;
    d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
    d3dpp.hDeviceWindow = hWnd;
    d3dpp.Flags = 0;
    d3dpp.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;
    d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    hResult = g_pD3D->CreateDevice(D3DADAPTER_DEFAULT,
                                   D3DDEVTYPE_HAL,
                                   hWnd,
                                   D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                   &d3dpp,
                                   &g_pd3dDevice);

    if (FAILED(hResult))
    {
        hResult = g_pD3D->CreateDevice(D3DADAPTER_DEFAULT,
                                       D3DDEVTYPE_HAL,
                                       hWnd,
                                       D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                                       &d3dpp,
                                       &g_pd3dDevice);
        assert(hResult == S_OK);
    }

    hResult = D3DXCreateFont(g_pd3dDevice,
                             20,
                             0,
                             FW_HEAVY,
                             1,
                             FALSE,
                             SHIFTJIS_CHARSET,
                             OUT_TT_ONLY_PRECIS,
                             CLEARTYPE_NATURAL_QUALITY,
                             FF_DONTCARE,
                             _T("ＭＳ ゴシック"),
                             &g_pFont);
    assert(hResult == S_OK);

    hResult = D3DXCreateFont(g_pd3dDevice,
                             56,
                             0,
                             FW_HEAVY,
                             1,
                             FALSE,
                             SHIFTJIS_CHARSET,
                             OUT_TT_ONLY_PRECIS,
                             CLEARTYPE_NATURAL_QUALITY,
                             FF_DONTCARE,
                             _T("ＭＳ ゴシック"),
                             &g_pLargeFont);
    assert(hResult == S_OK);

    LPD3DXBUFFER pD3DXMtrlBuffer = NULL;

    hResult = D3DXLoadMeshFromX(_T("cube.x"),
                                D3DXMESH_SYSTEMMEM,
                                g_pd3dDevice,
                                NULL,
                                &pD3DXMtrlBuffer,
                                NULL,
                                &g_dwNumMaterials,
                                &g_pMesh);
    assert(hResult == S_OK);

    D3DXMATERIAL* d3dxMaterials = (D3DXMATERIAL*)pD3DXMtrlBuffer->GetBufferPointer();
    g_pMaterials.resize(g_dwNumMaterials);
    g_pTextures.resize(g_dwNumMaterials);

    for (DWORD i = 0; i < g_dwNumMaterials; i++)
    {
        g_pMaterials[i] = d3dxMaterials[i].MatD3D;
        g_pMaterials[i].Ambient = g_pMaterials[i].Diffuse;
        g_pTextures[i] = NULL;

        std::string pTexPath(d3dxMaterials[i].pTextureFilename);

        if (!pTexPath.empty())
        {
            bool bUnicode = false;
#ifdef UNICODE
            bUnicode = true;
#endif
            if (!bUnicode)
            {
                hResult = D3DXCreateTextureFromFileA(g_pd3dDevice, pTexPath.c_str(), &g_pTextures[i]);
                assert(hResult == S_OK);
            }
            else
            {
                int len = MultiByteToWideChar(CP_ACP, 0, pTexPath.c_str(), -1, nullptr, 0);
                std::wstring pTexPathW(len, 0);
                MultiByteToWideChar(CP_ACP, 0, pTexPath.c_str(), -1, &pTexPathW[0], len);

                hResult = D3DXCreateTextureFromFileW(g_pd3dDevice, pTexPathW.c_str(), &g_pTextures[i]);
                assert(hResult == S_OK);
            }
        }
    }

    hResult = pD3DXMtrlBuffer->Release();
    assert(hResult == S_OK);

    hResult = D3DXCreateEffectFromFile(g_pd3dDevice,
                                       _T("simple.fx"),
                                       NULL,
                                       NULL,
                                       D3DXSHADER_DEBUG,
                                       NULL,
                                       &g_pEffect1,
                                       NULL);
    assert(hResult == S_OK);

    hResult = D3DXCreateEffectFromFile(g_pd3dDevice,
                                       _T("simple2.fx"),
                                       NULL,
                                       NULL,
                                       D3DXSHADER_DEBUG,
                                       NULL,
                                       &g_pEffect2,
                                       NULL);
    assert(hResult == S_OK);

    hResult = D3DXCreateSphere(g_pd3dDevice,
                               20.f,
                               32,
                               32,
                               &g_pMeshSphere,
                               NULL);
    assert(hResult == S_OK);

    hResult = D3DXCreateBox(g_pd3dDevice,
                            kBackdropCubeSize,
                            kBackdropCubeSize,
                            kBackdropCubeSize,
                            &g_pBackdropCube,
                            NULL);
    assert(hResult == S_OK);
    ReverseMeshWinding(g_pBackdropCube);

    hResult = D3DXCreateTextureFromFile(g_pd3dDevice,
                                        _T("backdrop4x4.bmp"),
                                        &g_pBackdropTexture);
    assert(hResult == S_OK);

    // === 変更: RT を 2 枚作成（両方 A8R8G8B8） ===
    hResult = D3DXCreateTexture(g_pd3dDevice,
                                kScreenWidth, kScreenHeight,
                                1,
                                D3DUSAGE_RENDERTARGET,
                                D3DFMT_A8R8G8B8,
                                D3DPOOL_DEFAULT,
                                &g_pRenderTarget);
    assert(hResult == S_OK);

    hResult = D3DXCreateTexture(g_pd3dDevice,
                                kScreenWidth, kScreenHeight,
                                1,
                                D3DUSAGE_RENDERTARGET,
                                D3DFMT_A8R8G8B8,
                                D3DPOOL_DEFAULT,
                                &g_pRenderTarget2);
    assert(hResult == S_OK);

    // フルスクリーンクアッドの頂宣言
    D3DVERTEXELEMENT9 elems[] =
    {
        { 0,  0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
        D3DDECL_END()
    };
    hResult = g_pd3dDevice->CreateVertexDeclaration(elems, &g_pQuadDecl);
    assert(hResult == S_OK);

    // 初回フレームで未初期化行列を使わないように単位行列を入れておく。
    D3DXMatrixIdentity(&g_matCurrentViewProj);
    D3DXMatrixIdentity(&g_matPrevViewProj);
    D3DXMatrixIdentity(&g_matInvCurrentViewProj);
}

void Cleanup()
{
    for (auto& texture : g_pTextures)
    {
        SAFE_RELEASE(texture);
    }

    SAFE_RELEASE(g_pMesh);
    SAFE_RELEASE(g_pBackdropCube);
    SAFE_RELEASE(g_pBackdropTexture);
    SAFE_RELEASE(g_pMeshSphere);
    SAFE_RELEASE(g_pEffect1);
    SAFE_RELEASE(g_pEffect2);
    SAFE_RELEASE(g_pLargeFont);
    SAFE_RELEASE(g_pFont);

    // 追加: 解放漏れ防止
    SAFE_RELEASE(g_pRenderTarget);
    SAFE_RELEASE(g_pRenderTarget2);
    SAFE_RELEASE(g_pQuadDecl);
    SAFE_RELEASE(g_pd3dDevice);
    SAFE_RELEASE(g_pD3D);
}

void ReverseMeshWinding(LPD3DXMESH pMesh)
{
    assert(pMesh != NULL);

    const DWORD faceCount = pMesh->GetNumFaces();
    HRESULT hResult = E_FAIL;

    if ((pMesh->GetOptions() & D3DXMESH_32BIT) != 0)
    {
        DWORD* indices = NULL;
        hResult = pMesh->LockIndexBuffer(0, reinterpret_cast<void**>(&indices));
        assert(hResult == S_OK);

        for (DWORD faceIndex = 0; faceIndex < faceCount; ++faceIndex)
        {
            DWORD* face = &indices[faceIndex * 3];
            const DWORD temp = face[1];
            face[1] = face[2];
            face[2] = temp;
        }

        hResult = pMesh->UnlockIndexBuffer();
        assert(hResult == S_OK);
    }
    else
    {
        WORD* indices = NULL;
        hResult = pMesh->LockIndexBuffer(0, reinterpret_cast<void**>(&indices));
        assert(hResult == S_OK);

        for (DWORD faceIndex = 0; faceIndex < faceCount; ++faceIndex)
        {
            WORD* face = &indices[faceIndex * 3];
            const WORD temp = face[1];
            face[1] = face[2];
            face[2] = temp;
        }

        hResult = pMesh->UnlockIndexBuffer();
        assert(hResult == S_OK);
    }
}

void RenderPass1()
{
    HRESULT hResult = E_FAIL;

    // 既存の RT0 を保存
    LPDIRECT3DSURFACE9 pOldRT0 = NULL;
    hResult = g_pd3dDevice->GetRenderTarget(0, &pOldRT0);
    assert(hResult == S_OK);

    // 2 枚の RT サーフェスを取得
    LPDIRECT3DSURFACE9 pRT0 = NULL;
    LPDIRECT3DSURFACE9 pRT1 = NULL;
    hResult = g_pRenderTarget->GetSurfaceLevel(0, &pRT0);  assert(hResult == S_OK);
    hResult = g_pRenderTarget2->GetSurfaceLevel(0, &pRT1); assert(hResult == S_OK);

    // Color RT and depth RT are cleared separately so the depth background stays far.
    hResult = g_pd3dDevice->SetRenderTarget(0, pRT0); assert(hResult == S_OK);
    hResult = g_pd3dDevice->SetRenderTarget(1, NULL); assert(hResult == S_OK);
    hResult = g_pd3dDevice->Clear(0, NULL,
                                  D3DCLEAR_TARGET,
                                  D3DCOLOR_XRGB(100, 100, 100),
                                  1.0f, 0);
    assert(hResult == S_OK);

    hResult = g_pd3dDevice->SetRenderTarget(0, pRT1); assert(hResult == S_OK);
    hResult = g_pd3dDevice->Clear(0, NULL,
                                  D3DCLEAR_TARGET,
                                  D3DCOLOR_XRGB(255, 255, 255),
                                  1.0f, 0);
    assert(hResult == S_OK);

    // MRT セット（スロット 0 と 1）
    hResult = g_pd3dDevice->SetRenderTarget(0, pRT0); assert(hResult == S_OK);
    hResult = g_pd3dDevice->SetRenderTarget(1, pRT1); assert(hResult == S_OK);

    D3DXMATRIX View, PrevView, Proj;

    D3DXMatrixPerspectiveFovLH(&Proj,
                               D3DXToRadian(60.0f),
                               static_cast<float>(kScreenWidth) / static_cast<float>(kScreenHeight),
                               1.0f,
                               10000.0f);

    D3DXVECTOR3 eye;
    D3DXVECTOR3 at;
    D3DXVECTOR3 prevEye;
    D3DXVECTOR3 prevAt;
    D3DXVECTOR3 up(0.0f, 1.0f, 0.0f);
    UpdateCamera(eye, at, prevEye, prevAt);
    D3DXMatrixLookAtLH(&View, &eye, &at, &up);
    D3DXMatrixLookAtLH(&PrevView, &prevEye, &prevAt, &up);

    // 現在フレームの ViewProjection とその逆行列を更新する。
    g_matCurrentViewProj = View * Proj;
    g_matPrevViewProj = PrevView * Proj;

    D3DXMATRIX matIdentity;
    D3DXMatrixIdentity(&matIdentity);
    if (D3DXMatrixInverse(&g_matInvCurrentViewProj, NULL, &g_matCurrentViewProj) == NULL)
    {
        g_matInvCurrentViewProj = matIdentity;
    }

    // 初回フレームは prev=current にして velocity を 0 にする。
    if (!g_bHasPrevViewProj)
    {
        g_matPrevViewProj = g_matCurrentViewProj;
    }

    hResult = g_pd3dDevice->Clear(0, NULL,
                                  D3DCLEAR_ZBUFFER,
                                  D3DCOLOR_XRGB(0, 0, 0),
                                  1.0f, 0);
    assert(hResult == S_OK);

    hResult = g_pd3dDevice->BeginScene(); assert(hResult == S_OK);

    // タイトル
    TCHAR msg[100];
    _tcscpy_s(msg, 100, _T("Camera Motion Blur Prep"));
    TextDraw(g_pFont, msg, 0, 0, D3DCOLOR_ARGB(255, 0, 0, 0));

    // === 変更: MRT 用テクニックを使用 ===
    hResult = g_pEffect1->SetTechnique("TechniqueMRT");
    assert(hResult == S_OK);

    UINT numPass = 0;
    hResult = g_pEffect1->Begin(&numPass, 0); assert(hResult == S_OK);
    hResult = g_pEffect1->BeginPass(0);       assert(hResult == S_OK);

    D3DXMATRIX backdropWorld;
    D3DXMATRIX backdropWorldViewProj;
    D3DXMatrixIdentity(&backdropWorld);
    backdropWorldViewProj = backdropWorld * g_matCurrentViewProj;

    hResult = g_pEffect1->SetBool("g_bUseTexture", TRUE);                           assert(hResult == S_OK);
    hResult = g_pEffect1->SetTexture("texture1", g_pBackdropTexture);               assert(hResult == S_OK);
    hResult = g_pEffect1->SetMatrix("g_matWorldViewProj", &backdropWorldViewProj);  assert(hResult == S_OK);
    hResult = g_pEffect1->CommitChanges();                                          assert(hResult == S_OK);
    hResult = g_pBackdropCube->DrawSubset(0);                                       assert(hResult == S_OK);

    // 10m 間隔で 10x10x10 個並べたモデル群
    hResult = g_pEffect1->SetBool("g_bUseTexture", TRUE); assert(hResult == S_OK);
    for (int ix = 0; ix < kGridCountPerAxis; ++ix)
    {
        for (int iy = 0; iy < kGridCountPerAxis; ++iy)
        {
            for (int iz = 0; iz < kGridCountPerAxis; ++iz)
            {
                D3DXMATRIX world;
                D3DXMATRIX worldViewProj;
                const float x = ix * kGridSpacing - kGridOriginOffset;
                const float y = iy * kGridSpacing - kGridOriginOffset;
                const float z = iz * kGridSpacing - kGridOriginOffset;

                D3DXMatrixTranslation(&world, x, y, z);
                worldViewProj = world * g_matCurrentViewProj;

                hResult = g_pEffect1->SetMatrix("g_matWorldViewProj", &worldViewProj);
                assert(hResult == S_OK);

                for (DWORD i = 0; i < g_dwNumMaterials; i++)
                {
                    hResult = g_pEffect1->SetTexture("texture1", g_pTextures[i]); assert(hResult == S_OK);
                    hResult = g_pEffect1->CommitChanges();                         assert(hResult == S_OK);
                    hResult = g_pMesh->DrawSubset(i);                              assert(hResult == S_OK);
                }
            }
        }
    }

    hResult = g_pEffect1->EndPass(); assert(hResult == S_OK);
    hResult = g_pEffect1->End();     assert(hResult == S_OK);

    hResult = g_pd3dDevice->EndScene(); assert(hResult == S_OK);

    // MRT を解除してバックバッファへ戻す
    hResult = g_pd3dDevice->SetRenderTarget(1, NULL);   assert(hResult == S_OK);
    hResult = g_pd3dDevice->SetRenderTarget(0, pOldRT0); assert(hResult == S_OK);

    SAFE_RELEASE(pRT0);
    SAFE_RELEASE(pRT1);
    SAFE_RELEASE(pOldRT0);
}

void RenderPass2()
{
    HRESULT hResult = E_FAIL;

    hResult = g_pd3dDevice->Clear(0, NULL,
                                  D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                                  D3DCOLOR_XRGB(0, 0, 0),
                                  1.0f, 0);
    assert(hResult == S_OK);

    // 2D 全面描画なので Z 無効
    hResult = g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    assert(hResult == S_OK);

    hResult = g_pd3dDevice->BeginScene(); assert(hResult == S_OK);

    // フルスクリーン: RT0 を simple2.fx で表示
    hResult = g_pEffect2->SetTechnique("Technique1");       assert(hResult == S_OK);

    UINT numPass = 0;
    hResult = g_pEffect2->Begin(&numPass, 0);               assert(hResult == S_OK);
    hResult = g_pEffect2->BeginPass(0);                     assert(hResult == S_OK);

    // ポストエフェクト側へカラー、深度、行列、調整定数を渡す。
    D3DXVECTOR4 texelSize(1.0f / static_cast<float>(kScreenWidth),
                          1.0f / static_cast<float>(kScreenHeight),
                          static_cast<float>(kScreenWidth),
                          static_cast<float>(kScreenHeight));

    hResult = g_pEffect2->SetTexture("texture1", g_pRenderTarget);                   assert(hResult == S_OK);
    hResult = g_pEffect2->SetTexture("depthTexture", g_pRenderTarget2);              assert(hResult == S_OK);
    hResult = g_pEffect2->SetMatrix("g_matInvCurrentViewProj", &g_matInvCurrentViewProj); assert(hResult == S_OK);
    hResult = g_pEffect2->SetMatrix("g_matPrevViewProj", &g_matPrevViewProj);        assert(hResult == S_OK);
    hResult = g_pEffect2->SetFloat("g_fBlurScale", g_fMotionBlurScaleThisFrame);     assert(hResult == S_OK);
    hResult = g_pEffect2->SetFloat("g_fMaxBlurPixels", kMaxBlurPixels);              assert(hResult == S_OK);
    hResult = g_pEffect2->SetVector("g_vTexelSize", &texelSize);                     assert(hResult == S_OK);
    hResult = g_pEffect2->SetInt("g_iDebugViewMode", kDebugViewMode);                assert(hResult == S_OK);
    hResult = g_pEffect2->SetInt("g_iMotionBlurEnabled",
                                 (g_bMotionBlurEnabled && g_bApplyMotionBlurThisFrame) ? 1 : 0);
    assert(hResult == S_OK);
    hResult = g_pEffect2->CommitChanges();                                            assert(hResult == S_OK);

    DrawFullscreenQuad();

    hResult = g_pEffect2->EndPass(); assert(hResult == S_OK);
    hResult = g_pEffect2->End();     assert(hResult == S_OK);

    // Draw FPS after the post effect so it stays readable on the final image.
    TCHAR fpsText[64];
    _stprintf_s(fpsText, 64, _T("FPS: %.1f"), UpdateFps());
    TextDraw(g_pFont, fpsText, 8, 8, D3DCOLOR_ARGB(255, 255, 255, 255));

    TextDraw(g_pFont,
             _T("Press 1: Motion Blur ON/OFF"),
             8,
             36,
             D3DCOLOR_ARGB(255, 255, 255, 255));

    TextDraw(g_pLargeFont,
             g_bMotionBlurEnabled ? _T("MOTION BLUR: ON") : _T("MOTION BLUR: OFF"),
             8,
             64,
             g_bMotionBlurEnabled ? D3DCOLOR_ARGB(255, 0, 255, 0) : D3DCOLOR_ARGB(255, 255, 0, 0));

    hResult = g_pd3dDevice->EndScene();  assert(hResult == S_OK);
    hResult = g_pd3dDevice->Present(NULL, NULL, NULL, NULL); assert(hResult == S_OK);

    g_bHasPrevViewProj = true;

    hResult = g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);
    assert(hResult == S_OK);
}

void DrawFullscreenQuad()
{
    QuadVertex v[4] { };

    float du = 0.5f / static_cast<float>(kScreenWidth);
    float dv = 0.5f / static_cast<float>(kScreenHeight);

    v[0].x = -1.0f; v[0].y = -1.0f; v[0].z = 0.0f; v[0].w = 1.0f; v[0].u = 0.0f + du; v[0].v = 1.0f - dv;
    v[1].x = -1.0f; v[1].y = 1.0f; v[1].z = 0.0f; v[1].w = 1.0f; v[1].u = 0.0f + du; v[1].v = 0.0f + dv;
    v[2].x = 1.0f; v[2].y = -1.0f; v[2].z = 0.0f; v[2].w = 1.0f; v[2].u = 1.0f - du; v[2].v = 1.0f - dv;
    v[3].x = 1.0f; v[3].y = 1.0f; v[3].z = 0.0f; v[3].w = 1.0f; v[3].u = 1.0f - du; v[3].v = 0.0f + dv;

    g_pd3dDevice->SetVertexDeclaration(g_pQuadDecl);
    g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(QuadVertex));
}

void UpdateCamera(D3DXVECTOR3& eye,
                  D3DXVECTOR3& at,
                  D3DXVECTOR3& prevEye,
                  D3DXVECTOR3& prevAt)
{
    static ULONGLONG s_prevTick = GetTickCount64();

    const ULONGLONG currentTick = GetTickCount64();
    float deltaSeconds = static_cast<float>(currentTick - s_prevTick) / 1000.0f;
    s_prevTick = currentTick;

    if (deltaSeconds > 0.1f)
    {
        deltaSeconds = 0.1f;
    }

    const D3DXVECTOR3 oldCameraEye = g_vCameraEye;
    const D3DXVECTOR3 oldCameraTarget = g_vCameraTarget;
    const float oldCameraYaw = g_fCameraYaw;
    const float oldCameraPitch = g_fCameraPitch;
    const float oldCameraDistance = g_fCameraDistance;

    if (g_hWnd != NULL && GetForegroundWindow() == g_hWnd)
    {
        POINT center;
        RECT clientRect;
        GetClientRect(g_hWnd, &clientRect);
        center.x = (clientRect.left + clientRect.right) / 2;
        center.y = (clientRect.top + clientRect.bottom) / 2;
        ClientToScreen(g_hWnd, &center);

        if (!g_bCameraMouseReady)
        {
            SetCursorPos(center.x, center.y);
            ShowCursor(FALSE);
            g_bCameraMouseReady = true;
        }
        else
        {
            POINT current;
            GetCursorPos(&current);
            const int dx = current.x - center.x;
            const int dy = current.y - center.y;

            g_fCameraYaw += static_cast<float>(dx) * kCameraMouseSensitivity;
            g_fCameraPitch += static_cast<float>(dy) * kCameraMouseSensitivity;

            if (g_fCameraPitch < -kCameraPitchLimit)
            {
                g_fCameraPitch = -kCameraPitchLimit;
            }
            else if (g_fCameraPitch > kCameraPitchLimit)
            {
                g_fCameraPitch = kCameraPitchLimit;
            }

            SetCursorPos(center.x, center.y);
        }
    }
    else
    {
        ResetCameraMouse();
    }

    D3DXVECTOR3 forward(cosf(g_fCameraPitch) * sinf(g_fCameraYaw),
                        -sinf(g_fCameraPitch),
                        cosf(g_fCameraPitch) * cosf(g_fCameraYaw));
    D3DXVec3Normalize(&forward, &forward);

    D3DXVECTOR3 orbitForward(forward.x, 0.0f, forward.z);
    if (D3DXVec3LengthSq(&orbitForward) > 0.0f)
    {
        D3DXVec3Normalize(&orbitForward, &orbitForward);
    }
    else
    {
        orbitForward = D3DXVECTOR3(0.0f, 0.0f, 1.0f);
    }

    D3DXVECTOR3 right(orbitForward.z, 0.0f, -orbitForward.x);
    D3DXVec3Normalize(&right, &right);

    D3DXVECTOR3 targetMove(0.0f, 0.0f, 0.0f);
    if (GetAsyncKeyState('W') & 0x8000)
    {
        g_fCameraDistance -= kCameraMoveSpeed * deltaSeconds;
    }
    if (GetAsyncKeyState('S') & 0x8000)
    {
        g_fCameraDistance += kCameraMoveSpeed * deltaSeconds;
    }
    if (GetAsyncKeyState('D') & 0x8000)
    {
        targetMove += right;
    }
    if (GetAsyncKeyState('A') & 0x8000)
    {
        targetMove -= right;
    }
    if (GetAsyncKeyState('E') & 0x8000)
    {
        targetMove += orbitForward;
    }
    if (GetAsyncKeyState('Q') & 0x8000)
    {
        targetMove -= orbitForward;
    }

    if (g_fCameraDistance < kCameraMinDistance)
    {
        g_fCameraDistance = kCameraMinDistance;
    }
    else if (g_fCameraDistance > kCameraMaxDistance)
    {
        g_fCameraDistance = kCameraMaxDistance;
    }

    if (D3DXVec3LengthSq(&targetMove) > 0.0f)
    {
        D3DXVec3Normalize(&targetMove, &targetMove);
        g_vCameraTarget += targetMove * (kCameraMoveSpeed * deltaSeconds);
    }

    g_vCameraEye = g_vCameraTarget - forward * g_fCameraDistance;

    const float motionScale = min(1.0f, kMotionVectorFrameSeconds / max(deltaSeconds, 0.0001f));
    const D3DXVECTOR3 motionVector = g_vCameraEye - oldCameraEye;
    const D3DXVECTOR3 targetMotion = g_vCameraTarget - oldCameraTarget;
    const float yawMotion = g_fCameraYaw - oldCameraYaw;
    const float pitchMotion = g_fCameraPitch - oldCameraPitch;
    const float distanceMotion = g_fCameraDistance - oldCameraDistance;
    const float targetTranslationMotion = D3DXVec3Length(&targetMotion);
    const float zoomMotion = fabsf(distanceMotion);
    const float translationMotion = max(targetTranslationMotion, zoomMotion);
    const float rotationMotion = max(fabsf(yawMotion), fabsf(pitchMotion));
    const bool hasTranslationMotion = (translationMotion > kMotionBlurTranslationThreshold);
    const bool hasRotationMotion = (rotationMotion > kMotionBlurRotationThreshold);

    g_bApplyMotionBlurThisFrame =
        hasTranslationMotion || hasRotationMotion;

    g_fMotionBlurScaleThisFrame = kBlurScale;
    if (!hasTranslationMotion && hasRotationMotion)
    {
        g_fMotionBlurScaleThisFrame *= kRotationOnlyBlurScale;
    }

    eye = g_vCameraEye;
    const D3DXVECTOR3 prevTarget = g_vCameraTarget - targetMotion * motionScale;
    const float prevDistance = g_fCameraDistance - distanceMotion * motionScale;

    const float prevYaw = g_fCameraYaw - yawMotion * motionScale;
    const float prevPitch = g_fCameraPitch - pitchMotion * motionScale;
    D3DXVECTOR3 prevForward(cosf(prevPitch) * sinf(prevYaw),
                            -sinf(prevPitch),
                            cosf(prevPitch) * cosf(prevYaw));
    D3DXVec3Normalize(&prevForward, &prevForward);

    prevEye = prevTarget - prevForward * prevDistance;
    at = g_vCameraTarget;
    prevAt = prevTarget;
}

void ResetCameraMouse()
{
    if (g_bCameraMouseReady)
    {
        ShowCursor(TRUE);
        g_bCameraMouseReady = false;
    }
}

float UpdateFps()
{
    static ULONGLONG s_prevTick = GetTickCount64();
    static float s_fps = 0.0f;
    static int s_frameCount = 0;
    static float s_elapsedSeconds = 0.0f;

    const ULONGLONG currentTick = GetTickCount64();
    const float deltaSeconds = static_cast<float>(currentTick - s_prevTick) / 1000.0f;
    s_prevTick = currentTick;

    ++s_frameCount;
    s_elapsedSeconds += deltaSeconds;

    if (s_elapsedSeconds >= 0.5f)
    {
        s_fps = static_cast<float>(s_frameCount) / s_elapsedSeconds;
        s_frameCount = 0;
        s_elapsedSeconds = 0.0f;
    }

    return s_fps;
}

LRESULT WINAPI MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_KEYDOWN:
    {
        if (wParam == '1')
        {
            g_bMotionBlurEnabled = !g_bMotionBlurEnabled;
            return 0;
        }
        break;
    }

    case WM_DESTROY:
    {
        ResetCameraMouse();
        PostQuitMessage(0);
        g_bClose = true;
        return 0;
    }
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}
