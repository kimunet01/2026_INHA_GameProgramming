#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <chrono>
#include <string>
#include <vector>
#include <thread>

// /entry:WinMainCRTStartUP -> WinMain(시작점)을 링커에게 호출하라고 명령
// subsystem:console -> 디버깅용 콘솔 띄우기
#pragma comment(linker, "/entry:WinMainCRTStartup /subsystem:console")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")  // 셰이더 Language를 컴파일 하기 위함

struct Vertex {
    float x, y, z;
    float r, g, b, a;
};

// HLSL (High-Level Shading Language) 소스 -> String 형식으로 GPU 메모리에 복사
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

// 프로시저 선언 (WndProc, 메시지 생성 및 전달)
// HWND : 윈도우창을 식별하기 위한 핸들
// UINT : Unsigned Int (메시지 번호)
// WPARAM : 키보드 입력
// LPARAM : 마우스 입력
// DefWindowProc : 기타적인 메시지는 OS가 기본으로 처리하도록
// 여기서 메시지를 발생시켜 메시지 핸들러로 전달
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

class DirectSet {       // DX11 관련 설정을 객체로 만들어 main문을 좀 더 가볍게 하였음.
public:
    ID3D11Device* g_pd3dDevice = nullptr;                                   // 리소스 생성자, VRAM내의 리소스 할당/관리 (Buffer, Texture, Shader 생성)
    ID3D11DeviceContext* g_pImmediateContext = nullptr;        // GPU에 실행 명령 전달 (Draw Call)
    IDXGISwapChain* g_pSwapChain = nullptr;                             // 화면 전환 (더블 버퍼링)
    ID3D11RenderTargetView* g_pRenderTargetView = nullptr;  // 그림을 그릴 도화지. 파이프 라인 출력단계에서 데이터가 기록될 실제 메모리 위치 지정 (데이터를 구분을 나누어 봄)
                                                                                                         // 백버퍼에 있는 데이터 정보들을 일정한 규칙에 맞춰 구문하는 역할
    ID3D11VertexShader* pVertexShader = nullptr;                       // 실제로 사용할 셰이더
    ID3D11PixelShader* pPixelShader = nullptr;
    ID3D11InputLayout* pVertexLayout = nullptr;                          // GPU에게 전달하는 데이터 해석법 가이드 라인 (실제 내용물 데이터 해석법을 담을 객체의 포인터)
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

        // 장치와 스왑체인 생성, 이 시점에서 백버퍼와 프론트 버퍼 공간이 실제로 생성
        D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, nullptr, &g_pImmediateContext);

        // 렌더 타겟 설정 (도화지 준비)
        ID3D11Texture2D* pBackBuffer = nullptr; // 만들어진 백버퍼의 주소를 담을 변수 (포인터변수)
        g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer); // 스왑체인에 있는 백버퍼의 주소를 포인터로 가져옴
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRenderTargetView); // 타겟뷰를 생성 (백버퍼와 같은 주소를 가리키고 있지만 타겟뷰는 시선이 다름 (안경이 다르다))
        // pBackBuffer는 데이터를 그냥 통덩어리로 봤다면, 타겟뷰는 안경을 끼고 파트별로 분류해서 바라보는중
        // 그렇다면 왜 pBackBuffer를 만들어야 하는가? -> DirectX의 규칙 때문.
        // 뷰를 만들기 위해서는 생 데이터 정보가 있어야 만들 수 있도록 규칙을 정해둠.
        // 도화지의 규격같은것을 정의, 메모리 영역
        pBackBuffer->Release(); // 뷰를 생성했으므로 원본 텍스트는 바로 해제 (참조 횟수가 2회이므로 실제 객체가 사라지는것이 아님)

        ID3DBlob* vsBlob, * psBlob; // 기계어를 임시로 담을 변수
        D3DCompile(shaderSource, strlen(shaderSource), nullptr, nullptr, nullptr, "VS", "vs_4_0", 0, 0, &vsBlob, nullptr); // 텍스트 소스를 읽어 VS라는 함수를 Vertex Shader용으로 번역
        D3DCompile(shaderSource, strlen(shaderSource), nullptr, nullptr, nullptr, "PS", "ps_4_0", 0, 0, &psBlob, nullptr); // 텍스트 소스를 읽어 PS라는 함수를 Pixel Shader용으로 번역

        g_pd3dDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &pVertexShader); // 임시 변수에서부터 실제 GPU프로그램 객체로 값 대입
        g_pd3dDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &pPixelShader);

        // 정점의 데이터 형식을 정의 (데이터 해석법 : Input Layout) (입력 데이터의 내용물을 정의 / 기획서)
        D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        g_pd3dDevice->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &pVertexLayout);
        vsBlob->Release(); psBlob->Release(); // 컴파일용 임시 메모리 해제
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
    std::string name;   // 해당 오브젝트의 이름
    std::vector<Component*> components; // 해당 오브젝트가 가질 컴포넌트들

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
        pComp->pOwner = this;   // 컴포넌트의 소유자를 오브젝트로 설정
        pComp->isStarted = false;
        components.push_back(pComp);
    }
};

class L_Triangle : public Component {
public:
    std::vector<Vertex> vertices;   // 점 정보를 담을 벡터
    float speed;
    bool isUp, isDown, isLeft, isRight; // 각 키가 입력이 되었는지 확인 할 Flag
    ID3D11Buffer* pVertexBuffer;    // 정점 버퍼, 배열 데이터를 GPU가 읽을 수 있도록 VRAM에 만든 복사본 상자 (GPU 파트)

    // 초기값 설정 (Start)
    void Start(DirectSet* cdx) override {
        vertices = {
            { -0.402f,  0.16f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f },
            { -0.3f, -0.08f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f },
            { -0.504f, -0.08f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f },
        };

        speed = 0.0003f;
        isUp = isDown = isLeft = isRight = 0;

        D3D11_BUFFER_DESC bd = { sizeof(Vertex) * (UINT)vertices.size(), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
        D3D11_SUBRESOURCE_DATA initData = { &vertices[0], 0, 0};    // 정점 데이터 넣는 부분 (CPU 파트)
        cdx->g_pd3dDevice->CreateBuffer(&bd, &initData, &pVertexBuffer); // 실제 버퍼 생성 (형식, 데이터, 결과물)
    }

    // 입력값 설정 (Input)
    void Input() override {
        isUp = (GetAsyncKeyState('W') & 0x8000);     // 순회하며 해당 키가 눌렸다면 True를, 해당 키가 때졌다면 False
        isDown = (GetAsyncKeyState('S') & 0x8000); // 0x8000을 넣는 이유 ? -> GetAsyncKeyState는 16비트를 리턴. .
        isLeft = (GetAsyncKeyState('A') & 0x8000);    // 그 중 15번째(최상위) 비트가 키가 눌려있는 상태인지를 나타냄
        isRight = (GetAsyncKeyState('D') & 0x8000);  //  0번째 비트는 아까 눌린적이 있는지 체크하는 비트인데 입력하지 않았을때 이동을 방지하기위해 사용

        /*
    * 해당 방식으로 했더니 해당 키를 땠을때를 인식을 하지 못하여 수정
    * Start는 초기에만 실행되므로 0으로 돌리는 방법이 해당 방식에는 없음
    if (GetAsyncKeyState('W') & 0x8000) isUp = 1;
    if (GetAsyncKeyState('S') & 0x8000) isDown = 1;
    if (GetAsyncKeyState('A') & 0x8000) isLeft = 1;
    if (GetAsyncKeyState('D') & 0x8000) isRight = 1;
    */
    }

    // 업데이트 (Update)
    void Update(float dt) override {  // 입력 감지 후 실제 정점 벡터 값 수정
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

    // 버퍼에 그리기 (Render)
    void Render(DirectSet* cdx) {
        cdx->g_pImmediateContext->UpdateSubresource(pVertexBuffer, 0, nullptr, &vertices[0], 0, 0); // 맴버변수인 정점 값을 VRAM에 전달

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
        pVertexBuffer->Release(); // 해당 객체가 사라질때는 VRAM에 정점값 복사본이 필요없으므로 해제
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
            { 0.402f, 0.16f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f },
            {  0.504f, -0.08f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f },
            {  0.3f, -0.08f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f },
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

        cdx->g_pImmediateContext->IASetInputLayout(cdx->pVertexLayout);
        UINT stride = sizeof(Vertex), offset = 0;
        cdx->g_pImmediateContext->IASetVertexBuffers(0, 1, &pVertexBuffer, &stride, &offset);

        cdx->g_pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cdx->g_pImmediateContext->VSSetShader(cdx->pVertexShader, nullptr, 0);
        cdx->g_pImmediateContext->PSSetShader(cdx->pPixelShader, nullptr, 0);

        cdx->g_pImmediateContext->Draw(3, 0);
    }

    ~R_Triangle() {
        pVertexBuffer->Release();
    }
};

// 게임 루프
class GameLoop {
public:
    std::vector<GameObject*> gameWorld; // 오브젝트를 담을 백터 (Scene 개념)
    std::chrono::high_resolution_clock::time_point prevTime; // 이전 프레임의 실행된 시점 저장
    float deltaTime;    // 이전 프레임에서 현재 프레임까지 걸린 시간
    DirectSet* cdx;

    GameLoop(DirectSet* cdx) {
        this->cdx = cdx;
        Initialize();
    }

    void Check_Started(DirectSet* cdx) {    // 중간에 객체가 추가되는경우 대비 & 첫프레임 씹힘 현상 방지를 위해 함수를 만듦.
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
    }

    void Initialize()
    {
        gameWorld.clear();

        prevTime = std::chrono::high_resolution_clock::now(); // 초기에 prevTime을 현재 시간으로 설정
        deltaTime = 0.0f;
    }

    // 입력값 수정
    void Input() {
        Check_Started(cdx); // 입력값이 들어오기전에 컴포넌트들을 수정
        for (int i = 0; i < (int)gameWorld.size(); i++)
        {
            for (int j = 0; j < (int)gameWorld[i]->components.size(); j++)
            {
                gameWorld[i]->components[j]->Input();
            }
        }
    }

    // 값 업데이트
    void Update() {
        /*
        // 초기화 부분을 왜 Update에 넣었나? -> 중간에 객체가 추가되는경우 때문에
        // -> 하지만 이러면 첫프레임에서의 입력이 씹힐 수 있기 때문에 수정
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
        */
        for (int i = 0; i < (int)gameWorld.size(); i++)
        {
            for (int j = 0; j < (int)gameWorld[i]->components.size(); j++)
            {
                gameWorld[i]->components[j]->Update(deltaTime);
            }
        }
    }

    void Render() {
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

    void Run() { //메시지 드리븐
        MSG msg = { 0 };
        while (WM_QUIT != msg.message) {
            if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { // PM_REMOVE는 메시지 받고나서 큐에서 삭제
                TranslateMessage(&msg); // 키보드 입력을 문자로 변환
                DispatchMessage(&msg); // 위에서 만든 WndProc 함수로 메세지 전달
            }
            else {
                std::chrono::high_resolution_clock::time_point currentTime = std::chrono::high_resolution_clock::now(); // 현재 시간 설정
                std::chrono::duration<float> elapsed = currentTime - prevTime; // 경과 시간 계산
                deltaTime = elapsed.count(); // 틱 횟수를 float로 반환
                prevTime = currentTime;

                Input();
                Update();
                Render();

                std::this_thread::sleep_for(std::chrono::milliseconds(10)); // 입력 지연을 통해서 프레임 제한
            }
        }
    }

    ~GameLoop() {
        for (int i = 0; i < (int)gameWorld.size(); i++)
        {
            delete gameWorld[i]; // GameObject 소멸자가 컴포넌트들도 지움
        }
    }
};

// 프로세스 진입점
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
    GameLoop gLoop(&cdx);
    // gLoop.Initialize(); // 이미 생성자에서 실행

    GameObject* tri_1 = new GameObject("Triangle_1");
    tri_1->AddComponent(new L_Triangle());
    gLoop.gameWorld.push_back(tri_1);

    GameObject* tri_2 = new GameObject("Triangle_2");
    tri_2->AddComponent(new R_Triangle());
    gLoop.gameWorld.push_back(tri_2);

    gLoop.Run();

    return 0;
}