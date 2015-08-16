/*++

// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.

Copyright (c) Microsoft Corporation. All rights reserved

Module Name:

    ClientManager.h

Abstract:

    This module contains the type definitions for the SPB accelerometer's
    client manager class.

    Refer to the .cpp file comment block for more details.

--*/

#pragma once

typedef struct _CLIENT_ENTRY
{
    BOOL                    fSubscribed;
    IPortableDeviceValues*  pDesiredSensitivityValues;
    ULONG                   desiredReportInterval;
} CLIENT_ENTRY, *PCLIENT_ENTRY;

typedef std::map<IWDFFile*, CLIENT_ENTRY> CLIENT_MAP;

class CClientManager : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IUnknown>
{
// Public methods
public:
    CClientManager();
    ~CClientManager();
        
    HRESULT Initialize(
        _In_ ULONG DefaultReportInterval,
        _In_ ULONG MinReportInterval,
        _In_ IPortableDeviceValues* pDefaultChangeSensitivities);

    HRESULT Connect(_In_ IWDFFile* pClientFile);
    HRESULT Disconnect(_In_ IWDFFile* pClientFile);
    HRESULT Subscribe(_In_ IWDFFile* pClientFile);
    HRESULT Unsubscribe(_In_ IWDFFile* pClientFile);

    ULONG GetClientCount();
    ULONG GetSubscriberCount();
    DATA_UPDATE_MODE GetDataUpdateMode();

    HRESULT SetDesiredProperty(
        _In_  IWDFFile* pClientFile,
        _In_  REFPROPERTYKEY key, 
        _In_  PROPVARIANT* pVar, 
        _Out_ PROPVARIANT* pVarResult);

    HRESULT GetArbitratedProperty(
        _In_  REFPROPERTYKEY key,
        _Out_ PROPVARIANT* pVar);

// Private methods
private:
    HRESULT SetDesiredChangeSensitivity(
        _In_  IWDFFile* pClientFile,
        _In_  IPortableDeviceValues* pValues,
        _Out_ IPortableDeviceValues** ppResults);
    
    HRESULT SetDesiredReportInterval(
        _In_ IWDFFile* pClientFile,
        _In_ ULONG reportInterval);

    HRESULT RecalculateProperties();

    HRESULT CopyValues(
        _In_    IPortableDeviceValues* pSourceValues,
        _Inout_ IPortableDeviceValues* pTargetValues);

// Members
private:
    // Client management
    CLIENT_MAP                                                m_pClientList;
    ULONG                                                     m_ClientCount;
    ULONG                                                     m_SubscriberCount;
    Microsoft::WRL::Wrappers::CriticalSection                 m_ClientListCS;

    // Minimum settable properties
    Microsoft::WRL::ComPtr<IPortableDeviceValues>             m_spDefaultSensitivityValues;
    Microsoft::WRL::ComPtr<IPortableDeviceValues>             m_spMinSensitivityValues;
    ULONG                                                     m_defaultReportInterval;
    ULONG                                                     m_minSupportedReportInterval;
    ULONG                                                     m_minReportInterval;
    BOOLEAN                                                   m_minReportIntervalExplicitlySet;
    Microsoft::WRL::Wrappers::CriticalSection                 m_MinPropsCS;
};

