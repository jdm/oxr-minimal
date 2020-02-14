//*********************************************************
//    Copyright (c) Microsoft. All rights reserved.
//
//    Apache 2.0 License
//
//    You may obtain a copy of the License at
//    http://www.apache.org/licenses/LICENSE-2.0
//
//    Unless required by applicable law or agreed to in writing, software
//    distributed under the License is distributed on an "AS IS" BASIS,
//    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
//    implied. See the License for the specific language governing
//    permissions and limitations under the License.
//
//*********************************************************

#include "pch.h"
#include "App.h"
#include <cstdlib>
#include <vector>

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
constexpr const char* ProgramName = "BasicXrApp_win32";
#else
constexpr const char* ProgramName = "BasicXrApp_uwp";
#endif

//extern "C" void run();
void run();

int __stdcall wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    run();
    return 0;
}


#include <openxr/openxr.h>
#include <openxr/openxr_reflection.h>

#include <string>

// Macro to generate stringify functions for OpenXR enumerations based data provided in openxr_reflection.h
// clang-format off
#define ENUM_CASE_STR(name, val) case name: return #name;

// Returns C string pointing to a string literal. Unknown values are returned as 'Unknown <type>'.
#define MAKE_TO_CSTRING_FUNC(enumType)                      \
    constexpr const char* ToCString(enumType e) noexcept {  \
        switch (e) {                                        \
            XR_LIST_ENUM_##enumType(ENUM_CASE_STR)          \
            default: return "Unknown " #enumType;           \
        }                                                   \
    }

// Returns a STL string. Unknown values are stringified as an integer.
#define MAKE_TO_STRING_FUNC(enumType)                  \
    inline std::string ToString(enumType e) {          \
        switch (e) {                                   \
            XR_LIST_ENUM_##enumType(ENUM_CASE_STR)     \
            default: return std::to_string(e);         \
        }                                              \
    }

#define MAKE_TO_STRING_FUNCS(enumType) \
    MAKE_TO_CSTRING_FUNC(enumType) \
    MAKE_TO_STRING_FUNC(enumType)
// clang-format on

namespace xr {
    MAKE_TO_STRING_FUNCS(XrReferenceSpaceType);
    MAKE_TO_STRING_FUNCS(XrViewConfigurationType);
    MAKE_TO_STRING_FUNCS(XrEnvironmentBlendMode);
    MAKE_TO_STRING_FUNCS(XrSessionState);
    MAKE_TO_STRING_FUNCS(XrResult);
    MAKE_TO_STRING_FUNCS(XrStructureType);
    MAKE_TO_STRING_FUNCS(XrFormFactor);
    MAKE_TO_STRING_FUNCS(XrEyeVisibility);
    MAKE_TO_STRING_FUNCS(XrObjectType);
    MAKE_TO_STRING_FUNCS(XrActionType);
} // namespace xr

#define CHECK_XRCMD(cmd) xr::detail::_CheckXrResult(cmd, #cmd, FILE_AND_LINE);
#define CHECK_XRRESULT(res, cmdStr) xr::detail::_CheckXrResult(res, cmdStr, FILE_AND_LINE);

#define CHECK_HRCMD(cmd) xr::detail::_CheckHResult(cmd, #cmd, FILE_AND_LINE);
#define CHECK_HRESULT(res, cmdStr) xr::detail::_CheckHResult(res, cmdStr, FILE_AND_LINE);

#define DEBUG_PRINT(...) ::OutputDebugStringA((xr::detail::_Fmt(__VA_ARGS__) + "\n").c_str())

namespace xr::detail {
#define CHK_STRINGIFY(x) #x
#define TOSTRING(x) CHK_STRINGIFY(x)
#define FILE_AND_LINE __FILE__ ":" TOSTRING(__LINE__)

    inline std::string _Fmt(const char* fmt, ...) {
        va_list vl;
        va_start(vl, fmt);
        int size = std::vsnprintf(nullptr, 0, fmt, vl);
        va_end(vl);

        if (size != -1) {
            std::unique_ptr<char[]> buffer(new char[size + 1]);

            va_start(vl, fmt);
            size = std::vsnprintf(buffer.get(), size + 1, fmt, vl);
            va_end(vl);
            if (size != -1) {
                return std::string(buffer.get(), size);
            }
        }

        throw std::runtime_error("Unexpected vsnprintf failure");
    }

    [[noreturn]] inline void _Throw(std::string failureMessage, const char* originator = nullptr, const char* sourceLocation = nullptr) {
        if (originator != nullptr) {
            failureMessage += _Fmt("\n    Origin: %s", originator);
        }
        if (sourceLocation != nullptr) {
            failureMessage += _Fmt("\n    Source: %s", sourceLocation);
        }

        throw std::logic_error(failureMessage);
    }

#define THROW(msg) xr::detail::_Throw(msg, nullptr, FILE_AND_LINE);
#define CHECK(exp)                                                   \
    {                                                                \
        if (!(exp)) {                                                \
            xr::detail::_Throw("Check failed", #exp, FILE_AND_LINE); \
        }                                                            \
    }
#define CHECK_MSG(exp, msg)                               \
    {                                                     \
        if (!(exp)) {                                     \
            xr::detail::_Throw(msg, #exp, FILE_AND_LINE); \
        }                                                 \
    }

    [[noreturn]] inline void _ThrowXrResult(XrResult res, const char* originator = nullptr, const char* sourceLocation = nullptr) {
        xr::detail::_Throw(_Fmt("XrResult failure [%s]", xr::ToCString(res)), originator, sourceLocation);
    }

    inline XrResult _CheckXrResult(XrResult res, const char* originator = nullptr, const char* sourceLocation = nullptr) {
        if (XR_FAILED(res)) {
            xr::detail::_ThrowXrResult(res, originator, sourceLocation);
        }

        return res;
    }

    [[noreturn]] inline void _ThrowHResult(HRESULT hr, const char* originator = nullptr, const char* sourceLocation = nullptr) {
        xr::detail::_Throw(xr::detail::_Fmt("HRESULT failure [%x]", hr), originator, sourceLocation);
    }

    inline HRESULT _CheckHResult(HRESULT hr, const char* originator = nullptr, const char* sourceLocation = nullptr) {
        if (FAILED(hr)) {
            xr::detail::_ThrowHResult(hr, originator, sourceLocation);
        }

        return hr;
    }
} // namespace xr::detail


namespace xr {
    template <typename HandleType, XrResult(XRAPI_PTR* DestroyFunction)(HandleType)>
    class UniqueHandle {
    public:
        UniqueHandle() = default;
        UniqueHandle(const UniqueHandle&) = delete;
        UniqueHandle(UniqueHandle&& other) noexcept {
            *this = std::move(other);
        }

        ~UniqueHandle() noexcept {
            Reset();
        }

        UniqueHandle& operator=(const UniqueHandle&) = delete;
        UniqueHandle& operator=(UniqueHandle&& other) noexcept {
            if (m_handle != other.m_handle) {
                Reset();

                m_handle = other.m_handle;
                other.m_handle = XR_NULL_HANDLE;
            }
            return *this;
        }

        HandleType Get() const noexcept {
            return m_handle;
        }

        HandleType* Put() noexcept {
            Reset();
            return &m_handle;
        }

        void Reset(HandleType newHandle = XR_NULL_HANDLE) noexcept {
            if (m_handle != XR_NULL_HANDLE) {
                DestroyFunction(m_handle);
            }
            m_handle = newHandle;
        }

    private:
        HandleType m_handle{XR_NULL_HANDLE};
    };

    using ActionHandle = UniqueHandle<XrAction, xrDestroyAction>;
    using ActionSetHandle = UniqueHandle<XrActionSet, xrDestroyActionSet>;
    using InstanceHandle = UniqueHandle<XrInstance, xrDestroyInstance>;
    using SessionHandle = UniqueHandle<XrSession, xrDestroySession>;
    using SpaceHandle = UniqueHandle<XrSpace, xrDestroySpace>;
    using SwapchainHandle = UniqueHandle<XrSwapchain, xrDestroySwapchain>;
    using SpatialAnchorHandle = UniqueHandle<XrSpatialAnchorMSFT, xrDestroySpatialAnchorMSFT>;
} // namespace xr

namespace xr {
    namespace math {
        namespace Pose {
            constexpr XrPosef Identity() {
                return { {0, 0, 0, 1}, {0, 0, 0} };
            }
        };
        struct NearFar {
            float Near;
            float Far;
        };
        struct ViewProjection {
            XrPosef Pose;
            XrFovf Fov;
            NearFar NearFar;
        };
    }
}

bool TryReadNextEvent(XrEventDataBuffer* buffer, const xr::InstanceHandle& m_instance) {
    // Reset buffer header for every xrPollEvent function call.
    *buffer = { XR_TYPE_EVENT_DATA_BUFFER };
    const XrResult xr = CHECK_XRCMD(xrPollEvent(m_instance.Get(), buffer));
    if (xr == XR_EVENT_UNAVAILABLE) {
        return false;
    }
    else {
        return true;
    }
}
void run() {
            constexpr static XrFormFactor m_formFactor{XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY};
        constexpr static XrViewConfigurationType m_primaryViewConfigType{XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO};
        constexpr static uint32_t m_stereoViewCount = 2; // PRIMARY_STEREO view configuration always has 2 views

        xr::InstanceHandle m_instance;
        xr::SessionHandle m_session;
        uint64_t m_systemId{XR_NULL_SYSTEM_ID};
        xr::SpaceHandle m_sceneSpace;
        XrReferenceSpaceType m_sceneSpaceType{};

        XrEnvironmentBlendMode m_environmentBlendMode{};
        
        using namespace xr::math;
        NearFar m_nearFar{};

        struct SwapchainD3D11 {
            xr::SwapchainHandle Handle;
            DXGI_FORMAT Format{ DXGI_FORMAT_UNKNOWN };
            uint32_t Width{ 0 };
            uint32_t Height{ 0 };
            uint32_t ArraySize{ 0 };
            std::vector<XrSwapchainImageD3D11KHR> Images;
        };

        struct RenderResources {
            XrViewState ViewState{ XR_TYPE_VIEW_STATE };
            std::vector<XrView> Views;
            std::vector<XrViewConfigurationView> ConfigViews;
            SwapchainD3D11 ColorSwapchain;
            //SwapchainD3D11 DepthSwapchain;
            std::vector<XrCompositionLayerProjectionView> ProjectionLayerViews;
            //std::vector<XrCompositionLayerDepthInfoKHR> DepthInfoViews;
        };

        std::unique_ptr<RenderResources> m_renderResources{};

        bool m_sessionRunning{ false };
        XrSessionState m_sessionState{ XR_SESSION_STATE_UNKNOWN };
        


        const std::vector<const char*> enabledExtensions{ XR_KHR_D3D11_ENABLE_EXTENSION_NAME };
    
    // Create the instance with desired extensions.
    XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
    createInfo.enabledExtensionCount = (uint32_t)enabledExtensions.size();
    createInfo.enabledExtensionNames = enabledExtensions.data();

    createInfo.applicationInfo = {"", 1, "OpenXR Sample", 1, XR_CURRENT_API_VERSION};
    strcpy_s(createInfo.applicationInfo.applicationName, "firefox.reality");
    CHECK_XRCMD(xrCreateInstance(&createInfo, m_instance.Put()));


    CHECK(m_instance.Get() != XR_NULL_HANDLE);
    CHECK(m_systemId == XR_NULL_SYSTEM_ID);

    XrSystemGetInfo systemInfo{ XR_TYPE_SYSTEM_GET_INFO };
    systemInfo.formFactor = m_formFactor;
    while (true) {
        XrResult result = xrGetSystem(m_instance.Get(), &systemInfo, &m_systemId);
        if (SUCCEEDED(result)) {
            break;
        }
        else if (result == XR_ERROR_FORM_FACTOR_UNAVAILABLE) {
            DEBUG_PRINT("No headset detected.  Trying again in one second...");
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(1s);
        }
        else {
            CHECK_XRRESULT(result, "xrGetSystem");
        }
    };

    // Choose an environment blend mode.
    {
        // Query the list of supported environment blend modes for the current system
        uint32_t count;
        CHECK_XRCMD(xrEnumerateEnvironmentBlendModes(m_instance.Get(), m_systemId, m_primaryViewConfigType, 0, &count, nullptr));
        CHECK(count > 0); // A system must support at least one environment blend mode.

        std::vector<XrEnvironmentBlendMode> environmentBlendModes(count);
        CHECK_XRCMD(xrEnumerateEnvironmentBlendModes(
            m_instance.Get(), m_systemId, m_primaryViewConfigType, count, &count, environmentBlendModes.data()));

        // This sample supports all modes, pick the system's preferred one.
        m_environmentBlendMode = environmentBlendModes[0];
    }

    // Choose a reasonable depth range can help improve hologram visual quality.
    // Use reversed Z (near > far) for more uniformed Z resolution.
    m_nearFar = { 20.f, 0.1f };


            CHECK(m_instance.Get() != XR_NULL_HANDLE);
            CHECK(m_systemId != XR_NULL_SYSTEM_ID);
            CHECK(m_session.Get() == XR_NULL_HANDLE);

            // Create the D3D11 device for the adapter associated with the system.
            XrGraphicsRequirementsD3D11KHR graphicsRequirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
            CHECK_XRCMD(xrGetD3D11GraphicsRequirementsKHR(m_instance.Get(), m_systemId, &graphicsRequirements));

            // Create a list of feature levels which are both supported by the OpenXR runtime and this application.
            std::vector<D3D_FEATURE_LEVEL> featureLevels = {D3D_FEATURE_LEVEL_12_1,
                                                            D3D_FEATURE_LEVEL_12_0,
                                                            D3D_FEATURE_LEVEL_11_1,
                                                            D3D_FEATURE_LEVEL_11_0,
                                                            D3D_FEATURE_LEVEL_10_1,
                                                            D3D_FEATURE_LEVEL_10_0};
            featureLevels.erase(std::remove_if(featureLevels.begin(),
                                               featureLevels.end(),
                                               [&](D3D_FEATURE_LEVEL fl) { return fl < graphicsRequirements.minFeatureLevel; }),
                                featureLevels.end());
            CHECK_MSG(featureLevels.size() != 0, "Unsupported minimum feature level!");



            // Create the DXGI factory.
            winrt::com_ptr<IDXGIFactory1> dxgiFactory;
            CHECK_HRCMD(CreateDXGIFactory1(winrt::guid_of<IDXGIFactory1>(), dxgiFactory.put_void()));

            winrt::com_ptr<IDXGIAdapter1> dxgiAdapter;
            for (UINT adapterIndex = 0;; adapterIndex++) {
                // EnumAdapters1 will fail with DXGI_ERROR_NOT_FOUND when there are no more adapters to enumerate.
                CHECK_HRCMD(dxgiFactory->EnumAdapters1(adapterIndex, dxgiAdapter.put()));

                DXGI_ADAPTER_DESC1 adapterDesc;
                CHECK_HRCMD(dxgiAdapter->GetDesc1(&adapterDesc));
                if (memcmp(&adapterDesc.AdapterLuid, &graphicsRequirements.adapterLuid, sizeof(graphicsRequirements.adapterLuid)) == 0) {
                    DEBUG_PRINT("Using graphics adapter %ws", adapterDesc.Description);
                    break;
                }
            }


            winrt::com_ptr<ID3D11Device> m_device;
            winrt::com_ptr<ID3D11DeviceContext> m_deviceContext;

            UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

#ifdef _DEBUG
            creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

            // Create the Direct3D 11 API device object and a corresponding context.
            D3D_DRIVER_TYPE driverType = dxgiAdapter == nullptr ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_UNKNOWN;

        TryAgain:
            const HRESULT hr = D3D11CreateDevice(dxgiAdapter.get(),
                driverType,
                0,
                creationFlags,
                featureLevels.data(),
                (UINT)featureLevels.size(),
                D3D11_SDK_VERSION,
                m_device.put(),
                nullptr,
                m_deviceContext.put());

            if (FAILED(hr)) {
                // If initialization failed, it may be because device debugging isn't supprted, so retry without that.
                if ((creationFlags & D3D11_CREATE_DEVICE_DEBUG) && (hr == DXGI_ERROR_SDK_COMPONENT_MISSING)) {
                    creationFlags &= ~D3D11_CREATE_DEVICE_DEBUG;
                    goto TryAgain;
                }

                // If the initialization still fails, fall back to the WARP device.
                // For more information on WARP, see: http://go.microsoft.com/fwlink/?LinkId=286690
                if (driverType != D3D_DRIVER_TYPE_WARP) {
                    driverType = D3D_DRIVER_TYPE_WARP;
                    goto TryAgain;
                }
            }


            //ID3D11Device* device = m_graphicsPlugin->InitializeDevice(graphicsRequirements.adapterLuid, featureLevels);

            XrGraphicsBindingD3D11KHR graphicsBinding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
            graphicsBinding.device = m_device.get();

            XrSessionCreateInfo createInfo2{XR_TYPE_SESSION_CREATE_INFO};
            createInfo2.next = &graphicsBinding;
            createInfo2.systemId = m_systemId;
            CHECK_XRCMD(xrCreateSession(m_instance.Get(), &createInfo2, m_session.Put()));

            /*XrSessionActionSetsAttachInfo attachInfo{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
            std::vector<XrActionSet> actionSets = {m_actionSet.Get()};
            attachInfo.countActionSets = (uint32_t)actionSets.size();
            attachInfo.actionSets = actionSets.data();
            CHECK_XRCMD(xrAttachSessionActionSets(m_session.Get(), &attachInfo));*/

            CHECK(m_session.Get() != XR_NULL_HANDLE);

            XrReferenceSpaceCreateInfo spaceCreateInfo{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
            m_sceneSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
            spaceCreateInfo.referenceSpaceType = m_sceneSpaceType;
            spaceCreateInfo.poseInReferenceSpace = xr::math::Pose::Identity();
            CHECK_XRCMD(xrCreateReferenceSpace(m_session.Get(), &spaceCreateInfo, m_sceneSpace.Put()));

            
            
            
            
            CHECK(m_session.Get() != XR_NULL_HANDLE);
            CHECK(m_renderResources == nullptr);

            m_renderResources = std::make_unique<RenderResources>();

            // Read graphics properties for preferred swapchain length and logging.
            XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
            CHECK_XRCMD(xrGetSystemProperties(m_instance.Get(), m_systemId, &systemProperties));


            CHECK(m_session.Get() != XR_NULL_HANDLE);

            // Query runtime preferred swapchain formats.
            uint32_t swapchainFormatCount;
            CHECK_XRCMD(xrEnumerateSwapchainFormats(m_session.Get(), 0, &swapchainFormatCount, nullptr));

            std::vector<int64_t> swapchainFormats(swapchainFormatCount);
            CHECK_XRCMD(xrEnumerateSwapchainFormats(
                m_session.Get(), (uint32_t)swapchainFormats.size(), &swapchainFormatCount, swapchainFormats.data()));

            const static std::vector<DXGI_FORMAT> SupportedColorFormats = {
                DXGI_FORMAT_R8G8B8A8_UNORM,
                DXGI_FORMAT_B8G8R8A8_UNORM,
                DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
                DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
            };

            // Choose the first runtime preferred format that this app supports.
            auto SelectPixelFormat = [](const std::vector<int64_t>& runtimePreferredFormats,
                                        const std::vector<DXGI_FORMAT>& applicationSupportedFormats) {
                auto found = std::find_first_of(std::begin(runtimePreferredFormats),
                                                std::end(runtimePreferredFormats),
                                                std::begin(applicationSupportedFormats),
                                                std::end(applicationSupportedFormats));
                if (found == std::end(runtimePreferredFormats)) {
                    THROW("No runtime swapchain format is supported.");
                }
                return (DXGI_FORMAT)*found;
            };

            DXGI_FORMAT colorSwapchainFormat = SelectPixelFormat(swapchainFormats, SupportedColorFormats);

            // Query and cache view configuration views.
            uint32_t viewCount;
            CHECK_XRCMD(xrEnumerateViewConfigurationViews(m_instance.Get(), m_systemId, m_primaryViewConfigType, 0, &viewCount, nullptr));
            CHECK(viewCount == m_stereoViewCount);

            m_renderResources->ConfigViews.resize(viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
            CHECK_XRCMD(xrEnumerateViewConfigurationViews(
                m_instance.Get(), m_systemId, m_primaryViewConfigType, viewCount, &viewCount, m_renderResources->ConfigViews.data()));

            // Using texture array for better performance, but requiring left/right views have identical sizes.
            const XrViewConfigurationView& view = m_renderResources->ConfigViews[0];
            CHECK(m_renderResources->ConfigViews[0].recommendedImageRectWidth ==
                  m_renderResources->ConfigViews[1].recommendedImageRectWidth);
            CHECK(m_renderResources->ConfigViews[0].recommendedImageRectHeight ==
                  m_renderResources->ConfigViews[1].recommendedImageRectHeight);
            CHECK(m_renderResources->ConfigViews[0].recommendedSwapchainSampleCount ==
                  m_renderResources->ConfigViews[1].recommendedSwapchainSampleCount);

            // Use recommended rendering parameters for a balance between quality and performance
            const uint32_t imageRectWidth = view.recommendedImageRectWidth;
            const uint32_t imageRectHeight = view.recommendedImageRectHeight;
            const uint32_t swapchainSampleCount = view.recommendedSwapchainSampleCount;

            // Create swapchains with texture array for color and depth images.
            // The texture array has the size of viewCount, and they are rendered in a single pass using VPRT.
            const uint32_t textureArraySize = viewCount;

            SwapchainD3D11 swapchain;
            swapchain.Format = colorSwapchainFormat;
            swapchain.Width = imageRectWidth;
            swapchain.Height = imageRectHeight;
            swapchain.ArraySize = textureArraySize;

            XrSwapchainCreateInfo swapchainCreateInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
            swapchainCreateInfo.arraySize = textureArraySize;
            swapchainCreateInfo.format = colorSwapchainFormat;
            swapchainCreateInfo.width = imageRectWidth;
            swapchainCreateInfo.height = imageRectHeight;
            swapchainCreateInfo.mipCount = 1;
            swapchainCreateInfo.faceCount = 1;
            swapchainCreateInfo.sampleCount = swapchainSampleCount;
            swapchainCreateInfo.createFlags = 0;
            swapchainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;

            CHECK_XRCMD(xrCreateSwapchain(m_session.Get(), &swapchainCreateInfo, swapchain.Handle.Put()));

            uint32_t chainLength;
            CHECK_XRCMD(xrEnumerateSwapchainImages(swapchain.Handle.Get(), 0, &chainLength, nullptr));

            swapchain.Images.resize(chainLength, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
            CHECK_XRCMD(xrEnumerateSwapchainImages(swapchain.Handle.Get(),
                                                   (uint32_t)swapchain.Images.size(),
                                                   &chainLength,
                                                   reinterpret_cast<XrSwapchainImageBaseHeader*>(swapchain.Images.data())));


            m_renderResources->ColorSwapchain = std::move(swapchain);

            // Preallocate view buffers for xrLocateViews later inside frame loop.
            m_renderResources->Views.resize(viewCount, {XR_TYPE_VIEW});

            while (true) {
                XrEventDataBuffer buffer{ XR_TYPE_EVENT_DATA_BUFFER };
                XrEventDataBaseHeader* header = reinterpret_cast<XrEventDataBaseHeader*>(&buffer);


                // Process all pending messages.
                while (TryReadNextEvent(&buffer, m_instance)) {
                    bool exitRenderLoop = false;
                    switch (header->type) {
                    case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
                        exitRenderLoop = true;
                        break;
                    }
                    case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                        const auto stateEvent = *reinterpret_cast<const XrEventDataSessionStateChanged*>(header);
                        CHECK(m_session.Get() != XR_NULL_HANDLE && m_session.Get() == stateEvent.session);
                        m_sessionState = stateEvent.state;
                        switch (m_sessionState) {
                        case XR_SESSION_STATE_READY: {
                            CHECK(m_session.Get() != XR_NULL_HANDLE);
                            XrSessionBeginInfo sessionBeginInfo{ XR_TYPE_SESSION_BEGIN_INFO };
                            sessionBeginInfo.primaryViewConfigurationType = m_primaryViewConfigType;
                            CHECK_XRCMD(xrBeginSession(m_session.Get(), &sessionBeginInfo));
                            m_sessionRunning = true;
                            break;
                        }
                        case XR_SESSION_STATE_STOPPING: {
                            m_sessionRunning = false;
                            CHECK_XRCMD(xrEndSession(m_session.Get()))
                                break;
                        }
                        case XR_SESSION_STATE_EXITING: {
                            // Do not attempt to restart because user closed this session.
                            exitRenderLoop = true;
                            break;
                        }
                        case XR_SESSION_STATE_LOSS_PENDING: {
                            // Poll for a new systemId
                            exitRenderLoop = true;
                            break;
                        }
                        }
                        break;
                    }
                    case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
                    case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
                    default: {
                        DEBUG_PRINT("Ignoring event type %d", header->type);
                        break;
                    }
                    }
                    if (exitRenderLoop) {
                        break;
                    }

                    if (m_sessionRunning) {
                        CHECK(m_session.Get() != XR_NULL_HANDLE);

                        XrFrameWaitInfo frameWaitInfo{ XR_TYPE_FRAME_WAIT_INFO };
                        XrFrameState frameState{ XR_TYPE_FRAME_STATE };
                        CHECK_XRCMD(xrWaitFrame(m_session.Get(), &frameWaitInfo, &frameState));

                        XrFrameBeginInfo frameBeginInfo{ XR_TYPE_FRAME_BEGIN_INFO };
                        CHECK_XRCMD(xrBeginFrame(m_session.Get(), &frameBeginInfo));

                        // EndFrame can submit mutiple layers
                        std::vector<XrCompositionLayerBaseHeader*> layers;

                        // The projection layer consists of projection layer views.
                        XrCompositionLayerProjection layer{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };

                        // Inform the runtime to consider alpha channel during composition
                        // The primary display on Hololens has additive environment blend mode. It will ignore alpha channel.
                        // But mixed reality capture has alpha blend mode display and use alpha channel to blend content to environment.
                        layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;

                        // Only render when session is visible. otherwise submit zero layers
                        if (frameState.shouldRender) {
                            // First update the viewState and views using latest predicted display time.
                            {
                                XrViewLocateInfo viewLocateInfo{ XR_TYPE_VIEW_LOCATE_INFO };
                                viewLocateInfo.viewConfigurationType = m_primaryViewConfigType;
                                viewLocateInfo.displayTime = frameState.predictedDisplayTime;
                                viewLocateInfo.space = m_sceneSpace.Get();

                                // The output view count of xrLocateViews is always same as xrEnumerateViewConfigurationViews
                                // Therefore Views can be preallocated and avoid two call idiom here.
                                uint32_t viewCapacityInput = (uint32_t)m_renderResources->Views.size();
                                uint32_t viewCountOutput;
                                CHECK_XRCMD(xrLocateViews(m_session.Get(),
                                    &viewLocateInfo,
                                    &m_renderResources->ViewState,
                                    viewCapacityInput,
                                    &viewCountOutput,
                                    m_renderResources->Views.data()));

                                CHECK(viewCountOutput == viewCapacityInput);
                                CHECK(viewCountOutput == m_renderResources->ConfigViews.size());
                                CHECK(viewCountOutput == m_renderResources->ColorSwapchain.ArraySize);
                            }

                            const uint32_t viewCount = (uint32_t)m_renderResources->ConfigViews.size();
                            m_renderResources->ProjectionLayerViews.resize(viewCount);
                            const SwapchainD3D11& colorSwapchain = m_renderResources->ColorSwapchain;
                            // Use the full range of recommended image size to achieve optimum resolution
                            const XrRect2Di imageRect = { {0, 0}, {(int32_t)colorSwapchain.Width, (int32_t)colorSwapchain.Height} };

                            uint32_t colorSwapchainImageIndex;
                            XrSwapchainImageAcquireInfo acquireInfo{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
                            CHECK_XRCMD(xrAcquireSwapchainImage(colorSwapchain.Handle.Get(), &acquireInfo, &colorSwapchainImageIndex));

                            XrSwapchainImageWaitInfo waitInfo{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
                            waitInfo.timeout = XR_INFINITE_DURATION;
                            CHECK_XRCMD(xrWaitSwapchainImage(colorSwapchain.Handle.Get(), &waitInfo));

                            // Prepare rendering parameters of each view for swapchain texture arrays
                            std::vector<xr::math::ViewProjection> viewProjections(viewCount);
                            for (uint32_t i = 0; i < viewCount; i++) {
                                viewProjections[i] = { m_renderResources->Views[i].pose, m_renderResources->Views[i].fov, m_nearFar };

                                m_renderResources->ProjectionLayerViews[i] = { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
                                m_renderResources->ProjectionLayerViews[i].pose = m_renderResources->Views[i].pose;
                                m_renderResources->ProjectionLayerViews[i].fov = m_renderResources->Views[i].fov;
                                m_renderResources->ProjectionLayerViews[i].subImage.swapchain = colorSwapchain.Handle.Get();
                                m_renderResources->ProjectionLayerViews[i].subImage.imageRect = imageRect;
                                m_renderResources->ProjectionLayerViews[i].subImage.imageArrayIndex = i;

                            }

                            std::vector<char> pixels;
                            int byteLen = imageRect.extent.width * imageRect.extent.height * 4;
                            pixels.resize(byteLen);
                            memset(pixels.data(), 0xFF, byteLen);
                            D3D11_SUBRESOURCE_DATA data[2] = {
                                {pixels.data(), imageRect.extent.width * 4, byteLen},
                                {pixels.data(), imageRect.extent.width * 4, byteLen}
                            };

                            D3D11_TEXTURE2D_DESC desc;
                            desc.Width = imageRect.extent.width;
                            desc.Height = imageRect.extent.height;
                            desc.Format = colorSwapchainFormat;
                            desc.MipLevels = 1;
                            desc.ArraySize = 2;
                            desc.SampleDesc = DXGI_SAMPLE_DESC{ 1, 0 };
                            desc.Usage = D3D11_USAGE_DEFAULT;
                            desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
                            desc.CPUAccessFlags = 0;
                            desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

                            winrt::com_ptr<ID3D11Texture2D> solidTexture;
                            CHECK_HRCMD(m_device->CreateTexture2D(&desc, &data[0], solidTexture.put()));

                            m_deviceContext->CopyResource(colorSwapchain.Images[colorSwapchainImageIndex].texture, solidTexture.get());

                            XrSwapchainImageReleaseInfo releaseInfo{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
                            CHECK_XRCMD(xrReleaseSwapchainImage(colorSwapchain.Handle.Get(), &releaseInfo));

                            layer.space = m_sceneSpace.Get();
                            layer.viewCount = (uint32_t)m_renderResources->ProjectionLayerViews.size();
                            layer.views = m_renderResources->ProjectionLayerViews.data();
                            layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer));

                            /*// Then render projection layer into each view.
                            if (RenderLayer(frameState.predictedDisplayTime, layer)) {
                                layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer));
                            }*/
                        }

                        // Submit the composition layers for the predicted display time.
                        XrFrameEndInfo frameEndInfo{ XR_TYPE_FRAME_END_INFO };
                        frameEndInfo.displayTime = frameState.predictedDisplayTime;
                        frameEndInfo.environmentBlendMode = m_environmentBlendMode;
                        frameEndInfo.layerCount = (uint32_t)layers.size();
                        frameEndInfo.layers = layers.data();
                        CHECK_XRCMD(xrEndFrame(m_session.Get(), &frameEndInfo));

                    } else {
                        // Throttle loop since xrWaitFrame won't be called.
                        using namespace std::chrono_literals;
                        std::this_thread::sleep_for(250ms);
                    }
                }

            }
}

