/*++
 
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.

Copyright (C) Microsoft Corporation, All Rights Reserved

Module Name:

    SensorDevice.cpp

Abstract:

    This module implements the CSensorDevice base class.

    The CSensorDevice base class abstracts a sensor device and implements
    functionality common across sensor types:
    * Initialize the sensor and data report manager.
    * Return supported properties, data fields, and events.
    * Start/stop the sensor.
    * Manage client connection and subscription states.

    General-purpose classes (such as CSensorDdi and CReportManager) interact
    with CSensorDevice.

    Each sensor type (e.g. accelerometer) inherits from and specializes this
    base class (e.g. CAccelerometerDevice). The specializations define
    the sensor-type-specific properties and data fields, and do the actual
    work to interact with the hardware.

    To support a new sensor type, add a new class for that sensor type
    by following the example of CAccelerometerDevice.

--*/


#include "Internal.h"
#include "ClientManager.h"
#include "ReportManager.h"
#include "SensorDdi.h"

#include "SensorDevice.h"
#include "SensorDevice.tmh"

using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::CSensorDevice()
//
//  Constructor
//
/////////////////////////////////////////////////////////////////////////
CSensorDevice::CSensorDevice() :
    m_spSupportedSensorProperties(nullptr),
    m_spSupportedSensorDataFields(nullptr),
    m_spSensorPropertyValues(nullptr),
    m_spSensorDataFieldValues(nullptr),
    m_spWdfDevice2(nullptr),
    m_pClientManager(nullptr),
    m_pReportManager(nullptr),
    m_fStateChanged(FALSE),
    m_fSensorInitialized(FALSE),
    m_DataUpdateMode(DataUpdateModeOff)
{

}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::~CSensorDevice()
//
//  Destructor
//
/////////////////////////////////////////////////////////////////////////
CSensorDevice::~CSensorDevice()
{
    {
        // Synchronize access to the property cache
        auto scopeLock = m_CacheCriticalSection.Lock();

        // Clear the cache
        if (m_spSupportedSensorProperties != nullptr)
        {
            m_spSupportedSensorProperties->Clear();
            m_spSupportedSensorProperties = nullptr;
        }

        if (m_spSensorPropertyValues != nullptr)
        {
            m_spSensorPropertyValues->Clear();
            m_spSensorPropertyValues = nullptr;
        }

        if (m_spSupportedSensorDataFields != nullptr)
        {
            m_spSupportedSensorDataFields->Clear();
            m_spSupportedSensorDataFields = nullptr;
        }

        if (m_spSensorDataFieldValues != nullptr)
        {
            m_spSensorDataFieldValues->Clear();
            m_spSensorDataFieldValues = nullptr;
        }
    }

    m_fSensorInitialized = FALSE;
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::Initialize()
//
//  Initialize function that will set up the sensor device and the sensor
//  driver interface.
//
//  Parameters:
//      pWdfDevice - pointer to a device object
//      pWdfResourcesRaw - pointer the raw resource list
//      pWdfResourcesTranslated - pointer to the translated resource list
//
//  Return Values:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDevice::Initialize(
    _In_ IWDFDevice* pWdfDevice,
    _In_ IWDFCmResourceList* pWdfResourcesRaw,
    _In_ IWDFCmResourceList* pWdfResourcesTranslated,
    _In_ CSensorDdi* pSensorDdi
    )
{
    FuncEntry();

    // Check if we are already initialized
    HRESULT hr = S_OK;

    if (m_fSensorInitialized == FALSE)
    {
        // Save weak reference to callback
        m_pSensorDdi = pSensorDdi;

        hr = InitializeDevice(pWdfDevice, pWdfResourcesRaw, pWdfResourcesTranslated);

        if (SUCCEEDED(hr))
        {
            // Initialize the sensor driver interface
            hr = InitializeSensorDriverInterface(pWdfDevice);
        }

        ULONG defaultReportInterval = 0;
        ULONG minReportInterval = 1; // default to 1
        ComPtr<IPortableDeviceValues> spDefaultSensitivities;
        if (SUCCEEDED(hr))
        {
            hr = GetDefaultSettableProperties(&defaultReportInterval, &spDefaultSensitivities);

            if (SUCCEEDED(hr))
            {
                PROPVARIANT var = {};
                if (SUCCEEDED(m_spSensorPropertyValues->GetValue(SENSOR_PROPERTY_MIN_REPORT_INTERVAL, &var)) &&
                    var.vt == VT_UI4)
                {
                    minReportInterval = var.ulVal;
                }
            }
        }

        if (SUCCEEDED(hr))
        {
            // Create the client manager
            m_pClientManager = Make<CClientManager>();

            if (nullptr == m_pClientManager)
            {
                Trace(
                    TRACE_LEVEL_ERROR,
                    "Failed to create the Client Manager, %!HRESULT!",
                    hr);

                hr = E_OUTOFMEMORY;
            }

            if (SUCCEEDED(hr))
            {
                // Initialize the client manager
                hr = m_pClientManager->Initialize(defaultReportInterval, minReportInterval, spDefaultSensitivities.Get());
            }
        }

        if (SUCCEEDED(hr))
        {
            // Create the report manager
            m_pReportManager = Make<CReportManager>();

            if (nullptr == m_pClientManager)
            {
                Trace(
                    TRACE_LEVEL_ERROR,
                    "Failed to create the Report Manager, %!HRESULT!",
                    hr);

                hr = E_OUTOFMEMORY;
            }

            if (SUCCEEDED(hr))
            {
                // Initialize the report manager with the
                // default report interval
                m_pReportManager->Initialize(this, defaultReportInterval);
            }
        }

        if (SUCCEEDED(hr))
        {
            hr = pWdfDevice->QueryInterface(IID_PPV_ARGS(&m_spWdfDevice2));

            if (FAILED(hr))
            {
                Trace(
                    TRACE_LEVEL_ERROR,
                    "Failed to query IWDFDevice2 interface from IWDFDevice %p, "
                    "%!HRESULT!", 
                    pWdfDevice,
                    hr);
            }
        }

        if (SUCCEEDED(hr))
        {
            m_fSensorInitialized = TRUE;
        }
    }

    FuncExit();

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::InitializeSensorDriverInterface()
//
//  Initialize function that will set up all the propertykeys
//
//  Parameters:
//      pWdfDevice - pointer to a device object
//
//  Return Values:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDevice::InitializeSensorDriverInterface(
    _In_ IWDFDevice* pWdfDevice
    )
{
    FuncEntry();

    HRESULT hr = S_OK;

    if (m_spSupportedSensorProperties == nullptr)
    {
        // Create a new PortableDeviceKeyCollection to store the supported 
        // property KEYS
        hr = CoCreateInstance(
            CLSID_PortableDeviceKeyCollection,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&m_spSupportedSensorProperties));
    }
        
    if (SUCCEEDED(hr))
    {
        if (m_spSensorPropertyValues == nullptr)
        {
            // Create a new PortableDeviceValues to store the property VALUES
            hr = CoCreateInstance(
                CLSID_PortableDeviceValues,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&m_spSensorPropertyValues));
        }
    }
        
    if (SUCCEEDED(hr))
    {        
        if (m_spSupportedSensorDataFields == nullptr)
        {
            // Create a new PortableDeviceValues to store the supported 
            // datafield KEYS
            hr = CoCreateInstance(
                CLSID_PortableDeviceKeyCollection,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&m_spSupportedSensorDataFields));
        }
    }
        
    if (SUCCEEDED(hr))
    {        
        if (m_spSensorDataFieldValues == nullptr)
        {
            // Create a new PortableDeviceValues to store the datafield VALUES
            hr = CoCreateInstance(
                CLSID_PortableDeviceValues,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&m_spSensorDataFieldValues));
        }
    }

    if (SUCCEEDED(hr))
    {
        // Add supported property keys and initialize values
        hr = AddPropertyKeys();
    }

    if (SUCCEEDED(hr))
    {
        // Add supported data field keys and initialize values
        hr = AddDataFieldKeys();
    }

    if (SUCCEEDED(hr))
    {
        // Set the unique ID of the sensor
        hr = SetUniqueID(pWdfDevice);
    }

    if (SUCCEEDED(hr))
    {
        // Set the default property values
        hr = SetDefaultPropertyValues();
    }

    if (FAILED(hr))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            "Failed to initialize the sensor driver interface, %!HRESULT!", 
            hr);
    }

    FuncExit();

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::Uninitialize()
//
//  Uninitialize function that will tear down the sensor device and 
//  report manager.
//
//  Parameters:
//
//  Return Values:
//      none
//
/////////////////////////////////////////////////////////////////////////
void CSensorDevice::Uninitialize()
{
    FuncEntry();

    // Uninitialize the report manager
    if (m_pReportManager != nullptr)
    {
        m_pReportManager->Uninitialize();
    }

    // The sensor device has already been stopped
    // in D0Exit.  No further uninitialization
    // is necessary.

    FuncExit();
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::Start()
//
//  This method configures the sensor device and places it in standby mode.
//
//  Parameters:
//
//  Return Values:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDevice::Start()
{
    FuncEntry();

    // Check if we are already initialized
    HRESULT hr = m_fSensorInitialized ? S_OK : E_UNEXPECTED;

    if (SUCCEEDED(hr))
    {
        // Configure the sensor device. The sensor state should
        // already be SENSOR_STATE_NO_DATA, which will be updated when 
        // the first data bytes are received.
        hr = ConfigureHardware();

        if (FAILED(hr))
        {
            Trace(
                TRACE_LEVEL_ERROR,
                "The hardware could not be configured, %!HRESULT!", 
                hr);
        }
    }

    if (SUCCEEDED(hr))
    {
        if (m_pClientManager->GetClientCount() > 0)
        {
            // Restore previous configuration. This function
            // will apply the current report interval and change
            // sensitivity and set the reporting mode based on  
            // client connectivity and subscription.
            hr = ApplyUpdatedProperties();

            if (SUCCEEDED(hr))
            {
                // Poll for initial data
                hr = PollForData();
            }
        }
    }

    FuncExit();

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::Stop()
//
//  This method disables the sensor device.
//
//  Parameters:
//
//  Return Values:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDevice::Stop()
{
    FuncEntry();

    HRESULT hr = S_OK;

    // Indicate that the sensor no longer has valid data
    hr = SetState(SENSOR_STATE_NO_DATA);

    if (SUCCEEDED(hr))
    {
        hr = SetDataUpdateMode(DataUpdateModeOff);
    }

    FuncExit();

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::SetDataUpdateMode()
//
//  This method sets the data update mode.
//
//  Parameters:
//      Mode - new data update mode
//
//  Return Values:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDevice::SetDataUpdateMode(
    _In_  DATA_UPDATE_MODE Mode
    )
{
    FuncEntry();

    HRESULT hr = S_OK;

    switch (Mode)
    {
    case DataUpdateModeOff:
        hr = SetDeviceStateStandby();
        break;

    case DataUpdateModePolling:
        hr = SetDeviceStatePolling();
        break;

    case DataUpdateModeEventing:
        hr = SetDeviceStateEventing();
        break;

    default:
        hr = E_INVALIDARG;
        break;
    }

    if (FAILED(hr))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            "Failed to set data update mode %d, %!HRESULT!",
            Mode,
            hr);
    }

    if (SUCCEEDED(hr))
    {
        m_DataUpdateMode = Mode;
    }

    FuncExit();

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::GetSupportedProperties
//  
//  Parameters:
//      ppSupportedProperties - an IPortableDeviceKeyCollection to be populated with 
//          supported PROPERTYKEYs
//
//  Return Value:
//      status 
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDevice::GetSupportedProperties(
    _Out_ IPortableDeviceKeyCollection** ppSupportedProperties
    )
{
    FuncEntry();

    HRESULT hr = S_OK;

    // CoCreate a collection to store the supported property keys.
    hr = CoCreateInstance(
        CLSID_PortableDeviceKeyCollection,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(ppSupportedProperties));

    // Add supported property keys for the specified object to the collection
    if (SUCCEEDED(hr))
    {
        hr = CopyKeys(m_spSupportedSensorProperties.Get(), *ppSupportedProperties);
    }

    if (FAILED(hr))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            "Failed to get the supported sensor properties, %!HRESULT!", 
            hr);
    }

    FuncExit();

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::GetSupportedDataFields
//  
//  Parameters:
//      ppSupportedDataFields - An IPortableDeviceKeyCollection to be populated with 
//          supported PROPERTYKEYs
//
// Return Value:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDevice::GetSupportedDataFields(
    _Out_ IPortableDeviceKeyCollection** ppSupportedDataFields
    )
{
    FuncEntry();

    HRESULT hr = S_OK;

    // CoCreate a collection to store the supported property keys.
    hr = CoCreateInstance(
        CLSID_PortableDeviceKeyCollection,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(ppSupportedDataFields));

    // Add supported property keys for the specified object to the collection
    if (SUCCEEDED(hr))
    {
        hr = CopyKeys(m_spSupportedSensorDataFields.Get(), *ppSupportedDataFields);
    }

    if (FAILED(hr))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            "Failed to get the supported sensor data fields, %!HRESULT!", 
            hr);
    }

    FuncExit();

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::GetSupportedEvents
//  
//  Parameters:
//      ppSupportedEvents - A set of GUIDs that represent the supported events
//      pEventCount - Count of the number of events supported
//
//  Return Value:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDevice::GetSupportedEvents(
    _Out_ GUID** ppSupportedEvents,
    _Out_ ULONG* pEventCount
    )
{    
    FuncEntry();

    HRESULT hr = S_OK;
    ULONG count;
    const PROPERTYKEY* events = GetSupportedEvents(&count);
    
    // Allocate memory for the list of supported events
    GUID* pBuf = (GUID*)CoTaskMemAlloc(sizeof(GUID) * count);

    if (pBuf != nullptr)
    {
        for (DWORD i = 0; i < count; count++)
        {
            *(pBuf + i) = events[i].fmtid;
        }

        *ppSupportedEvents = pBuf;
        *pEventCount = count;
    }
    else
    {
        hr = E_OUTOFMEMORY;

        *ppSupportedEvents = nullptr;
        *pEventCount = 0;
    }

    if (FAILED(hr))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            "Failed to get the supported sensor events, %!HRESULT!", 
            hr);
    }

    FuncExit();

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::GetProperties
//  
//  Parameters:
//      appId - pinter to an IWDFFile interface that represents the file 
//          object for the application requesting property values
//      pProperties - an IPortableDeviceKeyCollection containing the list of 
//          properties being requested
//      ppPropertyValues - an IPortableDeviceValues pointer that receives the 
//          requested property values
//
//  Return Value:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDevice::GetProperties(
    _In_ IWDFFile* /* appId */,
    _In_ IPortableDeviceKeyCollection* pProperties,
    _Out_ IPortableDeviceValues** ppPropertyValues
    )
{
    FuncEntry();

    DWORD keyCount = 0;
    BOOL fError = FALSE;
    IPortableDeviceValues* pValues = nullptr;

    // CoCreate an object to store the property values
    HRESULT hr = CoCreateInstance(
        CLSID_PortableDeviceValues,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(ppPropertyValues));

    if (SUCCEEDED(hr))
    {
        pValues = *ppPropertyValues;
        
        if (pValues == nullptr)
        {
            hr = E_INVALIDARG;
        }
    }

    if (SUCCEEDED(hr))
    {
        hr = pProperties->GetCount(&keyCount);
    }

    if (SUCCEEDED(hr))
    {
        // Loop through each key and get the property value
        for (DWORD i = 0; i < keyCount; i++)
        {
            PROPERTYKEY key;
            hr = pProperties->GetAt(i, &key);

            if (SUCCEEDED(hr))
            {
                PROPVARIANT var;
                PropVariantInit(&var);

                HRESULT hrTemp = GetProperty(key, &var);

                if (SUCCEEDED(hrTemp))
                {
                   pValues->SetValue(key, &var);
                }
                else
                {
                    // Failed to find the requested property, 
                    // convey the hr back to the caller
                    Trace(
                        TRACE_LEVEL_ERROR,
                        "Failed to get the sensor property value, "
                        "%!HRESULT!", 
                        hrTemp);

                    pValues->SetErrorValue(key, hrTemp);
                    fError = TRUE;     
                }

                if ((var.vt & VT_VECTOR) == 0)
                {
                    // For a VT_VECTOR type, PropVariantClear()
                    // frees all underlying elements. Note pValues
                    // now has a pointer to the vector structure
                    // and is responsible for freeing it.

                    // If var is not a VT_VECTOR, clear it.
                    PropVariantClear(&var);
                }
            }
            else
            {
                Trace(
                    TRACE_LEVEL_ERROR,
                    "Failed to get property key, %!HRESULT!", 
                    hr);

                break;
            }
        }

        if (fError)
        {
            hr = S_FALSE;
        }
    }

    FuncExit();

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::GetDataFields
//  
//  Parameters:
//      appId - pinter to an IWDFFile interface that represents the file 
//          object for the application requesting data field values
//      pDataFields - an IPortableDeviceKeyCollection containing the list of 
//          data fields being requested
//      ppDataFieldValues - an IPortableDeviceValues pointer that receives the 
//          requested data field values
//
//  Return Value:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDevice::GetDataFields(
    _In_ IWDFFile* /* appId */,
    _In_ IPortableDeviceKeyCollection* pDataFields,
    _Out_ IPortableDeviceValues** ppDataFieldValues
    )
{    
    FuncEntry();

    DWORD keyCount = 0;
    BOOL fError = FALSE;
    IPortableDeviceValues*  pValues = nullptr;

    // CoCreate an object to store the data field values
    HRESULT hr = CoCreateInstance(
        CLSID_PortableDeviceValues,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(ppDataFieldValues));

    if (SUCCEEDED(hr))
    {
        pValues = *ppDataFieldValues;
        
        if (pValues == nullptr)
        {
            hr = E_INVALIDARG;
        }
    }

    if (SUCCEEDED(hr))
    {
        DWORD value;
        SensorState currentState = SENSOR_STATE_NOT_AVAILABLE;
        PROPVARIANT var;
        PropVariantInit(&var);

        // Get current state
        hr = GetProperty(SENSOR_PROPERTY_STATE, &var);

        if (SUCCEEDED(hr)) 
        {
            PropVariantToUInt32(var, &value);
            currentState = (SensorState)value;
        }

        if ((m_DataUpdateMode == DataUpdateModePolling) ||
            (currentState != SENSOR_STATE_READY))
        {
            hr = PollForData();
        }

        PropVariantClear(&var);
    }

    if (SUCCEEDED(hr))
    {
        hr = pDataFields->GetCount(&keyCount);
    }

    if (SUCCEEDED(hr))
    {
        // Loop through each key and get the data field value
        for (DWORD i = 0; i < keyCount; i++)
        {
            PROPERTYKEY key;
            hr = pDataFields->GetAt(i, &key);

            if (SUCCEEDED(hr))
            {
                PROPVARIANT var;
                PropVariantInit(&var);

                HRESULT hrTemp = GetDataField(key, &var);

                if (SUCCEEDED(hrTemp))
                {
                   pValues->SetValue(key, &var);
                }
                else
                {
                    // Failed to find the requested property, 
                    // convey the hr back to the caller
                    Trace(
                        TRACE_LEVEL_ERROR,
                        "Failed to get the sensor data field value, "
                        "%!HRESULT!", 
                        hrTemp);

                    pValues->SetErrorValue(key, hrTemp);
                    fError = TRUE;
                }

                PropVariantClear(&var);
            }
            else
            {
                Trace(
                    TRACE_LEVEL_ERROR,
                    "Failed to get property key, %!HRESULT!", 
                    hr);

                break;
            }
        }

        if (fError)
        {
            hr = S_FALSE;
        }
    }

    FuncExit();

    return hr;
}


/////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::SetProperties
//
//  This method is called by CSensorDdi::OnSetProperties to set Sensor properties
//  for a particular Sensor.
//  
//  Parameters:
//      appId - pinter to an IWDFFile interface that represents the file 
//          object for the application specifying property values
//      pProperties - an IPortableDeviceValues containing the list of 
//          properties being specified
//      ppResults - an IPortableDeviceValues pointer that receives the 
//          list of specified property values if successful or an error code
//
//  Return Value:
//      status
// 
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDevice::SetProperties(
    _In_ IWDFFile* appId,
    _In_ IPortableDeviceValues* pProperties,
    _Out_ IPortableDeviceValues** ppResults
    )
{
    FuncEntry();

    DWORD count = 0;
    BOOL fError = FALSE;
        
    // CoCreate an object to store the property value results
    HRESULT hr = CoCreateInstance(
        CLSID_PortableDeviceValues,
        NULL,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(ppResults));

    if (SUCCEEDED(hr))
    {
        hr = pProperties->GetCount(&count);
    }

    if (SUCCEEDED(hr))
    {
        // Loop through each key and get the 
        // property key and value
        for (DWORD i = 0; i < count; i++)
        {
            PROPERTYKEY key = WPD_PROPERTY_NULL;
            PROPVARIANT var;

            PropVariantInit( &var );

            hr = pProperties->GetAt(i, &key, &var);

            if (SUCCEEDED(hr))
            {
                HRESULT hrTemp;
                PROPVARIANT varResult;
                PropVariantInit(&varResult);

                // Check if this is one of the test properties
                if (IsTestProperty(key))
                {
                    hrTemp = SetTestProperty(
                        key,
                        &var);

                    // Test does not care about the property
                    // result.  No need to set varResult.
                }
                // Else assume this is one of the settable
                // properties, which are mainted by the client
                // manager
                else
                {
                    hrTemp = m_pClientManager->SetDesiredProperty(
                        appId,
                        key,
                        &var,
                        &varResult);
                }

                if (SUCCEEDED(hrTemp))
                {
                    (*ppResults)->SetValue(key, &varResult);

                    // Check if one of the change sensitivity
                    // values failed.  Convey this back to the
                    // client
                    if (hrTemp != S_OK)
                    {
                        fError = TRUE;
                    }
                }
                else
                {
                    Trace(
                        TRACE_LEVEL_ERROR,
                        "Failed to set property value, "
                        "%!HRESULT!",
                        hrTemp);
                                    
                    fError = TRUE;
                    (*ppResults)->SetErrorValue(key, hrTemp);
                }

                PropVariantClear(&varResult);
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
    }

    if (SUCCEEDED(hr))
    {
        // Successfully setting a property may have
        // caused the mimimum properties to change.
        // Be safe and reapply the updated values.
        hr = ApplyUpdatedProperties();
    }

    if (SUCCEEDED(hr) && (fError == TRUE))
    {
        hr = S_FALSE;
    }

    FuncExit();

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::ClientConnect
//  
//  Parameters:
//      appId - file object for the application requesting the conenction
//
//  Return Value:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDevice::ClientConnect(
    _In_ IWDFFile* appId
    )
{
    FuncEntry();

    HRESULT hr = S_OK;
    ULONG clientCount = 0;

    {
        // Synchronize access to the client manager so that after
        // the client connects it can query the new client
        // count atomically
        auto scopeLock = m_ClientCriticalSection.Lock();

        // Inform the client manager of the new client
        hr = m_pClientManager->Connect(appId);

        if (SUCCEEDED(hr))
        {
            // Save the client count
            clientCount = m_pClientManager->GetClientCount();
        }
    }

    if (SUCCEEDED(hr))
    {
        // The mimimum properties may have changed, reapply
        hr = ApplyUpdatedProperties();
    }

    if (SUCCEEDED(hr))
    {
        // Stop idle detection if this is the first client
        if (clientCount == 1)
        {
            Trace(
                TRACE_LEVEL_INFORMATION,
                "First client, stop idle detection");

            // When using a power managed queue we are guaranteed
            // to be in D0 during OnClientConnect, so there is no need
            // to block on this call. It's safe to touch hardware at 
            // this point. There is potential, however, to temporarily 
            // transition from D0->Dx->D0 after this call returns, so be 
            // sure to reconfigure the hardware in D0Enty.
            hr = m_spWdfDevice2->StopIdle(false);

            if (FAILED(hr))
            {
                Trace(
                    TRACE_LEVEL_ERROR,
                    "Failed to stop idle detection for IWDFDevice2 %p, "
                    "%!HRESULT!",
                    m_spWdfDevice2.Get(),
                    hr);
            }

            if (SUCCEEDED(hr))
            {
                hr = SetDataUpdateMode(DataUpdateModePolling);
            }

            if (SUCCEEDED(hr))
            {
                // Poll for initial data
                hr = PollForData();
            }
        }
    }

    FuncExit();

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::ClientDisconnect
//  
//  Parameters:
//      appId - file object for the application requesting 
//          the disconnection
//
//  Return Value:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDevice::ClientDisconnect(
    _In_ IWDFFile* appId
    )
{
    FuncEntry();

    HRESULT hr = S_OK;
    ULONG clientCount = 0;

    {
        // Synchronize access to the client manager so that after
        // the client disconnects it can query the new client
        // count atomically
        auto scopeLock = m_ClientCriticalSection.Lock();

        // Inform the client manager that the client
        // is leaving
        hr  = m_pClientManager->Disconnect(appId);

        if (SUCCEEDED(hr))
        {
            // Save the client count
            clientCount = m_pClientManager->GetClientCount();
        }
    }

    if (SUCCEEDED(hr))
    {
        // The mimimum properties may have changed, reapply
        hr = ApplyUpdatedProperties();
    }

    if (SUCCEEDED(hr))
    {
        // Resume idle detection if there are no more clients
        if (clientCount == 0)
        {
            Trace(
                TRACE_LEVEL_INFORMATION,
                "No clients, resume idle detection");

            m_spWdfDevice2->ResumeIdle();

            if (SUCCEEDED(hr))
            {
                hr = SetDataUpdateMode(DataUpdateModeOff);
            }
        }
    }

    FuncExit();

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::ClientSubscribeToEvents
//  
//  Parameters:
//      appId - file object for the application subscribing to events
//
// Return Value:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDevice::ClientSubscribeToEvents(
    _In_ IWDFFile* appId
    )
{
    FuncEntry();

    // Let the client manager know that the client
    // has subscribed
    HRESULT hr = m_pClientManager->Subscribe(appId);

    if (SUCCEEDED(hr))
    {
        // The mimimum properties may have changed, reapply
        hr = ApplyUpdatedProperties();
    }

    FuncExit();

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::ClientUnsubscribeFromEvents
//  
//  Parameters:
//      appId - file object for the application unsubscribing from events
//
// Return Value:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDevice::ClientUnsubscribeFromEvents(
    _In_ IWDFFile* appId
    )
{
    FuncEntry();

        // Let the client manager know that the client
    // has subscribed
    HRESULT hr = m_pClientManager->Unsubscribe(appId);

    if (SUCCEEDED(hr))
    {
        // The mimimum properties may have changed, reapply
        hr = ApplyUpdatedProperties();
    }

    FuncExit();

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::AddPropertyKeys
//
//  This methods populates the supported properties list and initializes
//  their values to VT_EMPTY.
//
//  Parameters: 
//
//  Return Value:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDevice::AddPropertyKeys()
{
    FuncEntry();

    // Synchronize access to the property cache
    auto scopeLock = m_CacheCriticalSection.Lock();

    HRESULT hr = S_OK;

    if (m_spSupportedSensorProperties == nullptr)
    {
        hr = E_POINTER;
    }

    if (SUCCEEDED(hr))
    {
        ULONG count;
        const PROPERTYKEY* properties = GetSupportedProperties(&count);
        for (DWORD i = 0; i < count; i++)
        {
            PROPVARIANT var;
            PropVariantInit(&var);

            // Add the PROPERTYKEY to the list of supported properties
            if (SUCCEEDED(hr))
            {
                hr = m_spSupportedSensorProperties->Add(
                    properties[i]);
            }

            // Initialize the PROPERTYKEY value to VT_EMPTY
            if (SUCCEEDED(hr))
            {
                hr = m_spSensorPropertyValues->SetValue(
                    properties[i],
                    &var);
            }

            PropVariantClear(&var);

            if (FAILED(hr))
            {
                Trace(
                    TRACE_LEVEL_ERROR,
                    "Failed to add the sensor property key, %!HRESULT!,",
                    hr);
            }
        }
    }

    FuncExit();

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::AddDataFieldKeys
//
//  This methods populates the supported data fields list and initializes
//  their values to VT_EMPTY.
//
//  Parameters: 
//
//  Return Value:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDevice::AddDataFieldKeys()
{
    FuncEntry();

    // Synchronize access to the property cache
    auto scopeLock = m_CacheCriticalSection.Lock();

    HRESULT hr = S_OK;

    if (m_spSupportedSensorDataFields == nullptr)
    {
        hr = E_POINTER;
    }

    if (SUCCEEDED(hr))
    {
        ULONG count;
        const PROPERTYKEY* datafields = GetSupportedDataFields(&count);
        for (DWORD i = 0; i < count; i++)
        {
            PROPVARIANT var;
            PropVariantInit(&var);

            // Add the PROPERTYKEY to the list of supported properties
            if (SUCCEEDED(hr))
            {
                hr = m_spSupportedSensorDataFields->Add(
                    datafields[i]);
            }

            // Initialize the PROPERTYKEY value to VT_EMPTY
            if (SUCCEEDED(hr))
            {
                hr = m_spSensorDataFieldValues->SetValue(
                    datafields[i],
                    &var);
            }

            PropVariantClear(&var);

            if (FAILED(hr))
            {
                Trace(
                    TRACE_LEVEL_ERROR,
                    "Failed to add the sensor data field key, %!HRESULT!,",
                    hr);
            }
        }
    }

    FuncExit();

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::SetUniqueID
//
//  This methods sets the persistent unique ID property.
//
//  Parameters: 
//      pWdfDevice - pointer to a device object
//
//  Return Value:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDevice::SetUniqueID(_In_ IWDFDevice* pWdfDevice)
{
    FuncEntry();

    // Synchronize access to the property cache
    auto scopeLock = m_CacheCriticalSection.Lock();

    HRESULT hr = S_OK;
    
    if (pWdfDevice == nullptr)
    {
        hr = E_INVALIDARG;
    }

    ComPtr<IWDFNamedPropertyStore> spPropStore;
    if (SUCCEEDED(hr))
    {
        hr = pWdfDevice->RetrieveDevicePropertyStore(
            nullptr, 
            WdfPropertyStoreCreateIfMissing, 
            &spPropStore, 
            nullptr);
    }

    if (SUCCEEDED(hr))
    {
        GUID idGuid;
        LPCWSTR lpcszKeyName = GetSensorObjectID();
        
        PROPVARIANT var;
        PropVariantInit(&var);
        hr = spPropStore->GetNamedValue(lpcszKeyName, &var);
        if (SUCCEEDED(hr))
        {
            hr = ::CLSIDFromString(var.bstrVal, &idGuid);
        }
        else
        {
            hr = ::CoCreateGuid(&idGuid);
            if (SUCCEEDED(hr))
            {
                LPOLESTR lpszGUID = nullptr;
                hr = ::StringFromCLSID(idGuid, &lpszGUID);
                if (SUCCEEDED(hr))
                {
                    var.vt = VT_LPWSTR;
                    var.pwszVal = lpszGUID;
                    hr = spPropStore->SetNamedValue(lpcszKeyName, &var);
                }
            }
        }

        PropVariantClear(&var);

        if (SUCCEEDED(hr))
        {
            hr = m_spSensorPropertyValues->SetGuidValue(
                SENSOR_PROPERTY_PERSISTENT_UNIQUE_ID, 
                idGuid);
        }
    }

    if (FAILED(hr))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            "Failed to set the sensor's unique ID, %!HRESULT!", 
            hr);
    }

    FuncExit();

    return hr;
}

///////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::GetProperty
//
//  Gets the property value for a given property key.
//
//  Parameters:
//      key - the requested property key
//      pVar - location for the value of the requested property key
//
//  Return Value:
//      status
//
///////////////////////////////////////////////////////////////////////////
HRESULT CSensorDevice::GetProperty(
        _In_  REFPROPERTYKEY key, 
        _Out_ PROPVARIANT* pVar
        )
{
    HRESULT hr = S_OK;

    // Check if this is a test property
    if (IsTestProperty(key))
    {
        hr = GetTestProperty(key, pVar);
    }
    // Settable properties are managed by the client manager
    else if (IsEqualPropertyKey(key, SENSOR_PROPERTY_CHANGE_SENSITIVITY) ||
        IsEqualPropertyKey(key, SENSOR_PROPERTY_CURRENT_REPORT_INTERVAL))
    {
        hr = m_pClientManager->GetArbitratedProperty(key, pVar);
    }
    // Other property key
    else
    {
        // Synchronize access to the property cache
        auto scopeLock = m_CacheCriticalSection.Lock();

        if (m_spSensorPropertyValues == nullptr)
        {
            hr = E_POINTER;
        }
        else
        {
            // Retrieve property value from cache
            hr = m_spSensorPropertyValues->GetValue(key, pVar);
        }
    }

    if (FAILED(hr))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            "Failed to get the sensor property value, %!HRESULT!", 
            hr);
    }

    FuncExit();

    return hr;
}

///////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::SetState
//
//  Sets the property value for a given property key.
//
//  Parameters: 
//      newState - the new state of the sensor
//
//  Return Value:
//      status
//
///////////////////////////////////////////////////////////////////////////
HRESULT CSensorDevice::SetState(
    _In_ SensorState newState
    )
{
    FuncEntry();

    HRESULT hr = S_OK;

    DWORD value;
    SensorState currentState;

    PROPVARIANT var;
    PropVariantInit(&var);    

    // Get current state
    hr = GetProperty(SENSOR_PROPERTY_STATE, &var);

    if (SUCCEEDED(hr)) 
    {
        PropVariantToUInt32(var, &value);
        currentState = (SensorState)value;

        if (currentState != newState)
        {
            // Synchronize access to the property cache
            auto scopeLock = m_CacheCriticalSection.Lock();

            // State has changed, update property
            // value in the cache

            Trace(
                TRACE_LEVEL_INFORMATION,
                "State has changed, now %d",
                newState);

            PropVariantClear(&var);
            InitPropVariantFromUInt32(newState, &var);

            hr = m_spSensorPropertyValues->SetValue(
                SENSOR_PROPERTY_STATE,
                &var);

            if (SUCCEEDED(hr))
            {
                m_fStateChanged = TRUE;
            }
        }
    }

    PropVariantClear(&var);

    FuncExit();

    return hr;
}

///////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::HasStateChanged
//
//  Checks if the state has changed since the last data event.
//
//  Parameters: 
//
//  Return Value:
//      TRUE if it has changed and clears the flag
//
///////////////////////////////////////////////////////////////////////////
BOOL CSensorDevice::HasStateChanged()
{
    FuncEntry();

    // Synchronize access to the property cache
    auto scopeLock = m_CacheCriticalSection.Lock();

    BOOL fStateEvent = FALSE;

    // Check if there is a valid data event to post
    if(TRUE == m_fStateChanged)
    {
        fStateEvent = m_fStateChanged;
        m_fStateChanged = FALSE;
    }

    FuncExit();

    return fStateEvent;
}

///////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::SetTimeStamp
//
//  Sets the timestamp when the data cache is updated with new data.
//
//  Parameters:
//
//  Return Value:
//      status
//
///////////////////////////////////////////////////////////////////////////
HRESULT CSensorDevice::SetTimeStamp()
{
    FuncEntry();

    // Synchronize access to the data field cache
    auto scopeLock = m_CacheCriticalSection.Lock();

    HRESULT hr = S_OK;

    PROPVARIANT var;

    // Get the current time as FILETIME format
    FILETIME ft;

    GetSystemTimePreciseAsFileTime(&ft);

    hr = InitPropVariantFromFileTime(&ft, &var);
    if (SUCCEEDED(hr))
    {
        m_spSensorDataFieldValues->SetValue(SENSOR_DATA_TYPE_TIMESTAMP, &var);
    }

    PropVariantClear(&var);

    FuncExit();

    return hr;
}

///////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::GetDataField
//
//  Gets the data field value for a given data field key.
//
//  Parameters: 
//      key - the requested data field key
//      pVar - location for the value of the requested data field key
//
//  Return Value:
//      status
//
///////////////////////////////////////////////////////////////////////////
HRESULT CSensorDevice::GetDataField(
        _In_  REFPROPERTYKEY key, 
        _Out_ PROPVARIANT* pVar
        )
{ 
    FuncEntry();

    // Synchronize access to the data field cache
    auto scopeLock = m_CacheCriticalSection.Lock();

    HRESULT hr = S_OK;

    if (m_spSensorDataFieldValues == nullptr)
    {
        hr = E_POINTER;
    }
    else
    {
        // Retrieve the value
        hr = m_spSensorDataFieldValues->GetValue(key, pVar);

        if (FAILED(hr))
        {
            Trace(
                TRACE_LEVEL_ERROR,
                "Failed to get the sensor data field value, %!HRESULT!", 
                hr);
        }
    }

    FuncExit();

    return hr;
}

///////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::GetAllDataFields
//
//  Gets all of the data field values.
//
//  Parameters: 
//      pValues - pointer to the IPortableDeviceValues structure to place all
//          of the data field values
//
//  Return Value:
//      status
//
///////////////////////////////////////////////////////////////////////////
HRESULT CSensorDevice::GetAllDataFields(
    _Inout_ IPortableDeviceValues* pValues)
{
    FuncEntry();

    // Synchronize access to the data field cache
    auto scopeLock = m_CacheCriticalSection.Lock();

    HRESULT hr = S_OK;

    if (m_spSensorDataFieldValues == nullptr)
    {
        hr = E_POINTER;
    }
    else
    {
        DWORD count = 0;
        hr = m_spSensorDataFieldValues->GetCount(&count);

        if (SUCCEEDED(hr))
        {
            // Loop through each data field and add
            // its value to the list
            for (DWORD i = 0; i < count; i++)
            {
                PROPERTYKEY key;
                PROPVARIANT var;
                PropVariantInit( &var );
                hr = m_spSensorDataFieldValues->GetAt(i, &key, &var);
                
                if (SUCCEEDED(hr))
                {
                    hr = pValues->SetValue(key, &var);
                }

                PropVariantClear(&var);
            }
        }

    }

    FuncExit();

    return hr;
}

///////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::CopyKeys
//
//  Copies keys from the source list to the target list.
//
//  Parameters: 
//      pSourceKeys - an IPortableDeviceKeyCollection containing the list
//          of source keys
//      pTargetKeys - an IPortableDeviceKeyCollection to contain the list
//          the copied keys
//
//  Return Value:
//      status
//
///////////////////////////////////////////////////////////////////////////
HRESULT CSensorDevice::CopyKeys(
    _In_    IPortableDeviceKeyCollection *pSourceKeys,
    _Inout_ IPortableDeviceKeyCollection *pTargetKeys)
{
    FuncEntry();

    HRESULT hr = S_OK;
    DWORD cKeys = 0;
    PROPERTYKEY key = {0};

    if ((pSourceKeys == nullptr) ||
        (pTargetKeys == nullptr))
    {
        hr = E_INVALIDARG;
    }

    if (SUCCEEDED(hr))
    {
        hr = pSourceKeys->GetCount(&cKeys);
        
        if (SUCCEEDED(hr))
        {
            // Loop through each source key and copy to the
            // destination collection
            for (DWORD dwIndex = 0; dwIndex < cKeys; ++dwIndex)
            {
                hr = pSourceKeys->GetAt(dwIndex, &key);
                
                if (SUCCEEDED(hr))
                {
                    hr = pTargetKeys->Add(key);
                }
            }

            if (FAILED(hr))
            {
                Trace(
                    TRACE_LEVEL_ERROR,
                    "Failed to copy keys, %!HRESULT!", 
                    hr);
            }
        }
    }

    FuncExit();

    return hr;
}

///////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::IsPerDataFieldProperty
//
//  This method is used to determine if the specified property key
//      has per data field values
//
//  Parameters:
//      key - property key in question
//
//  Return Value:
//      status
//
///////////////////////////////////////////////////////////////////////////
BOOL CSensorDevice::IsPerDataFieldProperty(PROPERTYKEY key)
{
    FuncEntry();

    BOOL fPdfkey = FALSE;

    // Loop through the per data field values and see if the key matches
    ULONG count = 0;
    const PROPERTYKEY* properties = GetSupportedPerDataFieldProperties(&count);
    for (ULONG i = 0; i < count; i++)
    {
        if (IsEqualPropertyKey(key, properties[i]))
        {
            fPdfkey = TRUE;
            break;
        }
    }

    FuncExit();

    return fPdfkey;
}

///////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::IsTestProperty
//
//  This method is used to determine if the key is a test property key.
//
//  Parameters:
//      key - property key in question
//
//  Return Value:
//      TRUE if test property key
//
///////////////////////////////////////////////////////////////////////////
BOOL CSensorDevice::IsTestProperty(PROPERTYKEY key)
{
    FuncEntry();

    BOOL fTestkey = FALSE;

    if (IsEqualPropertyKey(key, SENSOR_PROPERTY_TEST_REGISTER) ||
        IsEqualPropertyKey(key, SENSOR_PROPERTY_TEST_DATA_SIZE) ||
        IsEqualPropertyKey(key, SENSOR_PROPERTY_TEST_DATA))
    {
        fTestkey = TRUE;
    }

    FuncExit();

    return fTestkey;
}

///////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::DataAvailable
//
//  Method indicating new data is available.
//
//  Parameters:
//      pValues - an IPortableDeviceValues collection containing the list of 
//          new data field values
//
//  Return Value:
//      status
//
///////////////////////////////////////////////////////////////////////////
HRESULT CSensorDevice::DataAvailable(
    _In_ IPortableDeviceValues* pValues
    )
{
    FuncEntry();

    HRESULT hr = S_OK;

    if (pValues == nullptr)
    {
        hr = E_INVALIDARG;
    }

    // Update the timestamp
    if (SUCCEEDED(hr))
    {
        hr = SetTimeStamp();
    }
    
    // Update cache with the new data
    if (SUCCEEDED(hr))
    {
        DWORD valueCount = 0;
        hr = pValues->GetCount(&valueCount);

        if (SUCCEEDED(hr))
        {
            // Synchronize access to the property cache
            auto scopeLock = m_CacheCriticalSection.Lock();

            // Loop through each data field value
            // and update cache
            for (DWORD i = 0; i < valueCount; i++)
            {
                PROPERTYKEY key = WPD_PROPERTY_NULL;
                PROPVARIANT var;

                PropVariantInit(&var);

                hr = pValues->GetAt(i, &key, &var);

                if (SUCCEEDED(hr))
                {
                    // Data value was already validated. Go
                    // ahead and update cache
                    hr = m_spSensorDataFieldValues->SetValue(key, &var);
                }

                PropVariantClear(&var);
                
                if (FAILED(hr))
                {
                    break;
                }
            }
        }
    }

    // Mark sensor state as ready.
    if (SUCCEEDED(hr))
    {
        hr = SetState(SENSOR_STATE_READY);
    }

    if (SUCCEEDED(hr))
    {
        Trace(
            TRACE_LEVEL_VERBOSE,
            "New data received from the device");
        
        // Raise an event to signal new data
        RaiseDataEvent();
    }

    FuncExit();

    return hr;
}

///////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::RaiseDataEvent
//
//  This method is called when new data is received from the device.  It only
//  raises an event if there are clients subscribed.
//
//  Parameters:
//
//  Return Value:
//      none
//
///////////////////////////////////////////////////////////////////////////
VOID CSensorDevice::RaiseDataEvent()
{
    FuncEntry();

    // Check if there are any subscribers
    if(m_pClientManager->GetSubscriberCount() > 0)
    {
        // Inform the report manager when new data is available.
        // It will determine when a data event should be
        // posted to the class extension.
        m_pReportManager->NewDataAvailable();
    }

    FuncExit();
}

///////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::PollForData
//
//  This method is called to synchronously poll the device for new data
//  and update the data cache.
//
//  Parameters:
//
//  Return Value:
//      status
//
///////////////////////////////////////////////////////////////////////////
HRESULT CSensorDevice::PollForData()
{
    ComPtr<IPortableDeviceValues> spValues = nullptr;
    HRESULT hr;

    // Create an IPortableDeviceValues to hold the data
    hr = CoCreateInstance(
        CLSID_PortableDeviceValues,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&spValues));
                
    if (SUCCEEDED(hr))
    {
        hr = RequestNewData(spValues.Get());
    
        if (FAILED(hr))
        {
            Trace(
                TRACE_LEVEL_ERROR,
                "Failed to poll for new data, %!HRESULT!",
                hr);
        }
    }
    
    // Update cache with the new data
    if (SUCCEEDED(hr))
    {
        DWORD valueCount = 0;
        hr = spValues->GetCount(&valueCount);

        if (SUCCEEDED(hr))
        {
            // Synchronize access to the property cache
            auto scopeLock = m_CacheCriticalSection.Lock();

            // Loop through each data field value
            // and update cache
            for (DWORD i = 0; i < valueCount; i++)
            {
                PROPERTYKEY key = WPD_PROPERTY_NULL;
                PROPVARIANT var;

                PropVariantInit(&var);

                hr = spValues->GetAt(i, &key, &var);

                if (SUCCEEDED(hr))
                {
                    // Data value was already validated. Go
                    // ahead and update cache
                    hr = m_spSensorDataFieldValues->SetValue(key, &var);
                }

                PropVariantClear(&var);
                
                if (FAILED(hr))
                {
                    break;
                }
            }
        }
    }

    // Update the timestamp
    if (SUCCEEDED(hr))
    {
        hr = SetTimeStamp();
    }

    // Mark sensor state as ready.
    if (SUCCEEDED(hr))
    {
        hr = SetState(SENSOR_STATE_READY);
    }

    return hr;
}

///////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::ApplyUpdatedProperties
//
//  This method retrieves the settable values from the client manager
//  and applies them to the driver.  This method must be called each time
//  the properties are changed or a client changes its subscription status.
//
//  Parameters:
//
//  Return Value:
//      status
//
///////////////////////////////////////////////////////////////////////////
HRESULT CSensorDevice::ApplyUpdatedProperties(
    )
{
    FuncEntry();

    HRESULT hr = (m_fSensorInitialized == TRUE) ? S_OK : E_UNEXPECTED;

    if (SUCCEEDED(hr))
    {
        PROPVARIANT var;
        PropVariantInit(&var);
        
        // Update the report interval
        hr = m_pClientManager->GetArbitratedProperty(
            SENSOR_PROPERTY_CURRENT_REPORT_INTERVAL,
            &var);
        
        if (SUCCEEDED(hr))
        {
            // Update device with report interval
            hr = SetReportInterval(var.ulVal);

            if (SUCCEEDED(hr))
            {
                m_pReportManager->SetReportInterval(var.ulVal);
            }
        }
        
        if (SUCCEEDED(hr))
        {
            // Update the change sensitivity
            hr = m_pClientManager->GetArbitratedProperty(
                SENSOR_PROPERTY_CHANGE_SENSITIVITY,
                &var);

            if (SUCCEEDED(hr))
            {
                // Update device with change sensitivity
                hr = SetChangeSensitivity(&var);
            }
        }
        
        PropVariantClear(&var);
    }

    if (SUCCEEDED(hr))
    {
        DATA_UPDATE_MODE newMode = m_pClientManager->GetDataUpdateMode();

        if (newMode != m_DataUpdateMode)
        {
            Trace(
                TRACE_LEVEL_INFORMATION,
                "Data update mode has changed to %d",
                newMode);

            hr = SetDataUpdateMode(newMode);
        }
    }

    FuncExit();

    return hr;
}

///////////////////////////////////////////////////////////////////////////
//
//  CSensorDevice::ReportIntervalExpired
//
//  This method is called when the report interval has expired and new
//  data has been recieved.
//
//  Parameters:
//
//  Return Value:
//      none
//
///////////////////////////////////////////////////////////////////////////
HRESULT CSensorDevice::ReportIntervalExpired()
{
    FuncEntry();

    HRESULT hr = S_OK;
    
    // Create the event parameters collection if it doesn't exist
    ComPtr<IPortableDeviceValues> spEventParams;
    if (spEventParams == nullptr)
    {
        hr = CoCreateInstance(
            CLSID_PortableDeviceValues,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&spEventParams));
    }

    if (FAILED(hr))
    {
        Trace(
            TRACE_LEVEL_ERROR, 
            "Failed to CoCreateInstance for Event Parameters, %!HRESULT!", 
            hr);

        return 0;
    }

    if (SUCCEEDED(hr))
    {
        // Initialize the event parameters
        spEventParams->Clear();

        // Populate the event type
        hr = spEventParams->SetGuidValue(
            SENSOR_EVENT_PARAMETER_EVENT_ID, 
            SENSOR_EVENT_DATA_UPDATED);
    }

    if (SUCCEEDED(hr))
    {
        // Get the All the Data Field values
        // Populate the event parameters
        if (SUCCEEDED(hr)) {
            hr = GetAllDataFields(spEventParams.Get());
        }

        if (SUCCEEDED(hr) && HasStateChanged())
        {
            PROPVARIANT var;
            ULONG value = 0;
            SensorState currentState;

            PropVariantInit(&var);
            
            hr = GetProperty(SENSOR_PROPERTY_STATE, &var);

            if (SUCCEEDED(hr))
            {
                hr = PropVariantToUInt32(var, &value);
            }

            if (SUCCEEDED(hr))
            {
                currentState = (SensorState)value;

                Trace(
                    TRACE_LEVEL_INFORMATION,
                    "Posting state change, now %d",
                    currentState);

                // Post a state change event
                hr = m_pSensorDdi->PostStateChange(GetSensorObjectID(), currentState);
            }

            PropVariantClear(&var);
        }

        if( SUCCEEDED(hr) )
        {
            Trace(
                TRACE_LEVEL_VERBOSE,
                "Posting data event");

            hr = m_pSensorDdi->PostDataEvent(GetSensorObjectID(), spEventParams.Get());
        }
    }

    FuncExit();

    return hr;
}
