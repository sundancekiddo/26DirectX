/*
================================================================================
 [Engine Architecture]
 1. WindowContext: Win32 창 생성 및 메시지 루프 관리
 2. GraphicsContext: DX11 디바이스, 스왑체인, 셰이더 컴파일 및 영상 설정 관리
 3. DeltaTime: 고해상도 타이머를 이용한 시간 계산
 4. GameObject & Component: 객체 지향적 기능 확장 구조
 5. GameLoop: 전체 흐름(Input-Update-Render) 제어
================================================================================
*/

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <directxmath.h>
#include <vector>
#include <chrono>
#include <string>

/*
코드 보는법
1. pragma 보기. 윈도우 프로그래밍이고, 서브시스템으로 콘솔창 띄우고, 다이렉트x 쓸거고, 네임스페이스 다이렉트x.
2. 다음은 메인함수 보기. WinMain. 왜냐하면 프로그램의 시작지점이니까. */

#pragma comment(linker, "/entry:WinMainCRTStartup /subsystem:console")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace DirectX;

struct Vertex
{
    XMFLOAT3 pos; XMFLOAT4 col;
};
struct ConstantBuffer
{
    XMMATRIX matWorld;
};

struct VideoConfig {
    const int Width = 800;
    const int Height = 800;
}g_Config;

class DeltaTime
{
    std::chrono::high_resolution_clock::time_point prevTime;
public:
    DeltaTime()
    {
        prevTime = std::chrono::high_resolution_clock::now();
    }

    float GetDelta()
    {
        std::chrono::steady_clock::time_point currTime = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(currTime - prevTime).count();
        prevTime = currTime;
        return dt;
    }
};

class WindowContext
{
public:
    HWND hWnd;
    int Width, Height;
    LPCWSTR windowName;

    WindowContext(LPCWSTR winName = L"DX11 Component Engine")
        : windowName(winName), hWnd(nullptr), Width(800), Height(600)
    {
    }

    ~WindowContext()
    {
        UnregisterClass(L"DX11Engine", GetModuleHandle(NULL));
    }

    bool Initialize(HINSTANCE hInst, int w, int h, LRESULT(CALLBACK* wndProc)(HWND, UINT, WPARAM, LPARAM))
    {
        Width = w; Height = h;

        WNDCLASSEX wc = { sizeof(WNDCLASSEX) };
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = wndProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = L"DX11Engine";

        if (!RegisterClassEx(&wc)) return false;

        hWnd = CreateWindow(L"DX11Engine", windowName, WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, 1280, 1060,
            NULL, NULL, hInst, NULL);

        if (!hWnd) return false;

        ShowWindow(hWnd, SW_SHOW);
        return true;
    }
};

class GraphicsContext {
public:
    ID3D11Device* Device = nullptr;
    ID3D11DeviceContext* ImmediateContext = nullptr;
    IDXGISwapChain* SwapChain = nullptr;
    ID3D11RenderTargetView* RTV = nullptr;

    bool IsFullscreen = false;
    bool IsSize = false;
    int VSync = 1;

    bool InitDX(HWND hWnd, int w, int h)
    {
        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount = 1;
        sd.BufferDesc.Width = w; sd.BufferDesc.Height = h;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hWnd; sd.SampleDesc.Count = 1; sd.Windowed = TRUE;

        HRESULT hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0,
            D3D11_SDK_VERSION, &sd, &SwapChain, &Device, NULL, &ImmediateContext);

        return SUCCEEDED(hr) && CreateRTV(w, h);
    }

    bool CreateRTV(int w, int h)
    {
        if (RTV) RTV->Release();
        ID3D11Texture2D* pBB;
        SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBB);
        Device->CreateRenderTargetView(pBB, NULL, &RTV);
        pBB->Release();
        return true;
    }

    void Resize(int w, int h)
    {
        ImmediateContext->OMSetRenderTargets(0, 0, 0);
        RTV->Release(); RTV = nullptr;
        SwapChain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
        CreateRTV(w, h);
    }

    void SetFullscreen(bool goFull)
    {
        IsFullscreen = goFull;
        SwapChain->SetFullscreenState(goFull, NULL);
    }

    ID3DBlob* CompileShader(const std::string& src, const std::string& entry, const std::string& profile) {
        ID3DBlob* blob = nullptr;
        D3DCompile(src.c_str(), src.length(), NULL, NULL, NULL, entry.c_str(), profile.c_str(), 0, 0, &blob, NULL);
        return blob;
    }

    ~GraphicsContext() {
        if (RTV)
            RTV->Release();
        if (SwapChain)
            SwapChain->Release();
        if (ImmediateContext)
            ImmediateContext->Release();
        if (Device)
            Device->Release();
    }
};

class GameObject;
class Component
{
public:
    GameObject* pOwner = nullptr;
    bool isStarted = false;

    Component() {}
    virtual void Start(GraphicsContext* gfx) = 0;
    virtual void Input() = 0;
    virtual void Update(float dt) = 0;
    virtual void Render(GraphicsContext* gfx) = 0;
    virtual ~Component() {}
};

class GameObject
{
public:
    XMFLOAT3 pos = { 0, 0, 0 };
    XMFLOAT3 rot = { 0, 0, 0 };
    XMFLOAT3 scale = { 1, 1, 1 };
    std::vector<Component*> components;

    GameObject(float x, float y, float z)
    {
        pos.x = x;
        pos.y = y;
        pos.z = z;
    }
    ~GameObject()
    {
        for (int i = 0; i < (int)components.size(); i++)
            delete components[i];
    }

    void AddComponent(Component* c)
    {
        c->pOwner = this;
        components.push_back(c);
    }

    void Input()
    {
        int componentCount = (int)components.size();
        for (int i = 0; i < componentCount; i++)
        {
            if (components[i] != nullptr)
            {
                components[i]->Input();
            }
        }
    }

    void Update(float dt, GraphicsContext* gfx)
    {
        //3번문제
        for (int i = 0; i < components.size(); i++) {
            if (components[i] != nullptr) {
                components[i]->Update(dt);
            }
        }

    }
    void Render(GraphicsContext* gfx)
    {
        for (int i = 0; i < components.size(); i++)
        {
            if (components[i] != nullptr)
            {
                components[i]->Render(gfx);
            }
        }
    }
};


struct Mesh
{
    ID3D11Buffer* vBuffer;
    ID3D11InputLayout* pInputLayout;
    ID3D11VertexShader* pVS;
    ID3D11PixelShader* pPS;
    UINT vertexCount;
    XMFLOAT4 color;

    Mesh()
        : vBuffer(NULL), pInputLayout(NULL), pVS(NULL), pPS(NULL), vertexCount(0)
    {
        color = { 1, 1, 1, 1 };
    }

    ~Mesh()
    {
        if (vBuffer) vBuffer->Release();
        if (pInputLayout) pInputLayout->Release();
        if (pVS) pVS->Release();
        if (pPS) pPS->Release();
    }
};


class MeshRenderer : public Component
{
    Mesh* pMeshData = nullptr;
    ID3D11Buffer* cBuffer = nullptr;

public:
    MeshRenderer(Mesh* mesh) : Component(), pMeshData(mesh)
    {
    }

    ~MeshRenderer()
    {
        if (cBuffer) cBuffer->Release();
        if (pMeshData) delete pMeshData;
    }

    void Start(GraphicsContext* gfx) override
    {
        D3D11_BUFFER_DESC cbd = { 0 };
        cbd.Usage = D3D11_USAGE_DEFAULT;
        cbd.ByteWidth = sizeof(ConstantBuffer);
        cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

        gfx->Device->CreateBuffer(&cbd, nullptr, &cBuffer);
    }

    void Render(GraphicsContext* gfx) override
    {
        if (pMeshData == nullptr || pMeshData->vBuffer == nullptr) return;

        gfx->ImmediateContext->IASetInputLayout(pMeshData->pInputLayout);
        gfx->ImmediateContext->VSSetShader(pMeshData->pVS, nullptr, 0);
        gfx->ImmediateContext->PSSetShader(pMeshData->pPS, nullptr, 0);

        float s = 1.0f / (pOwner->pos.z + 1.0f);
        XMMATRIX world = XMMatrixScaling(s, s, s) * XMMatrixRotationZ(pOwner->rot.z) * XMMatrixTranslation(pOwner->pos.x, pOwner->pos.y, 0.0f);
        ConstantBuffer cb;
        cb.matWorld = XMMatrixTranspose(world);
        gfx->ImmediateContext->UpdateSubresource(cBuffer, 0, nullptr, &cb, 0, 0);

        UINT stride = sizeof(Vertex), offset = 0;
        gfx->ImmediateContext->IASetVertexBuffers(0, 1, &pMeshData->vBuffer, &stride, &offset);
        gfx->ImmediateContext->VSSetConstantBuffers(0, 1, &cBuffer);
        gfx->ImmediateContext->Draw(pMeshData->vertexCount, 0);
    }
    void Input() override {}
    void Update(float dt) override {}
};

class PlayerController : public Component
{
    // 입력 상태를 저장하기 위한 멤버 변수 (내부용)
    XMFLOAT2 moveDir;  // x: 좌우, y: 상하
    float    rotDir;   // 회전 방향
    float    zoomDir;  // 확대/축소 방향

public:
    PlayerController() : Component()
    {
        moveDir = { 0, 0 };
        rotDir = 0.0f;
        zoomDir = 0.0f;
    }

    ~PlayerController()
    {
    }

    void Start(GraphicsContext* gfx) override
    {
        printf("[Component] 플레이어 컨트롤러 로드 완료\n");
    }

    // [Step 1] 입력 감지 및 상태 저장
    void Input() override
    {
        // 매 프레임 입력 상태 초기화
        moveDir = { 0, 0 };
        rotDir = 0.0f;
        zoomDir = 0.0f;

        // 방향키 입력 (이동)
        if (GetAsyncKeyState(VK_UP) & 0x8000)    moveDir.y += 1.0f;
        if (GetAsyncKeyState(VK_DOWN) & 0x8000)  moveDir.y -= 1.0f;
        if (GetAsyncKeyState(VK_LEFT) & 0x8000)  moveDir.x -= 1.0f;
        if (GetAsyncKeyState(VK_RIGHT) & 0x8000) moveDir.x += 1.0f;

        // AD 키 입력 (회전)
        if (GetAsyncKeyState('A') & 0x8000) rotDir += 1.0f;
        if (GetAsyncKeyState('D') & 0x8000) rotDir -= 1.0f;

        // WS 키 입력 (줌)
        if (GetAsyncKeyState('W') & 0x8000) zoomDir -= 1.0f;
        if (GetAsyncKeyState('S') & 0x8000) zoomDir += 1.0f;
    }

    // [Step 2] 저장된 상태를 바탕으로 데이터 갱신
    void Update(float dt) override
    {
        // 1. 속도 정의 (사이즈 비례 속도 적용 가능)
        float speedFactor = pOwner->scale.x;
        float moveSpeed = 2.0f * speedFactor;
        float rotateSpeed = 3.0f * speedFactor;
        float zoomSpeed = 5.0f * speedFactor;

        // 2. 위치 업데이트
        pOwner->pos.x += moveDir.x * moveSpeed * dt;
        pOwner->pos.y += moveDir.y * moveSpeed * dt;

        // 3. 회전 업데이트
        pOwner->rot.z += rotDir * rotateSpeed * dt;

        // 4. 줌(Z축) 업데이트 및 제한
        pOwner->pos.z += zoomDir * zoomSpeed * dt;

        if (pOwner->pos.z < -0.9f)
        {
            pOwner->pos.z = -0.9f;
        }
    }

    void Render(GraphicsContext* gfx) override
    {
    }
};

class GameLoop
{
public:
    WindowContext win;
    GraphicsContext gfx;
    DeltaTime timer;
    std::vector<GameObject*> world;
    bool isRunning = false;

    ID3D11VertexShader* pDefaultVS = nullptr;
    ID3D11PixelShader* pDefaultPS = nullptr;
    ID3D11InputLayout* pDefaultLayout = nullptr;

    GameLoop() : isRunning(true)
    {
        world.clear();
        printf("[Engine] GameLoop Created.\n");
    }

    ~GameLoop()
    {
        for (int i = 0; i < (int)world.size(); i++)
        {
            if (world[i])
            {
                delete world[i];
                world[i] = nullptr;
            }
        }
        world.clear();

        if (pDefaultLayout) pDefaultLayout->Release();
        if (pDefaultVS) pDefaultVS->Release();
        if (pDefaultPS) pDefaultPS->Release();

        printf("[Engine] GameLoop Destroyed. All resources released.\n");
    }

    void Initialize(HINSTANCE hInst, LRESULT(CALLBACK* wndProc)(HWND, UINT, WPARAM, LPARAM))
    {
        win.Initialize(hInst, 800, 800, wndProc);
        gfx.InitDX(win.hWnd, 800, 800);
    }

    void Input()
    {
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
            isRunning = false;
        if (GetAsyncKeyState('F') & 0x0001)
            gfx.SetFullscreen(!gfx.IsFullscreen);
        if (GetAsyncKeyState('C') & 0x0001)
            gfx.Resize(600, 600);

        // 2. 월드 내 모든 오브젝트에 입력 전파
        int objectCount = (int)world.size();
        for (int i = 0; i < objectCount; i++)
        {
            if (world[i] != nullptr)
            {
                world[i]->Input();
            }
        }
    }

    void Update()
    {
        float dt = timer.GetDelta();
        for (int i = 0; i < (int)world.size(); i++)
        {
            if (world[i] != nullptr)
            {
                world[i]->Update(dt, &gfx);
            }
        }
    }

    void Render()
    {
        float col[] = { 0.1f, 0.2f, 0.3f, 1.0f };
        gfx.ImmediateContext->ClearRenderTargetView(gfx.RTV, col);

        D3D11_VIEWPORT vp = { 0, 0, (float)win.Width, (float)win.Height, 0, 1 };
        gfx.ImmediateContext->RSSetViewports(1, &vp);
        gfx.ImmediateContext->OMSetRenderTargets(1, &gfx.RTV, NULL);

        if (pDefaultLayout)
        {
            gfx.ImmediateContext->IASetInputLayout(pDefaultLayout);
        }
        gfx.ImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        for (int i = 0; i < (int)world.size(); i++)
        {
            if (world[i] != nullptr)
            {
                world[i]->Render(&gfx);
            }
        }
        gfx.SwapChain->Present(gfx.VSync, 0);
    }

    void Run()
    {
        MSG msg = {};
        while (msg.message != WM_QUIT && isRunning)
        {
            if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg); DispatchMessage(&msg);
            }
            else
            {
                Input();
                Update();
                Render();
            }
        }
    }
};

LRESULT CALLBACK GlobalWndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_DESTROY) PostQuitMessage(0);
    return DefWindowProc(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hI, HINSTANCE, LPSTR, int nS)
{
    GameLoop gEngine;
    gEngine.Initialize(hI, GlobalWndProc);
    std::string triShader = R"(
cbuffer MoveBuffer : register(b0) 
        {
            float4 g_Offset;
        };
struct VS_INPUT { float3 pos : POSITION; float4 col : COLOR; };
struct PS_INPUT { float4 pos : SV_POSITION; float4 col : COLOR; };

PS_INPUT VS(VS_INPUT input) {
    PS_INPUT output;
    output.pos = float4(input.pos, 1.0f); // 3D 좌표를 4D로 확장
    output.col = input.col;
    return output;
}

float4 PS(PS_INPUT input) : SV_Target {
    return input.col;
}
)"; //
    ID3DBlob* vsBlob = gEngine.gfx.CompileShader(triShader, "VS", "vs_5_0");
    ID3DBlob* psBlob = gEngine.gfx.CompileShader(triShader, "PS", "ps_5_0");

    Mesh* myTri = new Mesh();
    Mesh* myStar = new Mesh();
    myTri->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    myTri->vertexCount = 3;

    gEngine.gfx.Device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), NULL, &myTri->pVS);
    gEngine.gfx.Device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), NULL, &myTri->pPS);

    Vertex v[] = { { {0, 0.2f, 0}, myTri->color }, { {0.2f, -0.2f, 0}, myTri->color }, { {-0.2f, -0.2f, 0}, myTri->color } };
    D3D11_BUFFER_DESC bd = { sizeof(v), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER };
    D3D11_SUBRESOURCE_DATA sd = { v };
    gEngine.gfx.Device->CreateBuffer(&bd, &sd, &myTri->vBuffer);

    D3D11_INPUT_ELEMENT_DESC ied[] = { { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
    gEngine.gfx.Device->CreateBuffer(&bd, &sd, &myTri->vBuffer);
    gEngine.gfx.Device->CreateInputLayout(ied, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &myTri->pInputLayout);

    vsBlob->Release(); psBlob->Release();

    GameObject* triangle = new GameObject(0, 0, 0);
    triangle->AddComponent(new MeshRenderer(myTri));
    triangle->AddComponent(new PlayerController());
    gEngine.world.push_back(triangle);

    for (int i = 0; i < (int)gEngine.world.size(); i++)
    {
        for (int j = 0; j < (int)gEngine.world[i]->components.size(); j++)
        {
            if (gEngine.world[i]->components[j]->isStarted == false) {
                gEngine.world[i]->components[j]->Start(&gEngine.gfx);
                gEngine.world[i]->components[j]->isStarted = true;
            }
        }
    }

    gEngine.Run();

    return 0;
}