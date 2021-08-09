//--------------------------------------------------------------------------------------
// SimpleCloudAwareSample.cpp
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#include "pch.h"
#include "SimpleCloudAwareSample.h"
#include <inttypes.h>

#include "ATGColors.h"
#include "ControllerFont.h"

extern void ExitSample() noexcept;

using namespace DirectX;
using namespace DirectX::SimpleMath;

using Microsoft::WRL::ComPtr;

static void CALLBACK ConnectionStateChangedCallback(
    void* context,
    XGameStreamingClientId client,
    XGameStreamingConnectionState state) noexcept
{
    auto sample = reinterpret_cast<Sample*>(context);

    if (state == XGameStreamingConnectionState::Connected)
    {
        for (size_t i = 0; i < c_maxClients; ++i)
        {
            if (sample->m_clients[i].id == XGameStreamingNullClientId)
            {
                sample->m_clients[i].id = client;

                //Check to see if this client has a small display
                uint32_t clientWidthMm = 0;
                uint32_t clientHeightMm = 0;
                if (SUCCEEDED(XGameStreamingGetStreamPhysicalDimensions(sample->m_clients[i].id, &clientWidthMm, &clientHeightMm)))
                {
                    if (clientWidthMm * clientHeightMm < 13000)
                    {
                        sample->m_clients[i].smallScreen = true;
                    }
                }

                //The sample TAK is 1.0, so ensure that the client has the overlay
                XVersion overlayVersion;
                if (SUCCEEDED(XGameStreamingGetTouchBundleVersion(sample->m_clients[i].id, &overlayVersion, 0, nullptr)))
                {
                    if (overlayVersion.major == 1 && overlayVersion.minor == 0)
                    {
                        sample->m_clients[i].validOverlay = true;
                    }
                }

                break;
            }
        }
    }
    else
    {
        for (size_t i = 0; i < c_maxClients; ++i)
        {
            if (sample->m_clients[i].id == client)
            {
                sample->m_clients[i] = ClientDevice();
            }
        }
    }

    sample->UpdateClientState();
}

Sample::Sample() noexcept(false) :
    m_frame(0),
    m_leftTrigger(0),
    m_rightTrigger(0),
    m_leftStickX(0),
    m_leftStickY(0),
    m_rightStickX(0),
    m_rightStickY(0),
    m_overlayDown(false),
    m_currentOverlay(CurrentOverlay::standard_controller),
    m_streaming(false),
    m_validOverlay(false)
{
    // Renders only 2D, so no need for a depth buffer.
    m_deviceResources = std::make_unique<DX::DeviceResources>(DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_UNKNOWN);
}

Sample::~Sample()
{
    XGameStreamingUnregisterConnectionStateChanged(m_token, false);
    XGameStreamingUninitialize();
}

// Initialize the Direct3D resources required to run.
void Sample::Initialize(HWND window)
{
    m_deviceResources->SetWindow(window);

    m_deviceResources->CreateDeviceResources();  	
    CreateDeviceDependentResources();

    m_deviceResources->CreateWindowSizeDependentResources();
    CreateWindowSizeDependentResources();

    DX::ThrowIfFailed(GameInputCreate(&m_gameInput));

    m_font = m_localFont.get();

    DX::ThrowIfFailed(XTaskQueueCreate(XTaskQueueDispatchMode::Immediate, XTaskQueueDispatchMode::Immediate, &m_queue));

    DX::ThrowIfFailed(XGameStreamingInitialize());
    DX::ThrowIfFailed(XGameStreamingRegisterConnectionStateChanged(m_queue, this, ConnectionStateChangedCallback, &m_token));
    XGameStreamingShowTouchControlLayout("standard-controller");
}

#pragma region Frame Update
// Executes basic render loop.
void Sample::Tick()
{
    PIXBeginEvent(PIX_COLOR_DEFAULT, L"Frame %I64u", m_frame);

    m_timer.Tick([&]()
    {
        Update(m_timer);
    });

    Render();

    PIXEndEvent();
    m_frame++;
}

// Updates the world.
void Sample::Update(DX::StepTimer const&)
{
    PIXScopedEvent(PIX_COLOR_DEFAULT, L"Update");

    if (XGameStreamingIsStreaming() != m_streaming)
    {
        //The streaming state has changed
        m_streaming = !m_streaming;
    }

    //Get only gamepad and touch input types
    HRESULT hr = m_gameInput->GetCurrentReading(GameInputKindGamepad | GameInputKindTouch, nullptr, &m_reading);

    m_buttonString = L"Buttons pressed:  ";

    if (SUCCEEDED(hr))
    {
        uint32_t inputCount;
        uint32_t readCount;

        GameInputGamepadState gamepadState;

        if (m_reading->GetGamepadState(&gamepadState))
        {
            XGameStreamingGamepadPhysicality gamepadPhysicality;
            m_typeString = L"";

            //Find out if the input is from a remote source or not
            if (SUCCEEDED(XGameStreamingGetGamepadPhysicality(m_reading.Get(), &gamepadPhysicality)))
            {
                if (gamepadPhysicality == XGameStreamingGamepadPhysicality::None)
                {
                    IGameInputDevice* device;
                    m_reading->GetDevice(&device);

                    //Ignore local, virtual devices
                    if (device->GetDeviceInfo()->deviceFamily != GameInputDeviceFamily::GameInputFamilyVirtual)
                    {
                        m_typeString = L"Type: Local";
                    }
                }
                else if ((gamepadPhysicality & XGameStreamingGamepadPhysicality::AllPhysical) != XGameStreamingGamepadPhysicality::None)
                {
                    m_typeString = L"Type: Cloud Physical";
                }
                else if ((gamepadPhysicality & XGameStreamingGamepadPhysicality::AllVirtual) != XGameStreamingGamepadPhysicality::None)
                {
                    m_typeString = L"Type: Cloud Virtual";
                }
            }

            int newOverlay = m_currentOverlay;

            if (gamepadState.buttons & GameInputGamepadDPadUp)
            {
                m_buttonString += L"[DPad]Up ";
            }

            if (gamepadState.buttons & GameInputGamepadDPadDown)
            {
                m_buttonString += L"[DPad]Down ";
            }

            if (gamepadState.buttons & GameInputGamepadDPadRight)
            {
                m_buttonString += L"[DPad]Right ";
            }

            if (gamepadState.buttons & GameInputGamepadDPadLeft)
            {
                m_buttonString += L"[DPad]Left ";
            }

            if (gamepadState.buttons & GameInputGamepadA)
            {
                m_buttonString += L"[A] ";
                if (m_streaming)
                {
                    newOverlay++;
                }
            }

            if (gamepadState.buttons & GameInputGamepadB)
            {
                m_buttonString += L"[B] ";
            }

            if (gamepadState.buttons & GameInputGamepadX)
            {
                m_buttonString += L"[X] ";
            }

            if (gamepadState.buttons & GameInputGamepadY)
            {
                m_buttonString += L"[Y] ";
            }

            if (gamepadState.buttons & GameInputGamepadLeftShoulder)
            {
                m_buttonString += L"[LB] ";
            }

            if (gamepadState.buttons & GameInputGamepadRightShoulder)
            {
                m_buttonString += L"[RB] ";
            }

            if (gamepadState.buttons & GameInputGamepadLeftThumbstick)
            {
                m_buttonString += L"[LThumb] ";
            }

            if (gamepadState.buttons & GameInputGamepadRightThumbstick)
            {
                m_buttonString += L"[RThumb] ";
            }

            if (gamepadState.buttons & GameInputGamepadMenu)
            {
                m_buttonString += L"[Menu] ";
            }

            if (gamepadState.buttons & GameInputGamepadView)
            {
                m_buttonString += L"[View] ";
            }

            if (m_currentOverlay == CurrentOverlay::Off && m_resetTime <= m_timer.GetTotalSeconds())
            {
                newOverlay = CurrentOverlay::standard_controller;
                m_overlayDown = false;
            }

            if (newOverlay != m_currentOverlay && !m_overlayDown)
            {
                m_overlayDown = true;

                if (newOverlay > CurrentOverlay::Off)
                {
                    m_currentOverlay = CurrentOverlay::standard_controller;
                }
                else
                {
                    m_currentOverlay = (CurrentOverlay)newOverlay;
                }

                switch (m_currentOverlay)
                {
                case CurrentOverlay::standard_controller:
                    XGameStreamingShowTouchControlLayout("standard-controller");
                    break;
                case CurrentOverlay::driving:
                    XGameStreamingShowTouchControlLayout("driving");
                    break;
                case CurrentOverlay::fighting:
                    XGameStreamingShowTouchControlLayout("fighting");
                    break;
                case CurrentOverlay::first_person_shooter:
                    XGameStreamingShowTouchControlLayout("first-person-shooter");
                    break;
                case CurrentOverlay::platformer_simple:
                    XGameStreamingShowTouchControlLayout("platformer-simple");
                    break;
                case CurrentOverlay::platformer:
                    XGameStreamingShowTouchControlLayout("platformer");
                    break;
                case CurrentOverlay::Off:
                    XGameStreamingHideTouchControls();
                    m_resetTime = m_timer.GetTotalSeconds() + 5.f;
                    break;
                }
            }
            else if (newOverlay == m_currentOverlay && m_overlayDown)
            {
                m_overlayDown = false;
            }

            m_leftTrigger = gamepadState.leftTrigger;
            m_rightTrigger = gamepadState.rightTrigger;
            m_leftStickX = gamepadState.leftThumbstickX;
            m_leftStickY = gamepadState.leftThumbstickY;
            m_rightStickX = gamepadState.rightThumbstickX;
            m_rightStickY = gamepadState.rightThumbstickY;
        }
        else
        {
            m_buttonString.clear();
        }

        m_touchPoints.clear();
        inputCount = m_reading->GetTouchCount();
        if (inputCount > 0)
        {
            auto touchReading = std::make_unique<GameInputTouchState[]>(inputCount);
            readCount = m_reading->GetTouchState(inputCount, touchReading.get());

            {
                std::lock_guard<std::mutex> lock(m_touchListLock);
                for (uint32_t i = 0; i < inputCount; i++)
                {
                    m_touchPoints.insert(std::pair<uint64_t, XMFLOAT2>(touchReading[i].touchId, XMFLOAT2(touchReading[i].positionX, touchReading[i].positionY)));
                }
            }
        }
    }
}
#pragma endregion

#pragma region Frame Render
// Draws the scene.
void Sample::Render()
{
    // Don't try to render anything before the first Update.
    if (m_timer.GetFrameCount() == 0)
    {
        return;
    }

    // Prepare the command list to render a new frame.
    m_deviceResources->Prepare();
    Clear();

    auto commandList = m_deviceResources->GetCommandList();
    PIXBeginEvent(commandList, PIX_COLOR_DEFAULT, L"Render");

    auto fullscreen = m_deviceResources->GetOutputSize();

    auto safeRect = Viewport::ComputeTitleSafeArea(UINT(fullscreen.right - fullscreen.left), UINT(fullscreen.bottom - fullscreen.top));

    auto heap = m_resourceDescriptors->Heap();
    commandList->SetDescriptorHeaps(1, &heap);

    m_batch->Begin(commandList);

    m_batch->Draw(m_resourceDescriptors->GetGpuHandle(Descriptors::Background), XMUINT2(1920, 1080), fullscreen);

    wchar_t tempString[256] = {};
    XMFLOAT2 pos(float(safeRect.left), float(safeRect.top));

    if (!m_buttonString.empty())
    {
        DX::DrawControllerString(m_batch.get(), m_font, m_ctrlFont.get(), m_buttonString.c_str(), pos);
        pos.y += m_font->GetLineSpacing() * 1.5f;

        if (!m_streaming)
        {
            m_font->DrawString(m_batch.get(), L"Game is not currently being streamed", pos, ATG::Colors::Orange);
        }
        else if (!m_validOverlay)
        {
            m_font->DrawString(m_batch.get(), L"The overlay is not loaded on a streaming client. Please view the Readme.", pos, ATG::Colors::Orange);
        }
        else
        {
            switch (m_currentOverlay)
            {
            case CurrentOverlay::standard_controller:
                m_font->DrawString(m_batch.get(), L"Overlay: Standard Controller", pos, ATG::Colors::White);
                break;
            case CurrentOverlay::driving:
                m_font->DrawString(m_batch.get(), L"Overlay: Driving", pos, ATG::Colors::White);
                break;
            case CurrentOverlay::fighting:
                m_font->DrawString(m_batch.get(), L"Overlay: Fighting", pos, ATG::Colors::White);
                break;
            case CurrentOverlay::first_person_shooter:
                m_font->DrawString(m_batch.get(), L"Overlay: First Person Shooter", pos, ATG::Colors::White);
                break;
            case CurrentOverlay::platformer_simple:
                m_font->DrawString(m_batch.get(), L"Overlay: Platformer Simple", pos, ATG::Colors::White);
                break;
            case CurrentOverlay::platformer:
                m_font->DrawString(m_batch.get(), L"Overlay: Platformer", pos, ATG::Colors::White);
                break;
            case CurrentOverlay::Off:
                m_font->DrawString(m_batch.get(), L"Overlay: Off", pos, ATG::Colors::White);
                break;
            }
        }

        if (!m_typeString.empty())
        {
            pos.y += m_font->GetLineSpacing() * 1.5f;
            m_font->DrawString(m_batch.get(), m_typeString.c_str(), pos, ATG::Colors::White);
        }

        pos.y += m_font->GetLineSpacing() * 1.5f;

        swprintf(tempString, 255, L"[LT]  %1.3f", m_leftTrigger);
        DX::DrawControllerString(m_batch.get(), m_font, m_ctrlFont.get(), tempString, pos);
        pos.y += m_font->GetLineSpacing() * 1.5f;

        swprintf(tempString, 255, L"[RT]  %1.3f", m_rightTrigger);
        DX::DrawControllerString(m_batch.get(), m_font, m_ctrlFont.get(), tempString, pos);
        pos.y += m_font->GetLineSpacing() * 1.5f;

        swprintf(tempString, 255, L"[LThumb]  X: %1.3f  Y: %1.3f", m_leftStickX, m_leftStickY);
        DX::DrawControllerString(m_batch.get(), m_font, m_ctrlFont.get(), tempString, pos);
        pos.y += m_font->GetLineSpacing() * 1.5f;

        swprintf(tempString, 255, L"[RThumb]  X: %1.3f  Y: %1.3f", m_rightStickX, m_rightStickY);
        DX::DrawControllerString(m_batch.get(), m_font, m_ctrlFont.get(), tempString, pos);
        pos.y += m_font->GetLineSpacing() * 1.5f;

        if (m_currentOverlay == CurrentOverlay::Off && m_resetTime > m_timer.GetTotalSeconds())
        {
            swprintf(tempString, 255, L"Overlay returning in %1.3f seconds", m_resetTime - m_timer.GetTotalSeconds());
            DX::DrawControllerString(m_batch.get(), m_font, m_ctrlFont.get(), tempString, pos);
            pos.y += m_font->GetLineSpacing() * 1.5f;
        }

        {
            std::lock_guard<std::mutex> lock(m_touchListLock);
            for (auto touchPoint : m_touchPoints)
            {
				DrawTouch(touchPoint.first, touchPoint.second.x, touchPoint.second.y);
            }
        }
    }
    else
    {
        m_font->DrawString(m_batch.get(), L"No client or controller connected", pos, ATG::Colors::Orange);
    }

    if (m_streaming)
    {
        DX::DrawControllerString(m_batch.get(),
            m_font, m_ctrlFont.get(),
            L"[A]: Select Overlay",
            XMFLOAT2(float(safeRect.left), float(safeRect.bottom) - m_font->GetLineSpacing()),
            ATG::Colors::LightGrey);
    }

    m_batch->End();

    PIXEndEvent(commandList);

    // Show the new frame.
    PIXBeginEvent(PIX_COLOR_DEFAULT, L"Present");
    m_deviceResources->Present();
    m_graphicsMemory->Commit(m_deviceResources->GetCommandQueue());
    PIXEndEvent();
}

// Helper method to clear the back buffers.
void Sample::Clear()
{
    auto commandList = m_deviceResources->GetCommandList();
    PIXBeginEvent(commandList, PIX_COLOR_DEFAULT, L"Clear");

    // Clear the views.
    auto rtvDescriptor = m_deviceResources->GetRenderTargetView();

    commandList->OMSetRenderTargets(1, &rtvDescriptor, FALSE, nullptr);
    commandList->ClearRenderTargetView(rtvDescriptor, ATG::Colors::Background, 0, nullptr);

    // Set the viewport and scissor rect.
    auto viewport = m_deviceResources->GetScreenViewport();
    auto scissorRect = m_deviceResources->GetScissorRect();
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissorRect);

    PIXEndEvent(commandList);
}
#pragma endregion

#pragma region Message Handlers
// Message handlers
void Sample::OnSuspending()
{
    m_deviceResources->Suspend();
}

void Sample::OnResuming()
{
    m_deviceResources->Resume();
    m_timer.ResetElapsedTime();
}
#pragma endregion

#pragma region Direct3D Resources
// These are the resources that depend on the device.
void Sample::CreateDeviceDependentResources()
{
    auto device = m_deviceResources->GetD3DDevice();

    m_graphicsMemory = std::make_unique<GraphicsMemory>(device);

    m_resourceDescriptors = std::make_unique<DescriptorHeap>(device,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, Descriptors::Count);

    RenderTargetState rtState(m_deviceResources->GetBackBufferFormat(), m_deviceResources->GetDepthBufferFormat());

    ResourceUploadBatch upload(device);
    upload.Begin();

    {
        SpriteBatchPipelineStateDescription pd(
            rtState,
            &CommonStates::AlphaBlend);

        m_batch = std::make_unique<SpriteBatch>(device, upload, pd);
    }

    m_localFont = std::make_unique<SpriteFont>(device, upload,
        L"SegoeUI_24.spritefont",
        m_resourceDescriptors->GetCpuHandle(Descriptors::PrintLocalFont),
        m_resourceDescriptors->GetGpuHandle(Descriptors::PrintLocalFont));

    m_remoteFont = std::make_unique<SpriteFont>(device, upload,
        L"SegoeUI_36.spritefont",
        m_resourceDescriptors->GetCpuHandle(Descriptors::PrintRemoteFont),
        m_resourceDescriptors->GetGpuHandle(Descriptors::PrintRemoteFont));

    m_ctrlFont = std::make_unique<SpriteFont>(device, upload,
        L"XboxOneControllerLegendSmall.spritefont",
        m_resourceDescriptors->GetCpuHandle(Descriptors::ControllerFont),
        m_resourceDescriptors->GetGpuHandle(Descriptors::ControllerFont));

    DX::ThrowIfFailed(CreateDDSTextureFromFile(device, upload, L"callout_circle.dds", m_circleTexture.ReleaseAndGetAddressOf()));
    DX::ThrowIfFailed(CreateDDSTextureFromFile(device, upload, L"ATGSampleBackground.DDS", m_background.ReleaseAndGetAddressOf()));

    auto finish = upload.End(m_deviceResources->GetCommandQueue());
    finish.wait();

    m_deviceResources->WaitForGpu();

    CreateShaderResourceView(device, m_circleTexture.Get(), m_resourceDescriptors->GetCpuHandle(Descriptors::Touch));
    CreateShaderResourceView(device, m_background.Get(), m_resourceDescriptors->GetCpuHandle(Descriptors::Background));
}

// Allocate all memory resources that change on a window SizeChanged event.
void Sample::CreateWindowSizeDependentResources()
{
    auto vp = m_deviceResources->GetScreenViewport();
    m_batch->SetViewport(vp);
}

//--------------------------------------------------------------------------------------
// Name: UpdateClientState()
// Desc: Updates overlay and screen size state for clients
//--------------------------------------------------------------------------------------
void Sample::UpdateClientState()
{
    m_font = m_localFont.get();
    m_validOverlay = true;

    for (int i = 0; i < c_maxClients; i++)
    {
        if (m_clients[i].id != XGameStreamingNullClientId)
        {
            if (m_clients[i].smallScreen)
            {
                m_font = m_remoteFont.get();
            }

            if (!m_clients[i].validOverlay)
            {
                m_validOverlay = false;
            }
        }
    }
}

//--------------------------------------------------------------------------------------
// Name: DrawTouch()
// Desc: Draws a touch point
//--------------------------------------------------------------------------------------
void XM_CALLCONV Sample::DrawTouch(uint64_t id, float x, float y)
{
    LONG localX = LONG(x * 1920.f);
    LONG localY = LONG(y * 1080.f);

    RECT drawspace;
	drawspace.top = localY - TOUCHSIZE;
	drawspace.bottom = localY + TOUCHSIZE;
	drawspace.left = localX - TOUCHSIZE;
	drawspace.right = localX + TOUCHSIZE;
    
    auto circleSize = GetTextureSize(m_circleTexture.Get());
    m_batch->Draw(m_resourceDescriptors->GetGpuHandle(Descriptors::Touch), circleSize, drawspace, ATG::Colors::Blue);

    XMFLOAT2 position = XMFLOAT2((float)localX, (float)localY);
	wchar_t tempString[256] = {};
	
	swprintf(tempString, 255, L"X: %4.3f", position.x);
	m_font->DrawString(m_batch.get(), tempString, position, ATG::Colors::White);
    position.y += m_font->GetLineSpacing();
	swprintf(tempString, 255, L"Y: %4.3f", position.y);
	m_font->DrawString(m_batch.get(), tempString, position, ATG::Colors::White);
    position.y += m_font->GetLineSpacing();
	swprintf(tempString, 255, L"Id: %llu", id);
	m_font->DrawString(m_batch.get(), tempString, position, ATG::Colors::White);
}
#pragma endregion