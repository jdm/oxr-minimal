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
#include <angle_windowsstore.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglplatform.h>

#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
constexpr const char* ProgramName = "BasicXrApp_win32";
#else
constexpr const char* ProgramName = "BasicXrApp_uwp";
#endif

extern "C" void run();

int __stdcall wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    const EGLint configAttributes[] = { EGL_RENDERABLE_TYPE,
                                   EGL_OPENGL_ES2_BIT,
                                   EGL_RED_SIZE,
                                   8,
                                   EGL_GREEN_SIZE,
                                   8,
                                   EGL_BLUE_SIZE,
                                   8,
                                   EGL_ALPHA_SIZE,
                                   8,
                                   EGL_DEPTH_SIZE,
                                   24,
                                   EGL_STENCIL_SIZE,
                                   8,
                                   EGL_NONE };

    const EGLint contextAttributes[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };

    // Based on Angle MS template.

    const EGLint defaultDisplayAttributes[] = {
        // These are the default display attributes, used to request ANGLE's D3D11
        // renderer.
        // eglInitialize will only succeed with these attributes if the hardware
        // supports D3D11 Feature Level 10_0+.
        EGL_PLATFORM_ANGLE_TYPE_ANGLE,
        EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,

        // EGL_EXPERIMENTAL_PRESENT_PATH_ANGLE is an optimization that
        // can have large performance benefits on mobile devices.
        /*EGL_EXPERIMENTAL_PRESENT_PATH_ANGLE,
        EGL_EXPERIMENTAL_PRESENT_PATH_FAST_ANGLE,*/

        /*EGL_PLATFORM_ANGLE_DEBUG_LAYERS_ENABLED_ANGLE,
        EGL_TRUE,*/

        // EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE is an option that
        // enables ANGLE to automatically call
        // the IDXGIDevice3::Trim method on behalf of the application when it gets
        // suspended.
        // Calling IDXGIDevice3::Trim when an application is suspended is a
        // Windows Store application certification
        // requirement.
        EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE,
        EGL_TRUE,
        EGL_NONE,
    };

    const EGLint fl9_3DisplayAttributes[] = {
        // These can be used to request ANGLE's D3D11 renderer, with D3D11 Feature
        // Level 9_3.
        // These attributes are used if the call to eglInitialize fails with the
        // default display attributes.
        EGL_PLATFORM_ANGLE_TYPE_ANGLE,
        EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,

        /*EGL_PLATFORM_ANGLE_DEBUG_LAYERS_ENABLED_ANGLE,
        EGL_TRUE,*/

        EGL_PLATFORM_ANGLE_MAX_VERSION_MAJOR_ANGLE,
        9,
        EGL_PLATFORM_ANGLE_MAX_VERSION_MINOR_ANGLE,
        3,
        /*EGL_EXPERIMENTAL_PRESENT_PATH_ANGLE,
        EGL_EXPERIMENTAL_PRESENT_PATH_FAST_ANGLE,*/
        EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE,
        EGL_TRUE,
        EGL_NONE,
    };

    const EGLint warpDisplayAttributes[] = {
        // These attributes can be used to request D3D11 WARP.
        // They are used if eglInitialize fails with both the default display
        // attributes and the 9_3 display attributes.
        EGL_PLATFORM_ANGLE_TYPE_ANGLE,
        EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,

        /*EGL_PLATFORM_ANGLE_DEBUG_LAYERS_ENABLED_ANGLE,
        EGL_TRUE,*/

        EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE,
        EGL_PLATFORM_ANGLE_DEVICE_TYPE_D3D_WARP_ANGLE,
        /*EGL_EXPERIMENTAL_PRESENT_PATH_ANGLE,
        EGL_EXPERIMENTAL_PRESENT_PATH_FAST_ANGLE,*/
        EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE,
        EGL_TRUE,
        EGL_NONE,
    };

    // eglGetPlatformDisplayEXT is an alternative to eglGetDisplay.
    // It allows us to pass in display attributes, used to configure D3D11.
    PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT =
        reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (!eglGetPlatformDisplayEXT) {
        throw winrt::hresult_error(
            E_FAIL, L"Failed to get function eglGetPlatformDisplayEXT");
    }

    //
    // To initialize the display, we make three sets of calls to
    // eglGetPlatformDisplayEXT and eglInitialize, with varying parameters passed
    // to eglGetPlatformDisplayEXT: 1) The first calls uses
    // "defaultDisplayAttributes" as a parameter. This corresponds to D3D11
    // Feature Level 10_0+. 2) If eglInitialize fails for step 1 (e.g. because
    // 10_0+ isn't supported by the default GPU), then we try again
    //    using "fl9_3DisplayAttributes". This corresponds to D3D11 Feature Level
    //    9_3.
    // 3) If eglInitialize fails for step 2 (e.g. because 9_3+ isn't supported by
    // the default GPU), then we try again
    //    using "warpDisplayAttributes".  This corresponds to D3D11 Feature Level
    //    11_0 on WARP, a D3D11 software rasterizer.
    //

    // This tries to initialize EGL to D3D11 Feature Level 10_0+. See above
    // comment for details.
    EGLDisplay mEglDisplay = eglGetPlatformDisplayEXT(
        EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, defaultDisplayAttributes);
    if (mEglDisplay == EGL_NO_DISPLAY) {
        throw winrt::hresult_error(E_FAIL, L"Failed to get EGL display");
    }

    if (eglInitialize(mEglDisplay, NULL, NULL) == EGL_FALSE) {
        // This tries to initialize EGL to D3D11 Feature Level 9_3, if 10_0+ is
        // unavailable (e.g. on some mobile devices).
        mEglDisplay = eglGetPlatformDisplayEXT(
            EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, fl9_3DisplayAttributes);
        if (mEglDisplay == EGL_NO_DISPLAY) {
            throw winrt::hresult_error(E_FAIL, L"Failed to get EGL display");
        }

        if (eglInitialize(mEglDisplay, NULL, NULL) == EGL_FALSE) {
            // This initializes EGL to D3D11 Feature Level 11_0 on WARP, if 9_3+ is
            // unavailable on the default GPU.
            mEglDisplay = eglGetPlatformDisplayEXT(
                EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, warpDisplayAttributes);
            if (mEglDisplay == EGL_NO_DISPLAY) {
                throw winrt::hresult_error(E_FAIL, L"Failed to get EGL display");
            }

            if (eglInitialize(mEglDisplay, NULL, NULL) == EGL_FALSE) {
                // If all of the calls to eglInitialize returned EGL_FALSE then an error
                // has occurred.
                throw winrt::hresult_error(E_FAIL, L"Failed to initialize EGL");
            }
        }
    }

    EGLint numConfigs = 0;
    EGLConfig config;
    if ((eglChooseConfig(mEglDisplay, configAttributes, &config, 1,
        &numConfigs) == EGL_FALSE) ||
        (numConfigs == 0)) {
        throw winrt::hresult_error(E_FAIL, L"Failed to choose first EGLConfig");
    }

    EGLContext context = eglCreateContext(mEglDisplay, config, EGL_NO_CONTEXT,
        contextAttributes);
    if (context == EGL_NO_CONTEXT) {
        throw winrt::hresult_error(E_FAIL, L"Failed to create EGL context");
    }

    EGLint attributes[] = {
        EGL_WIDTH, 1024,
        EGL_HEIGHT, 768,
        EGL_TEXTURE_FORMAT, EGL_TEXTURE_RGBA,
        EGL_TEXTURE_TARGET, EGL_TEXTURE_2D,
        EGL_NONE, 0, 0, 0,
    };

    EGLSurface surface = eglCreatePbufferSurface(mEglDisplay, config, attributes);
    eglMakeCurrent(mEglDisplay, surface, surface, context);

    run();
    /*try {
        auto graphics = sample::CreateCubeGraphics();
        auto program = sample::CreateOpenXrProgram(ProgramName, std::move(graphics));
        program->Run();
    } catch (const std::exception& ex) {
        DEBUG_PRINT("Unhandled Exception: %s\n", ex.what());
        return 1;
    } catch (...) {
        DEBUG_PRINT("Unhandled Exception\n");
        return 1;
    }*/
    return 0;
}
