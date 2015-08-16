/*++

// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.

Copyright (c) Microsoft Corporation. All rights reserved

Module Name:

    ClientManager.cpp

Abstract:

    This module contains the implementation of the SPB accelerometer's
    client manager class.

    The client manager is responsible for:
    * Maintaining a list of clients (applications) subscribed to the sensor.
    * Calculating the effective report interval and change sensitivity
      settings based on the client settings.

--*/

#include "Internal.h"

#include <limits.h>
#include <float.h>

#include "ClientManager.h"
#include "ClientManager.tmh"

using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;

const ULONG CURRENT_REPORT_INTERVAL_NOT_SET = 0;

/////////////////////////////////////////////////////////////////////////
//
//  CClientManager::CClientManager()
//
//  Constructor.
//
/////////////////////////////////////////////////////////////////////////
CClientManager::CClientManager() :
    m_ClientCount(0),
    m_SubscriberCount(0),
    m_defaultReportInterval(0),
    m_minSupportedReportInterval(0),
    m_minReportInterval(0),
    m_minReportIntervalExplicitlySet(FALSE)
{

}

/////////////////////////////////////////////////////////////////////////
//
//  CClientManager::~CClientManager()
//
//  Destructor
//
/////////////////////////////////////////////////////////////////////////
CClientManager::~CClientManager()
{
    {
        // Synchronize access to the client list and associated members
        auto scopeLock = m_ClientListCS.Lock();

        for (CLIENT_MAP::iterator iter = m_pClientList.begin(); iter != m_pClientList.end(); iter++)
        {
            SAFE_RELEASE(iter->second.pDesiredSensitivityValues);
        }

        m_pClientList.clear();
    }

    {
        // Synchronize access to the minimum property cache
        auto propsLock = m_MinPropsCS.Lock();

        if (m_spDefaultSensitivityValues != nullptr)
        {
            m_spDefaultSensitivityValues->Clear();
        }

        if (m_spMinSensitivityValues != nullptr)
        {
            m_spMinSensitivityValues->Clear();
        }
    }
}

/////////////////////////////////////////////////////////////////////////
//
//  CClientManager::Initialize
//
//  This method is used to initialize the client manager and its internal
//  member objects.
//
//  Parameters:
//
//  Return Values:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CClientManager::Initialize(
    _In_ ULONG DefaultReportInterval,
    _In_ ULONG MinReportInterval,
    _In_ IPortableDeviceValues* pDefaultChangeSensitivities
    )
{
    FuncEntry();

    HRESULT hr = S_OK;

    if (m_spMinSensitivityValues == nullptr)
    {
        // Create a new PortableDeviceValues to store the min property VALUES
        hr = CoCreateInstance(
            CLSID_PortableDeviceValues,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&m_spMinSensitivityValues));
    }

    if (SUCCEEDED(hr) && m_spDefaultSensitivityValues == nullptr)
    {
        // Create a new PortableDeviceValues to store the default property VALUES
        hr = CoCreateInstance(
            CLSID_PortableDeviceValues,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&m_spDefaultSensitivityValues));
    }

    // Set default change sensitivities
    if (SUCCEEDED(hr))
    {
        hr = CopyValues(pDefaultChangeSensitivities, m_spDefaultSensitivityValues.Get());

        if (SUCCEEDED(hr))
        {
            hr = CopyValues(pDefaultChangeSensitivities, m_spMinSensitivityValues.Get());
        }
    }
    
    // Set default report interval
    if (SUCCEEDED(hr))
    {
        m_defaultReportInterval = DefaultReportInterval;
        m_minSupportedReportInterval = MinReportInterval;
        m_minReportInterval = DefaultReportInterval;
    }

    FuncExit();

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CClientManager::Connect
//
//  This method is used to indicate that a new client has connected.
//
//  Parameters:
//      pClientFile - interface pointer to the application's file handle
//
//  Return Values:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CClientManager::Connect(
    _In_ IWDFFile* pClientFile
    )
{
    FuncEntry();

    HRESULT hr = S_OK;

    if (pClientFile == nullptr)
    {
        hr = E_INVALIDARG;
    }

    if (SUCCEEDED(hr))
    {
        // Create a new PortableDeviceValues to store the property VALUES
        ComPtr<IPortableDeviceValues> spValues;
        hr = CoCreateInstance(
            CLSID_PortableDeviceValues,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&spValues));

        if (SUCCEEDED(hr))
        {
            // Synchronize access to the client list and associated members
            auto scopeLock = m_ClientListCS.Lock();
            
            // Sanity check the client count
            if (m_pClientList.size() != m_ClientCount)
            {
                hr = HRESULT_FROM_WIN32(ERROR_INVALID_STATE);

                Trace(
                    TRACE_LEVEL_ERROR,
                    "Invalid ClientManager state detected: client list "
                    "entries = %d, client count = %d, %!HRESULT!",
                    (ULONG)m_pClientList.size(),
                    m_ClientCount,
                    hr);
            }

            if (SUCCEEDED(hr))
            {
                // Check to see if the client is already
                // in the client list
                CLIENT_ENTRY entry;
                CLIENT_MAP::iterator iter = m_pClientList.find(pClientFile);
                if (iter != m_pClientList.end())
                {
                    // The client already exists
                    hr = HRESULT_FROM_WIN32(ERROR_FILE_EXISTS);

                    Trace(
                        TRACE_LEVEL_ERROR,
                        "Client %p already exists in the client list, %!HRESULT!",
                        pClientFile,
                        hr);
                }
        
                if (SUCCEEDED(hr))
                {
                    // Store the file id and initialize the entry
                    entry.fSubscribed = FALSE;
                    entry.pDesiredSensitivityValues = spValues.Get();
                    entry.desiredReportInterval = 
                        CURRENT_REPORT_INTERVAL_NOT_SET;
                
                    entry.pDesiredSensitivityValues->AddRef();

                    m_pClientList[pClientFile] = entry;

                    Trace(
                        TRACE_LEVEL_INFORMATION,
                        "Client %p has connected",
                        pClientFile);

                    // Increment client count
                    m_ClientCount++;
                    // Recalculate new minimum properties
                    hr = RecalculateProperties();
                }
            }
        }
    }

    FuncExit();

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CClientManager::Disconnect
//
//  This method is used to indicate that a client has disconnected.
//
//  Parameters:
//      pClientFile - interface pointer to the application's file handle
//
//  Return Values:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CClientManager::Disconnect(
    _In_ IWDFFile* pClientFile
    )
{
    FuncEntry();

    // Synchronize access to the client list and associated members
    auto scopeLock = m_ClientListCS.Lock();

    HRESULT hr = S_OK;

    if (pClientFile == nullptr)
    {
        hr = E_INVALIDARG;
    }

    // Sanity check the client count

    if (SUCCEEDED(hr))
    {
        if (m_ClientCount == 0)
        {
            hr = HRESULT_FROM_WIN32(ERROR_INVALID_STATE);

            Trace(
                TRACE_LEVEL_ERROR,
                "Invalid ClientManager state detected: attempting to "
                "disconnect client %p with client count = 0, %!HRESULT!",
                pClientFile,
                hr);
        }
    }

    if (SUCCEEDED(hr))
    {
        if (m_pClientList.size() != m_ClientCount)
        {
            hr = HRESULT_FROM_WIN32(ERROR_INVALID_STATE);

            Trace(
                TRACE_LEVEL_ERROR,
                "Invalid ClientManager state detected: client list "
                "entries = %d, client count = %d, %!HRESULT!",
                (ULONG)m_pClientList.size(),
                m_ClientCount,
                hr);
        }
    }

    if (SUCCEEDED(hr))
    {
        // Ensure the client is in the client list
        CLIENT_ENTRY entry;
        CLIENT_MAP::iterator iter = m_pClientList.find(pClientFile);
        if (iter == m_pClientList.end())
        {
            // The client isn't connected
            hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

            Trace(
                TRACE_LEVEL_ERROR,
                "Client %p was not found in the client list, %!HRESULT!",
                pClientFile,
                hr);
        }
        else
        {
            entry = iter->second;
            
            SAFE_RELEASE(entry.pDesiredSensitivityValues);
            
            m_pClientList.erase(iter);
            
            Trace(
                TRACE_LEVEL_INFORMATION,
                "Client %p has disconnected",
                pClientFile);

            if (entry.fSubscribed)
            {
                // Unexpected state, but maintain correct subscriber count
                m_SubscriberCount--;
            }

            // Decrement client count
            m_ClientCount--;

            // Recalculate new minimum properties
            hr = RecalculateProperties();

        }
       
    }

    FuncExit();

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CClientManager::Subscribe
//
//  This method is used to indicate that a client has subscribed to events.
//
//  Parameters:
//      pClientFile - interface pointer to the application's file handle
//
//  Return Values:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CClientManager::Subscribe(
    _In_ IWDFFile* pClientFile
    )
{
    FuncEntry();

    // Synchronize access to the client list and associated members
    auto scopeLock = m_ClientListCS.Lock();

    HRESULT hr = S_OK;

    if (pClientFile == nullptr)
    {
        hr = E_INVALIDARG;
    }

    if (SUCCEEDED(hr))
    {
        // Ensure the client is in the client list
        CLIENT_ENTRY entry;
        CLIENT_MAP::iterator iter = m_pClientList.find(pClientFile);
        if (iter == m_pClientList.end())
        {
            // The client isn't connected
            hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

            Trace(
                TRACE_LEVEL_ERROR,
                "Client %p was not found in the client list, %!HRESULT!",
                pClientFile,
                hr);
        }
        else
        {
            entry = iter->second;

            if (entry.fSubscribed)
            {
                hr = HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
                Trace(
                    TRACE_LEVEL_ERROR,
                    "Client %p is already subscribed, %!HRESULT!",
                    pClientFile,
                    hr);
            }
        }
        
        if (SUCCEEDED(hr))
        {
            // Mark the client as subscribed
            entry.fSubscribed = TRUE;

            m_pClientList[pClientFile] = entry;

            Trace(
                TRACE_LEVEL_INFORMATION,
                "Client %p has subscribed to events",
                pClientFile);

            // Increment subscriber count
            m_SubscriberCount++;

            // Recalculate new minimum properties
            hr = RecalculateProperties();
        }
    }

    FuncExit();

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CClientManager::Unsubscribe
//
//  This method is used to indicate that a client has unsubscribed from events.
//
//  Parameters:
//      pClientFile - interface pointer to the application's file handle
//
//  Return Values:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CClientManager::Unsubscribe(
    _In_ IWDFFile* pClientFile
    )
{
    FuncEntry();

    // Synchronize access to the client list and associated members
    auto scopeLock = m_ClientListCS.Lock();

    HRESULT hr = S_OK;

    if (pClientFile == nullptr)
    {
        hr = E_INVALIDARG;
    }

    if (SUCCEEDED(hr))
    {
        // Ensure the client is in the client list
        CLIENT_ENTRY entry;
        CLIENT_MAP::iterator iter = m_pClientList.find(pClientFile);
        if (iter == m_pClientList.end())
        {
            // The client isn't connected
            hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

            Trace(
                TRACE_LEVEL_ERROR,
                "Client %p was not found in the client list, %!HRESULT!",
                pClientFile,
                hr);
        }
        else
        {
            entry = iter->second;

            if (!entry.fSubscribed)
            {
                hr = HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
                Trace(
                    TRACE_LEVEL_ERROR,
                    "Client %p is not subscribed, %!HRESULT!",
                    pClientFile,
                    hr);
            }
        }
        
        if (SUCCEEDED(hr))
        {
            // Mark the client as unsubscribed
            entry.fSubscribed = FALSE;

            m_pClientList[pClientFile] = entry;

            Trace(
                TRACE_LEVEL_INFORMATION,
                "Client %p has unsubscribed from events",
                pClientFile);

            // Decrement subscriber count
            m_SubscriberCount--;

            // Recalculate new minimum properties
            hr = RecalculateProperties();
        }
    }

    FuncExit();

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CClientManager::GetClientCount
//
//  This method is used to retrieve the number of connected clients.
//
//  Parameters:
//
//  Return Values:
//      number of clients
//
/////////////////////////////////////////////////////////////////////////
ULONG CClientManager::GetClientCount()
{
    FuncEntry();

    // Synchronize access to the client list and associated members
    auto scopeLock = m_ClientListCS.Lock();

    FuncExit();

    return m_ClientCount;
}

/////////////////////////////////////////////////////////////////////////
//
//  CClientManager::GetSubscriberCount
//
//  This method is used to retrieve the number of clients subscribed to
//	events.
//
//  Parameters:
//
//  Return Values:
//      number of clients subscribed to events
//
/////////////////////////////////////////////////////////////////////////
ULONG CClientManager::GetSubscriberCount()
{
    FuncEntry();

    // Synchronize access to the client list and associated members
    auto scopeLock = m_ClientListCS.Lock();

    FuncExit();

    return m_SubscriberCount;
}

/////////////////////////////////////////////////////////////////////////
//
//  CClientManager::GetDataUpdateMode
//
//  This method is used to retrieve the current data update mode.
//
//  Parameters:
//
//  Return Values:
//      current data update mode
//
/////////////////////////////////////////////////////////////////////////
DATA_UPDATE_MODE CClientManager::GetDataUpdateMode()
{
    FuncEntry();

    DATA_UPDATE_MODE mode;

    // Synchronize access to the client list and associated members
    auto scopeLock = m_ClientListCS.Lock();

    if (m_ClientCount == 0)
    {
        mode = DataUpdateModeOff;
    }
    else
    {
        if ((m_SubscriberCount > 0) || 
            (m_minReportIntervalExplicitlySet == TRUE))
        {
            mode = DataUpdateModeEventing;
        }
        else
        {
            mode = DataUpdateModePolling;
        }
    }

    FuncExit();

    return mode;
}

/////////////////////////////////////////////////////////////////////////
//
//  CClientManager::SetDesiredProperty
//
//  This method is used to set a client's desired settable property
//  values.
//
//  Parameters:
//      pClientFile - interface pointer to the application's file handle
//      key - the desired settable property key
//      pVar - pointer to the key value

//  Return Values:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CClientManager::SetDesiredProperty(
    _In_  IWDFFile* pClientFile,
    _In_  REFPROPERTYKEY key, 
    _In_  PROPVARIANT* pVar, 
    _Out_ PROPVARIANT* pVarResult
    )
{
    FuncEntry();

    HRESULT hr = S_OK;

    if ((pClientFile == nullptr) ||
        (pVar == nullptr) ||
        (pVarResult == nullptr))
    {
        hr = E_INVALIDARG;
    }

    if (SUCCEEDED(hr))
    {
        // Change sensitivity
        if (IsEqualPropertyKey(
            key, 
            SENSOR_PROPERTY_CHANGE_SENSITIVITY) == TRUE)
        {
            // Change sensitivity is a per data field
            // property and is stored as an IPortableDeviceValues
            if (pVar->vt != VT_UNKNOWN)
            {
                hr = E_INVALIDARG;
            }

            if (SUCCEEDED(hr))
            {
                ComPtr<IPortableDeviceValues> spPerDataFieldValues;
                ComPtr<IPortableDeviceValues> spPerDataFieldResults;
                                
                spPerDataFieldValues =
                    static_cast<IPortableDeviceValues*>(pVar->punkVal);

                // Set the client's desired change sensitivity
                hr = SetDesiredChangeSensitivity(
                    pClientFile,
                    spPerDataFieldValues.Get(),
                    &spPerDataFieldResults);

                if (SUCCEEDED(hr))
                {
                    pVarResult->vt = VT_UNKNOWN;
                    pVarResult->punkVal = static_cast<IUnknown*>(
                        spPerDataFieldResults.Detach());
                }
            }

            if (FAILED(hr))
            {
                Trace(
                    TRACE_LEVEL_ERROR,
                    "Failed to set desired change sensitivity "
                    "for client %p, %!HRESULT!",
                    pClientFile,
                    hr);
            }
        }

        // Report interval
        else if (IsEqualPropertyKey(
            key, 
            SENSOR_PROPERTY_CURRENT_REPORT_INTERVAL) == TRUE)
        {
            // Report interval is type unsigned long
            if (pVar->vt != VT_UI4)
            {
                hr = E_INVALIDARG;
            }

            if (SUCCEEDED(hr))
            {
                ULONG reportInterval = pVar->ulVal;

                // Inform the client manager of the client's
                // desired report interval
                hr = SetDesiredReportInterval(
                    pClientFile,
                    reportInterval);

                if (SUCCEEDED(hr))
                {
                    *pVarResult = *pVar;
                }
            }

            if (FAILED(hr))
            {
                Trace(
                    TRACE_LEVEL_ERROR,
                    "Failed to set desired report interval "
                    "for client %p, %!HRESULT!",
                    pClientFile,
                    hr);
            }
        }

        // Other property key
        else
        {
            hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

            Trace(
                TRACE_LEVEL_ERROR,
                "The specified key is not one of the settable "
                "property values, %!HRESULT!",
                hr);
        }
    }

    FuncExit();

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CClientManager::GetArbitratedProperty
//
//  This method is used to retrieve the arbitrated settable property
//  value for all subscribed clients.
//
//  Parameters:
//      key - the requested property key
//      pValues - pointer to the IPortableDeviceValues where the
//          arbitrated property should be added
//
//  Return Values:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CClientManager::GetArbitratedProperty(
    _In_  REFPROPERTYKEY key,
    _Out_ PROPVARIANT* pVar
    )
{
    FuncEntry();

    HRESULT hr = S_OK;

    if (pVar == nullptr)

    {
        hr = E_INVALIDARG;
    }

    if (SUCCEEDED(hr))
    {
        // Change sensitivity
        if (IsEqualPropertyKey(key, SENSOR_PROPERTY_CHANGE_SENSITIVITY))
        {
            // Create a IPortableDeviceValues to copy the 
            // change sensitivity values into
            ComPtr<IPortableDeviceValues> spMinSensitivityValuesCopy;
            hr = CoCreateInstance(
                CLSID_PortableDeviceValues,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&spMinSensitivityValuesCopy));

            if (SUCCEEDED(hr))
            {
                // Synchronize access to the minimum property cache
                auto scopeLock = m_MinPropsCS.Lock();

                // Copy the per data field values
                hr = CopyValues(
                    m_spMinSensitivityValues.Get(), 
                    spMinSensitivityValuesCopy.Get());
            }

            if (SUCCEEDED(hr))
            {
                pVar->vt = VT_UNKNOWN;
                pVar->punkVal = static_cast<IUnknown*>(
                    spMinSensitivityValuesCopy.Detach());
            }

            if (FAILED(hr))
            {
                Trace(
                    TRACE_LEVEL_ERROR,
                    "Failed to retrieve the change sensitivity value, "
                    "%!HRESULT!",
                    hr);
            }
        
        }
        // Report interval
        else if (IsEqualPropertyKey(key, SENSOR_PROPERTY_CURRENT_REPORT_INTERVAL))
        {
            // Synchronize access to minimum property cache
            auto scopeLock = m_MinPropsCS.Lock();

            hr = InitPropVariantFromUInt32(
                m_minReportInterval,
                pVar);

            if (FAILED(hr))
            {
                Trace(
                    TRACE_LEVEL_ERROR,
                    "Failed to retrieve the report interval value, "
                    "%!HRESULT!",
                    hr);
            }
        }
        // Other non-settable property
        else
        {
            hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

            Trace(
                TRACE_LEVEL_ERROR,
                "The specified key is not one of the settable "
                "property values, %!HRESULT!",
                hr);
        }
    }

    FuncExit();

    return hr;

}

/////////////////////////////////////////////////////////////////////////
//
//  CClientManager::SetDesiredChangeSensitivity
//
//  This method is used to indicate a client's desired change sensitivity
//  values for each data field.
//
//  Parameters:
//      pClientFile - interface pointer to the application's file handle
//      pValues - collection of per data field change sensitivity values
//      ppResults - an IPortableDeviceValues pointer that receives the 
//          list of per data field change sensitivity values if successful 
//          or an error code
//
//  Return Values:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CClientManager::SetDesiredChangeSensitivity(
    _In_  IWDFFile* pClientFile,
    _In_  IPortableDeviceValues* pValues,
    _Out_ IPortableDeviceValues** ppResults
    )
{
    FuncEntry();

    HRESULT hr = S_OK;

    if ((pClientFile == nullptr) ||
        (pValues == nullptr) ||
        (ppResults == nullptr))
    {
        hr = E_INVALIDARG;
    }

    if (SUCCEEDED(hr))
    {
        // Synchronize access to the client list and associated members
        auto scopeLock = m_ClientListCS.Lock();

        DWORD count = 0;
        BOOL fError = FALSE;

        // Ensure the client is in the client list
        CLIENT_ENTRY entry;
        CLIENT_MAP::iterator iter = m_pClientList.find(pClientFile);
        
        if (iter == m_pClientList.end())
        {
            // The client isn't connected
            hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

            Trace(
                TRACE_LEVEL_ERROR,
                "Client %p was not found in the client list, %!HRESULT!",
                pClientFile,
                hr);
        }
        else
        {
            entry = iter->second;
        }
        
        if (SUCCEEDED(hr))
        {
            // CoCreate an object to store the per data field
            // property value results
            hr = CoCreateInstance(
                CLSID_PortableDeviceValues,
                NULL,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(ppResults));
        }

        if (SUCCEEDED(hr))
        {
            // Get the count of change sensitivity values
            // the client has set.
            hr = pValues->GetCount(&count);
        }

        if (SUCCEEDED(hr))
        {
            // Loop through each key and get each desired change
            // sensitivity
            for (DWORD i = 0; i < count; i++)
            {
                PROPERTYKEY key = WPD_PROPERTY_NULL;
                PROPVARIANT var;

                PropVariantInit( &var );

                hr = pValues->GetAt(i, &key, &var);

                if (SUCCEEDED(hr))
                {
                    HRESULT hrTemp = S_OK;
                    PROPVARIANT varTemp;
                    PropVariantInit(&varTemp);

                    // Make sure this key is one of the supported data field change 
                    // sensitivities and its type is correct.
                    hrTemp = m_spMinSensitivityValues->GetValue(key, &varTemp);
                    if (SUCCEEDED(hrTemp))
                    {
                        if ((var.vt != varTemp.vt && var.vt != VT_NULL) ||
                            (var.vt == VT_R4 && var.fltVal < 0.0f) ||
                            (var.vt == VT_R8 && var.dblVal < 0.0f))
                        {
                            hrTemp = E_INVALIDARG;
                            Trace(
                                TRACE_LEVEL_ERROR,
                                "Invalid vartype or value, %!HRESULT!",
                                hr);
                        }
                    }

                    if (SUCCEEDED(hrTemp))
                    {
                        hrTemp = m_pClientList[pClientFile].pDesiredSensitivityValues->SetValue(key, &var);

                        if (SUCCEEDED(hrTemp))
                        {
                            if (var.vt == VT_R4 || var.vt == VT_R8)
                            {
                                Trace(
                                    TRACE_LEVEL_INFORMATION,
                                    "Change sensitivity set to %f for client %p",
                                    var.vt == VT_R4 ? var.fltVal : var.dblVal,
                                    pClientFile);
                            }
                            else if (var.vt == VT_NULL)
                            {
                                Trace(
                                    TRACE_LEVEL_INFORMATION,
                                    "Change sensitivity cleared for client %p",
                                    pClientFile);
                            }

                            (*ppResults)->SetValue(key, &var);
                        }
                    }

                    if (FAILED(hrTemp))
                    {
                        Trace(
                            TRACE_LEVEL_ERROR,
                            "Change sensitivity is not supported for the "
                            "specified property key, %!HRESULT!",
                            hrTemp);

                        fError = TRUE;
                        (*ppResults)->SetErrorValue(key, hrTemp);
                    }

                    PropVariantClear(&varTemp);
                }

                PropVariantClear(&var);
                
                if (FAILED(hr))
                {
                    Trace(
                        TRACE_LEVEL_ERROR,
                        "Failed to get property key and value, %!HRESULT!",
                        hr);

                    break;
                }
            }

            if (SUCCEEDED(hr))
            {
                // Recalculate new minimum properties
                hr = RecalculateProperties();
            }
        }

        if (SUCCEEDED(hr) && (fError == TRUE))
        {
            hr = S_FALSE;
        }
    }

    FuncExit();

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CClientManager::SetDesiredReportInterval
//
//  This method is used to indicate a client's desired report interval
//  value.
//
//  Parameters:
//      pClientFile - interface pointer to the application's file handle
//      reportInterval - client's desired report interval
//
//  Return Values:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CClientManager::SetDesiredReportInterval(
    _In_ IWDFFile* pClientFile,
    _In_ ULONG reportInterval
    )
{
    FuncEntry();

    HRESULT hr = S_OK;

    if (pClientFile == nullptr)
    {
        hr = E_INVALIDARG;
    }

    if (SUCCEEDED(hr))
    {
        // Validate the report interval value, 0 means use default
        if ((reportInterval != 0) && 
            (reportInterval < m_minSupportedReportInterval))
        {
            hr = E_INVALIDARG;
        }
    }

    if (SUCCEEDED(hr))
    {
        // Synchronize access to the client list and associated members
        auto scopeLock = m_ClientListCS.Lock();

        // Ensure the client is in the client list
        CLIENT_ENTRY entry;
        CLIENT_MAP::iterator iter = m_pClientList.find(pClientFile);
        if (iter == m_pClientList.end())
        {
            // The client isn't connected
            hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

            Trace(
                TRACE_LEVEL_ERROR,
                "Client %p was not found in the client list, %!HRESULT!",
                pClientFile,
                hr);
        }
        else
        {
            entry = iter->second;
        }
        
        if (SUCCEEDED(hr))
        {
            // Save the change sensitivity value
            m_pClientList[pClientFile].desiredReportInterval = reportInterval;

            if (SUCCEEDED(hr))
            {
                Trace(
                    TRACE_LEVEL_INFORMATION,
                    "Report interval set to %lu for "
                    "client %p",
                    reportInterval,
                    pClientFile);

                // Recalculate new minimum properties
                hr = RecalculateProperties();
            }
        }
    }

    FuncExit();

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CClientManager::RecalculateProperties
//
//  This method is used to recalculate the settable properties.
//
//  Parameters:
//
//  Return Values:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CClientManager::RecalculateProperties(
    )
{
    FuncEntry();

    // Caller should be holding the client lock, but we must synchronize
    // access to the minimum properties.
    auto scopeLock = m_MinPropsCS.Lock();

    HRESULT hr = S_OK;

    // Initialize the minimum values
    ULONG sensitivityCount = 0;
    m_spMinSensitivityValues->GetCount(&sensitivityCount);
    for (ULONG i = 0; i < sensitivityCount; i++)
    {
        PROPERTYKEY key;
        PROPVARIANT var;
        PropVariantInit(&var);
        var.vt = VT_NULL;

        if (SUCCEEDED(m_spMinSensitivityValues->GetAt(i, &key, nullptr)))
        {
            m_spMinSensitivityValues->SetValue(key, &var);
        }
    }
    m_minReportInterval = ULONG_MAX;
    m_minReportIntervalExplicitlySet = FALSE;

    // Loop through each client and update the minimum
    // property values as necessary
    CLIENT_MAP::iterator iter = m_pClientList.begin();
    while (SUCCEEDED(hr) && (iter != m_pClientList.end()))
    {
        CLIENT_ENTRY entry = {0};

        entry = iter->second;

        if (SUCCEEDED(hr))
        {
            ULONG count = 0;
            entry.pDesiredSensitivityValues->GetCount(&count);
            for (ULONG i = 0; SUCCEEDED(hr) && i < count; i++)
            {
                PROPERTYKEY key;
                PROPVARIANT var;
                PropVariantInit(&var);

                hr = entry.pDesiredSensitivityValues->GetAt(i, &key, &var);

                if (SUCCEEDED(hr) && var.vt != VT_NULL)
                {
                    PROPVARIANT varMin;
                    PropVariantInit(&varMin);

                    hr = m_spMinSensitivityValues->GetValue(key, &varMin);

                    if (SUCCEEDED(hr))
                    {
                        // For now only float and double types are supported.
                        // Add others as necessary.
                        if (varMin.vt == VT_NULL)
                        {
                            // Use client's value.
                        }
                        else if (varMin.vt == VT_R4)
                        {
                            var.fltVal = min(varMin.fltVal, var.fltVal);
                        }
                        else if (varMin.vt == VT_R8)
                        {
                            var.dblVal = min(varMin.dblVal, var.dblVal);
                        }
                        else
                        {
                            hr = HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
                            Trace(
                                TRACE_LEVEL_ERROR,
                                "Vartype %d not supported for %!GUID!-%u, %!HRESULT!",
                                varMin.vt,
                                &key.fmtid,
                                key.pid,
                                hr);
                        }

                        if (SUCCEEDED(hr))
                        {
                            hr = m_spMinSensitivityValues->SetValue(key, &var);
                        }
                    }

                    PropVariantClear(&varMin);
                }

                PropVariantClear(&var);

                if (FAILED(hr))
                {
                    Trace(
                        TRACE_LEVEL_ERROR,
                        "Failed to update minimum sensitivity value for client, %!HRESULT!",
                        hr);
                }
            }

            if (SUCCEEDED(hr))
            {
                // If the client has set a lower report interval,
                // update the minimum
                if ((entry.desiredReportInterval !=
                        CURRENT_REPORT_INTERVAL_NOT_SET) &&
                        (entry.desiredReportInterval < m_minReportInterval))
                {
                    m_minReportInterval = entry.desiredReportInterval;
                    m_minReportIntervalExplicitlySet = TRUE;
                }
            }
        }

        iter++;
    }

    if (SUCCEEDED(hr))
    {
        // If the property values were not set by clients, set them to their defaults
        for (ULONG i = 0; SUCCEEDED(hr) && i < sensitivityCount; i++)
        {
            PROPERTYKEY key;
            PROPVARIANT var;
            PropVariantInit(&var);

            hr = m_spMinSensitivityValues->GetAt(i, &key, &var);
            if (SUCCEEDED(hr) && var.vt == VT_NULL)
            {
                PropVariantClear(&var);
                hr = m_spDefaultSensitivityValues->GetValue(key, &var);

                if (SUCCEEDED(hr))
                {
                    hr = m_spMinSensitivityValues->SetValue(key, &var);
                }
            }

            if (SUCCEEDED(hr))
            {
                if (var.vt == VT_R4 || var.vt == VT_R8)
                {
                    Trace(
                        TRACE_LEVEL_INFORMATION,
                        "Min change sensitivity for %!GUID!-%u is %f",
                        &key.fmtid,
                        key.pid,
                        var.vt == VT_R4 ? var.fltVal : var.dblVal);
                }
            }

            PropVariantClear(&var);
        }

        if (m_minReportInterval == ULONG_MAX)
        {
            m_minReportInterval = m_defaultReportInterval;
        }

        Trace(
            TRACE_LEVEL_INFORMATION,
            "Min report interval is %u",
            m_minReportInterval);
    }

    if (FAILED(hr))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            "Failed to recalculate minimum property values, %!HRESULT!",
            hr);
    }

    FuncExit();

    return hr;
}

///////////////////////////////////////////////////////////////////////////
//
//  CClientManager::CopyValues
//
//  Copies values from the source list to the target list.
//
//  Parameters: 
//      pSourceValues - an IPortableDeviceValues containing the list
//          of source values
//      pTargetValues - an IPortableDeviceValues to contain the list
//          the copied values
//
//  Return Value:
//      status
//
///////////////////////////////////////////////////////////////////////////
HRESULT CClientManager::CopyValues(
    _In_    IPortableDeviceValues* pSourceValues,
    _Inout_ IPortableDeviceValues* pTargetValues
    )
{
    FuncEntry();

    HRESULT hr = S_OK;
    DWORD count = 0;
    PROPERTYKEY key = {0};
    PROPVARIANT var = {0};

    if ((pSourceValues == nullptr) ||
        (pTargetValues == nullptr))
    {
        hr = E_INVALIDARG;
    }

    if (SUCCEEDED(hr))
    {
        hr = pSourceValues->GetCount(&count);
        
        if (SUCCEEDED(hr))
        {
            // Loop through each source key and copy to the
            // destination collection
            for (DWORD i = 0; i < count; i++)
            {
                PropVariantInit(&var);

                hr = pSourceValues->GetAt(i, &key, &var);
                
                if (SUCCEEDED(hr))
                {
                    hr = pTargetValues->SetValue(key, &var);
                }
                
                PropVariantClear(&var);
            }

            if (FAILED(hr))
            {
                Trace(
                    TRACE_LEVEL_ERROR,
                    "Failed to copy values, %!HRESULT!", 
                    hr);
            }
        }
    }

    FuncExit();

    return hr;
}
