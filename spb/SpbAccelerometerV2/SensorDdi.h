/*++

// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.

Copyright (c) Microsoft Corporation. All rights reserved

Module Name:

    SensorDdi.h

Abstract:

    This module contains the type definitions for the ISensorDriver
    interface which is used by the Sensor Class Extension.

--*/

#ifndef _SENSORDDI_H_
#define _SENSORDDI_H_

#pragma once

// Forward declarations
class CSensorDevice;


class CSensorDdi : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, ISensorDriver>
{
public:
    CSensorDdi();
    virtual ~CSensorDdi();

// Public methods
public:
    HRESULT RuntimeClassInitialize(
        _In_ IWDFDevice* pWdfDevice,
        _In_ IWDFCmResourceList * pWdfResourcesRaw,
        _In_ IWDFCmResourceList * pWdfResourcesTranslated);
    VOID Uninitialize();
    HRESULT SetSensorClassExtension(
#pragma warning(suppress : 4995) // suppress deprecated message
        _In_ ISensorClassExtension* pClassExtension);
    HRESULT Start();
    HRESULT Stop();
    HRESULT PostDataEvent(_In_ LPWSTR SensorId, _In_ IPortableDeviceValues* pDataValues);
    HRESULT PostStateChange(_In_ LPWSTR SensorId, _In_ SensorState state);

// COM Interface methods
public:
    // ISensorDriver methods
    HRESULT STDMETHODCALLTYPE OnGetSupportedSensorObjects(
        _Out_ IPortableDeviceValuesCollection** ppSensorCollection
        );

    HRESULT STDMETHODCALLTYPE OnGetSupportedProperties(
        _In_  LPWSTR objectId,
        _Out_ IPortableDeviceKeyCollection** ppSupportedProperties
        );

    HRESULT STDMETHODCALLTYPE OnGetSupportedDataFields(
        _In_  LPWSTR objectId,
        _Out_ IPortableDeviceKeyCollection** ppSupportedDataFields
        );

    HRESULT STDMETHODCALLTYPE OnGetSupportedEvents(
        _In_  LPWSTR objectId,
        _Out_ GUID** ppSupportedEvents,
        _Out_ ULONG* pEventCount
        );

    HRESULT STDMETHODCALLTYPE OnGetProperties(
        _In_  IWDFFile* appId,
        _In_  LPWSTR objectId,
        _In_  IPortableDeviceKeyCollection* pProperties,
        _Out_ IPortableDeviceValues** ppPropertyValues
        );

    HRESULT STDMETHODCALLTYPE OnGetDataFields(
        _In_  IWDFFile* appId,
        _In_  LPWSTR objectId,
        _In_  IPortableDeviceKeyCollection* pDataFields,
        _Out_ IPortableDeviceValues** ppDataFieldValues
        );

    HRESULT STDMETHODCALLTYPE OnSetProperties(
        _In_  IWDFFile* appId,
        _In_  LPWSTR objectId,
        _In_  IPortableDeviceValues* pProperties,
        _Out_ IPortableDeviceValues** ppResults
        );

    HRESULT STDMETHODCALLTYPE OnClientConnect(
        _In_ IWDFFile* appId,
        _In_ LPWSTR objectId
        );

    HRESULT STDMETHODCALLTYPE OnClientDisconnect(
        _In_ IWDFFile* appId,
        _In_ LPWSTR objectId
        );

    HRESULT STDMETHODCALLTYPE OnClientSubscribeToEvents(
        _In_ IWDFFile* appId,
        _In_ LPWSTR objectId
        );

    HRESULT STDMETHODCALLTYPE OnClientUnsubscribeFromEvents(
        _In_ IWDFFile* appId,
        _In_ LPWSTR objectId
        );

    HRESULT STDMETHODCALLTYPE OnProcessWpdMessage(
        _In_ IUnknown* pPortableDeviceValuesParamsUnknown,
        _In_ IUnknown* pPortableDeviceValuesResultsUnknown
        );

private:
    HRESULT GetSensorObject(_In_ LPWSTR objectId, _Outptr_ CSensorDevice** ppSensor);

private:
    // Interface pointer to the class extension, sensor device, and IWDFDevice
    Microsoft::WRL::ComPtr<ISensorClassExtension>                 m_spClassExtension;
    Microsoft::WRL::ComPtr<CSensorDevice>                         m_pSensorDevice;
};

#endif // _SENSORDDI_H_
