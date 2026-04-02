#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <chrono>
#include <string>
#include <vector>
#include <thread>

#pragma comment(linker, "/entry:WinMainCRTStartup /subsystem:console")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

struct Vertex {
    float x, y, z;
    float r, g, b, a;
};

const char* shaderSource = R"(
struct VS_INPUT { float3 pos : POSITION; float4 col : COLOR; };
struct PS_INPUT { float4 pos : SV_POSITION; float4 col : COLOR; };

PS_INPUT VS(VS_INPUT input) {
    PS_INPUT output;
    output.pos = float4(input.pos, 1.0f); // 3D 좌표를 4D로 확장
    output.col = input.col;
    return output;
}

float4 PS(PS_INPUT input) : SV_Target {
    return input.col; // 정점에서 계산된 색상을 픽셀에 그대로 적용
}
)";

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

class DirectSet {
public:
    ID3D11Device* g_pd3dDevice = nullptr;
    ID3D11DeviceContext* g_pImmediateContext = nullptr;
    IDXGISwapChain* g_pSwapChain = nullptr;
    ID3D11RenderTargetView* g_pRenderTargetView = nullptr;
    ID3D11VertexShader* pVertexShader = nullptr;
    ID3D11PixelShader* pPixelShader = nullptr;
    ID3D11InputLayout* pVertexLayout = nullptr;
    //ID3D11Buffer* pVertexBuffer = nullptr;

    DirectSet(HWND hWnd) {
        DXGI_SWAP_CHAIN_DESC sd = {}; // 스왑체인 설정 정보를 담는 구조체
        sd.BufferCount = 1; // 백버퍼의 수
        sd.BufferDesc.Width = 800; sd.BufferDesc.Height = 600; // 가로, 세로 크기
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // 색상당 8비트 할당
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // 백버퍼 용도 명시
        sd.OutputWindow = hWnd; // 그림을 그려서 보낼 창
        sd.SampleDesc.Count = 1; // 안티에일리어싱 (1 = X)
        sd.Windowed = TRUE; // 창모드

        D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, nullptr, &g_pImmediateContext);

        ID3D11Texture2D* pBackBuffer = nullptr;
        g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRenderTargetView);
        pBackBuffer->Release();

        ID3DBlob* vsBlob, * psBlob;
        D3DCompile(shaderSource, strlen(shaderSource), nullptr, nullptr, nullptr, "VS", "vs_4_0", 0, 0, &vsBlob, nullptr);
        D3DCompile(shaderSource, strlen(shaderSource), nullptr, nullptr, nullptr, "PS", "ps_4_0", 0, 0, &psBlob, nullptr);

        g_pd3dDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &pVertexShader);
        g_pd3dDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &pPixelShader);

        D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        g_pd3dDevice->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &pVertexLayout);
        vsBlob->Release(); psBlob->Release();
    }
    ~DirectSet() {
        //if (pVertexBuffer) pVertexBuffer->Release();
        if (pVertexLayout) pVertexLayout->Release();
        if (pVertexShader) pVertexShader->Release();
        if (pPixelShader) pPixelShader->Release();
        if (g_pRenderTargetView) g_pRenderTargetView->Release();
        if (g_pSwapChain) g_pSwapChain->Release();
        if (g_pImmediateContext) g_pImmediateContext->Release();
        if (g_pd3dDevice) g_pd3dDevice->Release();
    }
};


class Component {
public:
    class GameObject* pOwner = nullptr; // 이 기능이 누구의 것인지 저장
    bool isStarted = 0;           // Start()가 실행되었는지 체크

    virtual void Start(DirectSet* cdx) = 0;              // 초기화
    virtual void Input() {}                // 입력 (선택사항)
    virtual void Update(float dt) = 0;     // 로직 (필수)
    virtual void Render(DirectSet* cdx) {}               // 그리기 (선택사항)

    virtual ~Component() {}
};

class GameObject {
public:
    std::string name;
    std::vector<Component*> components;

    GameObject(std::string n) {
        name = n;
    }

    ~GameObject() {
        for (int i = 0; i < (int)components.size(); i++)
        {
            delete components[i];
        }
    }

    void AddComponent(Component* pComp)
    {
        pComp->pOwner = this;
        pComp->isStarted = false;
        components.push_back(pComp);
    }
};

class L_Triangle : public Component {
public:
    std::vector<Vertex> vertices;
    float speed;
    bool isUp, isDown, isLeft, isRight;
    ID3D11Buffer* pVertexBuffer;

    void Start(DirectSet* cdx) override {
        vertices = {
            { 0.0f,  0.4f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f },
            { 0.255f, -0.2f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f },
            { -0.255f, -0.2f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f },
        };

        speed = 0.0003f;
        isUp = isDown = isLeft = isRight = 0;

        D3D11_BUFFER_DESC bd = { sizeof(Vertex) * (UINT)vertices.size(), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
        D3D11_SUBRESOURCE_DATA initData = { &vertices[0], 0, 0};
        cdx->g_pd3dDevice->CreateBuffer(&bd, &initData, &pVertexBuffer);
    }

    void Input() override {
        isUp = (GetAsyncKeyState('W') & 0x8000);
        isDown = (GetAsyncKeyState('S') & 0x8000);
        isLeft = (GetAsyncKeyState('A') & 0x8000);
        isRight = (GetAsyncKeyState('D') & 0x8000);
    }

    void Update(float dt) override {
        if (isUp) {
            for (int i = 0; i < 3; i++) {
                vertices[i].y += 1.0f * dt;
            }
        }
        if (isDown) {
            for (int i = 0; i < 3; i++) {
                vertices[i].y -= 1.0f * dt;
            }
        }
        if (isLeft) {
            for (int i = 0; i < 3; i++) {
                vertices[i].x -= 1.0f * dt;
            }
        }
        if (isRight) {
            for (int i = 0; i < 3; i++) {
                vertices[i].x += 1.0f * dt;
            }
        }
    }

    void Render(DirectSet* cdx) {
        cdx->g_pImmediateContext->UpdateSubresource(pVertexBuffer, 0, nullptr, &vertices[0], 0, 0);

        cdx->g_pImmediateContext->IASetInputLayout(cdx->pVertexLayout);  // (Input Assembler) 데이터 판독기 장착
        UINT stride = sizeof(Vertex), offset = 0; // stride : 간격 , offset : 시작점
        cdx->g_pImmediateContext->IASetVertexBuffers(0, 1, &pVertexBuffer, &stride, &offset); // 정점 버퍼 올리기

        // Primitive Topology 설정: 삼각형 리스트로 연결하라!
        cdx->g_pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); // 토폴로지 설정
        cdx->g_pImmediateContext->VSSetShader(cdx->pVertexShader, nullptr, 0); // Vertex shader 설정
        cdx->g_pImmediateContext->PSSetShader(cdx->pPixelShader, nullptr, 0); // Pixel shader 설정

        // 최종 그리기
        cdx->g_pImmediateContext->Draw(3, 0);
    }

    ~L_Triangle() {
        pVertexBuffer->Release();
    }
};

class R_Triangle : public Component {
public:
    std::vector<Vertex> vertices;
    float speed;
    bool isUp, isDown, isLeft, isRight;
    ID3D11Buffer* pVertexBuffer;

    void Start(DirectSet* cdx) override {
        vertices = {
            { -0.255f,  0.2f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f },
            {  0.255f,  0.2f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f },
            {  0.0f,  -0.4f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f },
        };

        speed = 0.0003f;
        isUp = isDown = isLeft = isRight = 0;

        D3D11_BUFFER_DESC bd = { sizeof(Vertex) * (UINT)vertices.size(), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
        D3D11_SUBRESOURCE_DATA initData = { &vertices[0], 0, 0};
        cdx->g_pd3dDevice->CreateBuffer(&bd, &initData, &pVertexBuffer);
    }

    void Input() override {
        isUp = (GetAsyncKeyState(VK_UP) & 0x8000);
        isDown = (GetAsyncKeyState(VK_DOWN) & 0x8000);
        isLeft = (GetAsyncKeyState(VK_LEFT) & 0x8000);
        isRight = (GetAsyncKeyState(VK_RIGHT) & 0x8000);

        /*
        if (GetAsyncKeyState(VK_UP) & 0x8000) isUp = 1;
        if (GetAsyncKeyState(VK_DOWN) & 0x8000) isDown = 1;
        if (GetAsyncKeyState(VK_LEFT) & 0x8000) isLeft = 1;
        if (GetAsyncKeyState(VK_RIGHT) & 0x8000) isRight = 1;
        */
    }

    void Update(float dt) override {
        if (isUp) {
            for (int i = 0; i < 3; i++) {
                vertices[i].y += 1.0f * dt;
            }
        }
        if (isDown) {
            for (int i = 0; i < 3; i++) {
                vertices[i].y -= 1.0f * dt;
            }
        }
        if (isLeft) {
            for (int i = 0; i < 3; i++) {
                vertices[i].x -= 1.0f * dt;
            }
        }
        if (isRight) {
            for (int i = 0; i < 3; i++) {
                vertices[i].x += 1.0f * dt;
            }
        }
    }

    void Render(DirectSet* cdx) {
        cdx->g_pImmediateContext->UpdateSubresource(pVertexBuffer, 0, nullptr, &vertices[0], 0, 0);

        cdx->g_pImmediateContext->IASetInputLayout(cdx->pVertexLayout);  // (Input Assembler) 데이터 판독기 장착
        UINT stride = sizeof(Vertex), offset = 0; // stride : 간격 , offset : 시작점
        cdx->g_pImmediateContext->IASetVertexBuffers(0, 1, &pVertexBuffer, &stride, &offset); // 정점 버퍼 올리기

        // Primitive Topology 설정: 삼각형 리스트로 연결하라!
        cdx->g_pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); // 토폴로지 설정
        cdx->g_pImmediateContext->VSSetShader(cdx->pVertexShader, nullptr, 0); // Vertex shader 설정
        cdx->g_pImmediateContext->PSSetShader(cdx->pPixelShader, nullptr, 0); // Pixel shader 설정

        // 최종 그리기
        cdx->g_pImmediateContext->Draw(3, 0);
    }

    ~R_Triangle() {
        pVertexBuffer->Release();
    }
};

class GameLoop {
public:
    std::vector<GameObject*> gameWorld;
    std::chrono::high_resolution_clock::time_point prevTime;
    float deltaTime;

    void Initialize()
    {
        gameWorld.clear();

        prevTime = std::chrono::high_resolution_clock::now();
        deltaTime = 0.0f;
    }

    void Input() {
        for (int i = 0; i < (int)gameWorld.size(); i++)
        {
            for (int j = 0; j < (int)gameWorld[i]->components.size(); j++)
            {
                gameWorld[i]->components[j]->Input();
            }
        }
    }

    void Update(DirectSet* cdx) {
        for (int i = 0; i < (int)gameWorld.size(); i++)
        {
            for (int j = 0; j < (int)gameWorld[i]->components.size(); j++)
            {
                if (gameWorld[i]->components[j]->isStarted == false)
                {
                    gameWorld[i]->components[j]->Start(cdx);
                    gameWorld[i]->components[j]->isStarted = true;
                }
            }
        }
        for (int i = 0; i < (int)gameWorld.size(); i++)
        {
            for (int j = 0; j < (int)gameWorld[i]->components.size(); j++)
            {
                gameWorld[i]->components[j]->Update(deltaTime);
            }
        }
    }

    void Render(DirectSet* cdx) {
        float clearColor[] = { 0.1f, 0.2f, 0.3f, 1.0f }; // 배경 색
        cdx->g_pImmediateContext->ClearRenderTargetView(cdx->g_pRenderTargetView, clearColor);

        // 렌더링 파이프라인 상태 설정
        cdx->g_pImmediateContext->OMSetRenderTargets(1, &cdx->g_pRenderTargetView, nullptr); // 그리는 결과물을 해당 뷰가 가리키는 백버퍼로 보냄
        D3D11_VIEWPORT vp = { 0, 0, 800, 600, 0.0f, 1.0f }; // 해당 타겟뷰(메모리) 에서 그림을 그릴 부분
        cdx->g_pImmediateContext->RSSetViewports(1, &vp); // 뷰포트 설정

        for (int i = 0; i < (int)gameWorld.size(); i++)
        {
            for (int j = 0; j < (int)gameWorld[i]->components.size(); j++)
            {
                gameWorld[i]->components[j]->Render(cdx);
            }
        }

        // 화면 교체 (프론트 버퍼와 백 버퍼 스왑)
        cdx->g_pSwapChain->Present(0, 0);
    }

    void Run(DirectSet* cdx) { //메시지 드리븐
        MSG msg = { 0 };
        while (WM_QUIT != msg.message) {
            if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { // PM_REMOVE는 메시지 받고나서 큐에서 삭제
                TranslateMessage(&msg); // 키보드 입력을 문자로 변환
                DispatchMessage(&msg); // 위에서 만든 WndProc 함수로 메세지 전달
            }
            else {
                std::chrono::high_resolution_clock::time_point currentTime = std::chrono::high_resolution_clock::now();
                std::chrono::duration<float> elapsed = currentTime - prevTime;
                deltaTime = elapsed.count();
                prevTime = currentTime;

                Input();
                Update(cdx);
                Render(cdx);

                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }

    GameLoop() {
        Initialize();
    }
    ~GameLoop() {
        for (int i = 0; i < (int)gameWorld.size(); i++)
        {
            delete gameWorld[i]; // GameObject 소멸자가 컴포넌트들도 지움
        }
    }
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpComLine, int nCmdShow) { //여기서 조립
    WNDCLASSEXW wcex = { sizeof(WNDCLASSEX) };
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.lpszClassName = L"Lecture04-HW";
    RegisterClassExW(&wcex);

    HWND hWnd = CreateWindowW(L"Lecture04-HW", L"과제: 각각 움직이는 삼각형 만들기", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, nullptr, nullptr, hInstance, nullptr);
    if (!hWnd) return FALSE;
    ShowWindow(hWnd, nCmdShow);

    DirectSet cdx(hWnd);
    GameLoop gLoop;
    gLoop.Initialize();

    GameObject* tri_1 = new GameObject("Triangle_1");
    tri_1->AddComponent(new L_Triangle());
    gLoop.gameWorld.push_back(tri_1);

    GameObject* tri_2 = new GameObject("Triangle_2");
    tri_2->AddComponent(new R_Triangle());
    gLoop.gameWorld.push_back(tri_2);

    gLoop.Run(&cdx);

    return 0;
}