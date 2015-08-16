/*++

// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.

Copyright (c) Microsoft Corporation. All rights reserved

Module Name:

    SensorDevice.h

Abstract:

    This module contains the type definitions for the CSensorDevice
    base class.

    Refer to the .cpp file comment block for more details.

--*/

#ifndef _SENSORDEVICE_H_
#define _SENSORDEVICE_H_

#pragma once

// Forward declarations
class CClientManager;
class CReportManager;
class CSensorDdi;

class CSensorDevice : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,IUnknown>
{
public:
    CSensorDevice();
    virtual ~CSensorDevice();

// Public methods
public:
    virtual HRESULT Initialize(
        _In_ IWDFDevice* pWdfDevice,
        _In_ IWDFCmResourceList* pWdfResourcesRaw,
        _In_ IWDFCmResourceList* pWdfResourcesTranslated,
        _In_ CSensorDdi* pSensorDdi);
    virtual void Uninitialize();

    HRESULT Start();
    HRESULT Stop();
    
    HRESULT GetSupportedProperties(_Out_ IPortableDeviceKeyCollection** ppSupportedProperties);
    HRESULT GetSupportedDataFields(_Out_ IPortableDeviceKeyCollection** ppSupportedDataFields);
    HRESULT GetSupportedEvents(_Out_ GUID** ppSupportedEvents, _Out_ ULONG* pEventCount);
    HRESULT GetProperties(
        _In_ IWDFFile* appId,
        _In_ IPortableDeviceKeyCollection* pProperties,
        _Out_ IPortableDeviceValues** ppPropertyValues);
    HRESULT GetDataFields(
        _In_ IWDFFile* appId,
        _In_ IPortableDeviceKeyCollection* pDataFields,
        _Out_ IPortableDeviceValues** ppDataFieldValues);
    HRESULT SetProperties(
        _In_ IWDFFile* appId,
        _In_ IPortableDeviceValues* pProperties,
        _Out_ IPortableDeviceValues** ppResults);
    HRESULT ClientConnect(_In_ IWDFFile* appId);
    HRESULT ClientDisconnect(_In_ IWDFFile* appId);
    HRESULT ClientSubscribeToEvents(_In_ IWDFFile* appId);
    HRESULT ClientUnsubscribeFromEvents(_In_ IWDFFile* appId);
    HRESULT ReportIntervalExpired();

    // Classes that derive from CSensorDevice must implement these methods
public:
    virtual LPWSTR GetSensorObjectID() = 0;
protected:
    virtual HRESULT InitializeDevice(
        _In_ IWDFDevice* pWdfDevice,
        _In_ IWDFCmResourceList* pWdfResourcesRaw,
        _In_ IWDFCmResourceList* pWdfResourcesTranslated) = 0;
    virtual const PROPERTYKEY* GetSupportedProperties(_Out_ ULONG* Count) = 0;
    virtual const PROPERTYKEY* GetSupportedPerDataFieldProperties(_Out_ ULONG* Count) = 0;
    virtual const PROPERTYKEY* GetSupportedDataFields(_Out_ ULONG* Count) = 0;
    virtual const PROPERTYKEY* GetSupportedEvents(_Out_ ULONG* Count) = 0;
    virtual HRESULT GetDefaultSettableProperties(
        _Out_ ULONG* pReportInterval,
        _Out_ IPortableDeviceValues** ppChangeSensitivities) = 0;
    virtual HRESULT SetDefaultPropertyValues() = 0;
    virtual HRESULT ConfigureHardware() = 0;
    virtual HRESULT SetReportInterval(_In_ ULONG ReportInterval) = 0;
    virtual HRESULT SetChangeSensitivity(_In_ PROPVARIANT* pVar) = 0;
    virtual HRESULT SetDeviceStateStandby() = 0;
    virtual HRESULT SetDeviceStatePolling() = 0;
    virtual HRESULT SetDeviceStateEventing() = 0;
    virtual HRESULT RequestNewData(_In_ IPortableDeviceValues* pValues) = 0;
    virtual HRESULT GetTestProperty(
        _In_  REFPROPERTYKEY key,
        _Out_ PROPVARIANT* pVar) = 0;
    virtual HRESULT SetTestProperty(
        _In_  REFPROPERTYKEY key,
        _In_  PROPVARIANT* pVar) = 0;

    // Initialization
    HRESULT InitializeSensorDriverInterface(_In_ IWDFDevice* pWdfDevice);
    
    HRESULT AddPropertyKeys();
    HRESULT AddDataFieldKeys();
    HRESULT SetUniqueID(_In_ IWDFDevice* pWdfDevice);

    // Steadystate methods
    HRESULT SetState(_In_ SensorState newState);
    BOOL HasStateChanged();
    HRESULT SetTimeStamp();
    HRESULT ApplyUpdatedProperties();
    HRESULT SetDataUpdateMode(_In_ DATA_UPDATE_MODE Mode);

    // Helper methods
    HRESULT GetProperty(
        _In_  REFPROPERTYKEY key, 
        _Out_ PROPVARIANT *pVar);

    HRESULT GetDataField(
        _In_  REFPROPERTYKEY key, 
        _Out_ PROPVARIANT *pVar);

    HRESULT GetAllDataFields(
        _Inout_ IPortableDeviceValues* pValues);

    HRESULT CopyKeys(
        _In_    IPortableDeviceKeyCollection *pSourceKeys,
        _Inout_ IPortableDeviceKeyCollection *pTargetKeys);

    BOOL IsPerDataFieldProperty(PROPERTYKEY key);
    BOOL IsTestProperty(PROPERTYKEY key);

    HRESULT PollForData();

    // Data eventing methods
    HRESULT DataAvailable(_In_ IPortableDeviceValues* pValues);
    void RaiseDataEvent();
    
protected:
    // PropertyKeys
    Microsoft::WRL::ComPtr<IPortableDeviceKeyCollection>  m_spSupportedSensorProperties;
    Microsoft::WRL::ComPtr<IPortableDeviceKeyCollection>  m_spSupportedSensorDataFields;
    BOOL                                                  m_fStateChanged;

    // Values
    Microsoft::WRL::ComPtr<IPortableDeviceValues>         m_spSensorPropertyValues;
    Microsoft::WRL::ComPtr<IPortableDeviceValues>         m_spSensorDataFieldValues;
        
    // Make calls to get/set properties thread safe
    Microsoft::WRL::Wrappers::CriticalSection             m_CacheCriticalSection;

    // Make calls to client thread safe
    Microsoft::WRL::Wrappers::CriticalSection             m_ClientCriticalSection;

    Microsoft::WRL::ComPtr<IWDFDevice2>                   m_spWdfDevice2;
    CSensorDdi*                                           m_pSensorDdi; // weak ref
    Microsoft::WRL::ComPtr<CClientManager>                m_pClientManager;
    Microsoft::WRL::ComPtr<CReportManager>                m_pReportManager;

    // Track sensor object state
    BOOL                                                  m_fSensorInitialized;
    DATA_UPDATE_MODE                                      m_DataUpdateMode;
};

#endif // _SENSORDEVICE_H_
