/*++

// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.

Copyright (C) Microsoft Corporation, All Rights Reserved

Module Name:

    Device.h

Abstract:

    This module contains the type definitions for the SPB accelerometer's
    device callback class.

--*/

#ifndef _DEVICE_H_
#define _DEVICE_H_

#pragma once

class CSensorDdi;

class CMyDevice : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,IPnpCallback,IPnpCallbackHardware2,IFileCallbackCleanup>
{
public:
    ~CMyDevice();
    CMyDevice();

    HRESULT RuntimeClassInitialize(
        _In_  IWDFDriver*              pDriver,
        _In_  IWDFDeviceInitialize*    pDeviceInit);

// COM Interface methods
public:

    // IPnpCallback
    STDMETHOD  (OnD0Entry)(
        _In_ IWDFDevice* pWdfDevice, 
        _In_ WDF_POWER_DEVICE_STATE previousState);
    STDMETHOD  (OnD0Exit)(
        _In_ IWDFDevice* pWdfDevice, 
        _In_ WDF_POWER_DEVICE_STATE newState);
    STDMETHOD_ (VOID, OnSurpriseRemoval)(_In_ IWDFDevice*) { return; }
    STDMETHOD_ (HRESULT, OnQueryRemove)(_In_ IWDFDevice*)  { return S_OK; }
    STDMETHOD_ (HRESULT, OnQueryStop)(_In_ IWDFDevice*)    { return S_OK; }

    // IPnpCallbackHardware2
    STDMETHOD_ (HRESULT, OnPrepareHardware)(
        _In_ IWDFDevice3 * pWdfDevice,
        _In_ IWDFCmResourceList * pWdfResourcesRaw,
        _In_ IWDFCmResourceList * pWdfResourcesTranslated);
    STDMETHOD_ (HRESULT, OnReleaseHardware)(
        _In_ IWDFDevice3 * pWdfDevice,
        _In_ IWDFCmResourceList * pWdfResourcesTranslated);

    // IFileCallbackCleanup
    STDMETHOD_ (VOID, OnCleanupFile)(_In_ IWDFFile *pWdfFile);

public:
    
    // The factory method used to create an instance of this device
    static HRESULT CreateInstance(
        _In_                           IWDFDriver*              pDriver,
        _In_                           IWDFDeviceInitialize*    pDeviceInit,
        _COM_Outptr_result_maybenull_  CMyDevice**  ppMyDevice);

    HRESULT Configure();

    HRESULT ProcessIoControl(
        _In_ IWDFIoQueue*     pQueue,
        _In_ IWDFIoRequest*   pRequest,
        _In_ ULONG            ControlCode,
             SIZE_T           InputBufferSizeInBytes,
             SIZE_T           OutputBufferSizeInBytes,
             DWORD*           pcbWritten);

private:
    // Interface pointers
    Microsoft::WRL::ComPtr<IWDFDevice>                            m_spWdfDevice;
    Microsoft::WRL::ComPtr<ISensorClassExtension>                 m_spClassExtension;

    // Class extension pointer					                  
    Microsoft::WRL::ComPtr<CSensorDdi>                            m_pSensorDdi;
};

#endif // _DEVICE_H_
