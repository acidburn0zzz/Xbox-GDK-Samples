//--------------------------------------------------------------------------------------
// DeviceManager.cpp
//
// Wraps the underlying IMMNotificationClient to manage capture devices
//
// Advanced Technology Group (ATG)
// Copyright (C) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

#include "pch.h"
#include "DeviceManager.h"

using namespace Microsoft::WRL;

DeviceManager::DeviceManager() :
        _cRef(1),
        m_CaptureIndex( 0 ),
		m_deviceChangeFunc( nullptr )
{
    ComPtr<IMMDeviceCollection>   deviceCollectionInterface;

    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&m_deviceEnum);

    if (!InitializeCriticalSectionEx( &m_CritSec, 0, 0 ))
    {
		return;
    }
            
    UpdateCaptureDeviceList();
}

DeviceManager::~DeviceManager()
{
    DeleteCriticalSection( &m_CritSec );
}

HRESULT STDMETHODCALLTYPE DeviceManager::OnDefaultDeviceChanged(
                                EDataFlow flow, ERole role,
                                LPCWSTR pwstrDeviceId)
{
    UNREFERENCED_PARAMETER(flow);
    UNREFERENCED_PARAMETER(role);
    UNREFERENCED_PARAMETER(pwstrDeviceId);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DeviceManager::OnDeviceAdded(LPCWSTR pwstrDeviceId)
{
    //Occurs when we plug in a new device
    UNREFERENCED_PARAMETER(pwstrDeviceId);
    UpdateCaptureDeviceList();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DeviceManager::OnDeviceRemoved(LPCWSTR pwstrDeviceId)
{
    //Occurs when we unplug a device
    UNREFERENCED_PARAMETER(pwstrDeviceId);
    UpdateCaptureDeviceList();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DeviceManager::OnDeviceStateChanged(
                            LPCWSTR pwstrDeviceId,
                            DWORD dwNewState)
{
    UNREFERENCED_PARAMETER(pwstrDeviceId);
    UNREFERENCED_PARAMETER(dwNewState);
    UpdateCaptureDeviceList();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE DeviceManager::OnPropertyValueChanged(
                            LPCWSTR pwstrDeviceId,
                            const PROPERTYKEY key)
{
    UNREFERENCED_PARAMETER(pwstrDeviceId);
    UNREFERENCED_PARAMETER(key);
    return S_OK;
}

        
//--------------------------------------------------------------------------------------
//  Name:   UpdateCaptureDeviceList
//  Desc:   Updates the list of capture devices
//--------------------------------------------------------------------------------------
void DeviceManager::UpdateCaptureDeviceList()
{
    EnterCriticalSection( &m_CritSec );
    ComPtr<IMMDeviceCollection>   deviceCollectionInterface;
    HRESULT hr = S_OK;
    std::wstring currentID = L"";
            
    //Save the ID of current capture device incase its index changes
    if(m_CaptureDevices.size() > 0)
    {
        currentID = m_CaptureDevices.at(size_t(m_CaptureIndex));
    }

    if (m_deviceEnum)
    {
        hr = m_deviceEnum->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &deviceCollectionInterface);
            
        if (SUCCEEDED( hr ))
        {
            ComPtr<IMMDevice>   deviceInterface;
            UINT iDeviceCount = 0;

            m_CaptureDevices.clear();

            hr = deviceCollectionInterface->GetCount(&iDeviceCount);
                
            for(UINT i=0; SUCCEEDED( hr ) && i<iDeviceCount; i++)
            {
                //Go through each device and save the ID
                hr = deviceCollectionInterface->Item(i, &deviceInterface);
                    
                if (SUCCEEDED( hr ))
                {
                    LPWSTR deviceId;
                    deviceInterface->GetId(&deviceId);
                    m_CaptureDevices.push_back(deviceId);

                    if(currentID.compare(deviceId) == 0)
                    {
                        //Make sure index is correct based on the ID string
                        m_CaptureIndex = static_cast<int>(i);
                    }
                }
            }

            if(m_deviceChangeFunc)
            {
                //Report back that a change has been made to the device list
                m_deviceChangeFunc(m_CaptureIndex);
            }
        }
    }

    LeaveCriticalSection( &m_CritSec );
}
