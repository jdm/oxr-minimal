use std::ptr;
use std::mem;

extern "C" {
    fn OutputDebugStringA(s: *const u8);
}

fn debug(s: &str) {
    let s = format!("{}\n", s);
    let s = std::ffi::CString::new(s).unwrap();
    unsafe {
        OutputDebugStringA(s.as_ptr() as *const _);
    }
}

fn do_redirect_stdout_stderr() -> Result<(), ()> {
    use std::thread;
    use winapi::shared;
    use winapi::um::debugapi;
    use winapi::um::handleapi;
    use winapi::um::minwinbase;
    use winapi::um::namedpipeapi;
    use winapi::um::processenv;
    use winapi::um::winbase;
    use winapi::um::winnt;

    let mut h_read_pipe: winnt::HANDLE = handleapi::INVALID_HANDLE_VALUE;
    let mut h_write_pipe: winnt::HANDLE = handleapi::INVALID_HANDLE_VALUE;
    let mut secattr: minwinbase::SECURITY_ATTRIBUTES = unsafe { std::mem::zeroed() };
    const BUF_LENGTH: usize = 1024;

    secattr.nLength = std::mem::size_of::<minwinbase::SECURITY_ATTRIBUTES>() as u32;
    secattr.bInheritHandle = shared::minwindef::TRUE;
    secattr.lpSecurityDescriptor = shared::ntdef::NULL;

    unsafe {
        if namedpipeapi::CreatePipe(
            &mut h_read_pipe,
            &mut h_write_pipe,
            &mut secattr,
            BUF_LENGTH as u32,
        ) == 0
        {
            return Err(());
        }

        if processenv::SetStdHandle(winbase::STD_OUTPUT_HANDLE, h_write_pipe) == 0 ||
            processenv::SetStdHandle(winbase::STD_ERROR_HANDLE, h_write_pipe) == 0
        {
            return Err(());
        }

        if handleapi::SetHandleInformation(
            h_read_pipe,
            winbase::HANDLE_FLAG_INHERIT,
            winbase::HANDLE_FLAG_INHERIT,
        ) == 0 ||
            handleapi::SetHandleInformation(
                h_write_pipe,
                winbase::HANDLE_FLAG_INHERIT,
                winbase::HANDLE_FLAG_INHERIT,
            ) == 0
        {
            return Err(());
        }

        let h_read_pipe_fd = libc::open_osfhandle(h_read_pipe as libc::intptr_t, libc::O_RDONLY);
        let h_write_pipe_fd = libc::open_osfhandle(h_write_pipe as libc::intptr_t, libc::O_WRONLY);

        if h_read_pipe_fd == -1 || h_write_pipe_fd == -1 {
            return Err(());
        }

        // 0 indicates success.
        if libc::dup2(h_write_pipe_fd, 1) != 0 || libc::dup2(h_write_pipe_fd, 2) != 0 {
            return Err(());
        }

        // If SetStdHandle(winbase::STD_OUTPUT_HANDLE, hWritePipe) is not called prior,
        // this will fail.  GetStdHandle() is used to make certain "servo" has the stdout
        // file descriptor associated.
        let h_stdout = processenv::GetStdHandle(winbase::STD_OUTPUT_HANDLE);
        if h_stdout == handleapi::INVALID_HANDLE_VALUE || h_stdout == shared::ntdef::NULL {
            return Err(());
        }

        // If SetStdHandle(winbase::STD_ERROR_HANDLE, hWritePipe) is not called prior,
        // this will fail.  GetStdHandle() is used to make certain "servo" has the stderr
        // file descriptor associated.
        let h_stderr = processenv::GetStdHandle(winbase::STD_ERROR_HANDLE);
        if h_stderr == handleapi::INVALID_HANDLE_VALUE || h_stderr == shared::ntdef::NULL {
            return Err(());
        }

        // Spawn a thread.  The thread will redirect all STDOUT and STDERR messages
        // to OutputDebugString()
        let _handler = thread::spawn(move || {
            loop {
                let mut read_buf: [i8; BUF_LENGTH] = [0; BUF_LENGTH];

                let result = libc::read(
                    h_read_pipe_fd,
                    read_buf.as_mut_ptr() as *mut _,
                    read_buf.len() as u32 - 1,
                );

                if result == -1 {
                    break;
                }

                // Write to Debug port.
                debugapi::OutputDebugStringA(read_buf.as_mut_ptr() as winnt::LPSTR);
            }
        });
    }

    Ok(())
}

#[no_mangle]
pub extern "C" fn run() {
    std::panic::set_hook(Box::new(|info| {
        let msg = match info.payload().downcast_ref::<&'static str>() {
            Some(s) => *s,
            None => match info.payload().downcast_ref::<String>() {
                Some(s) => &**s,
                None => "Box<Any>",
            },
        };
        let current_thread = std::thread::current();
        let name = current_thread.name().unwrap_or("<unnamed>");
        if let Some(location) = info.location() {
            eprintln!("{} (thread {}, at {}:{})", msg, name, location.file(), location.line());
        } else {
            eprintln!("{} (thread {})", msg, name);
        };
    }));
    
    /*stderrlog::StdErrLog::new().modules(
        &["webxr".to_string(), "webxr-api".to_string(), "webxr_api".to_string()]
    ).verbosity(99).color(stderrlog::ColorChoice::Never).init().unwrap();*/
    
    debug("running");

    let _ = do_redirect_stdout_stderr();
    
    let entry = Entry::load().unwrap();
    run2(&entry);
}

use openxr::d3d::{Requirements, SessionCreateInfo, D3D11};
use openxr::sys::platform::{ID3D11Device};
use openxr::Graphics;
use openxr::{
    self, ApplicationInfo, CompositionLayerFlags,
    CompositionLayerProjection, Entry, EnvironmentBlendMode, ExtensionSet, Extent2Di, FormFactor,
    FrameState, FrameStream, FrameWaiter, Instance, Posef, Quaternionf, ReferenceSpaceType,
    Session, Space, Swapchain, SwapchainCreateFlags, SwapchainCreateInfo, SwapchainUsageFlags,
    Vector3f, ViewConfigurationType,
};

use winapi::shared::dxgi;
use winapi::shared::dxgiformat;
use winapi::shared::dxgitype;
use winapi::shared::winerror::{DXGI_ERROR_NOT_FOUND, S_OK};
use winapi::um::d3d11::{self, ID3D11DeviceContext};
use winapi::um::d3dcommon::*;
use winapi::Interface;
use wio::com::ComPtr;

fn create_instance(entry: &Entry) -> Instance {
    let app_info = ApplicationInfo {
        application_name: "firefox.reality",
        application_version: 1,
        engine_name: "servo",
        engine_version: 1,
    };

    let exts = ExtensionSet {
        khr_d3d11_enable: true,
        ..Default::default()
    };

    entry
        .create_instance(&app_info, &exts)
        .unwrap()
}

fn pick_format(formats: &[dxgiformat::DXGI_FORMAT]) -> dxgiformat::DXGI_FORMAT {
    // TODO: extract the format from surfman's device and pick a matching
    // valid format based on that. For now, assume that eglChooseConfig will
    // gravitate to B8G8R8A8.
    eprintln!("Available formats: {:?}", formats);
    for format in formats {
        match *format {
            dxgiformat::DXGI_FORMAT_B8G8R8A8_UNORM => return *format,
            //dxgiformat::DXGI_FORMAT_R8G8B8A8_UNORM => return *format,
            f => {
                eprintln!("Backend requested unsupported format {:?}", f);
            }
        }
    }

    panic!("No formats supported amongst {:?}", formats);
}

fn get_matching_adapter(
    requirements: &Requirements,
) -> Result<ComPtr<dxgi::IDXGIAdapter1>, String> {
    unsafe {
        let mut factory_ptr: *mut dxgi::IDXGIFactory1 = ptr::null_mut();
        let result = dxgi::CreateDXGIFactory1(
            &dxgi::IDXGIFactory1::uuidof(),
            &mut factory_ptr as *mut _ as *mut _,
        );
        assert_eq!(result, S_OK);
        let factory = ComPtr::from_raw(factory_ptr);

        let index = 0;
        loop {
            let mut adapter_ptr = ptr::null_mut();
            let result = factory.EnumAdapters1(index, &mut adapter_ptr);
            if result == DXGI_ERROR_NOT_FOUND {
                return Err("No matching adapter".to_owned());
            }
            assert_eq!(result, S_OK);
            let adapter = ComPtr::from_raw(adapter_ptr);
            let mut adapter_desc = mem::zeroed();
            let result = adapter.GetDesc1(&mut adapter_desc);
            assert_eq!(result, S_OK);
            let adapter_luid = &adapter_desc.AdapterLuid;
            if adapter_luid.LowPart == requirements.adapter_luid.LowPart
                && adapter_luid.HighPart == requirements.adapter_luid.HighPart
            {
                return Ok(adapter);
            }
        }
    }
}

fn select_feature_levels(requirements: &Requirements) -> Vec<D3D_FEATURE_LEVEL> {
    let levels = [
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    ];
    levels
        .iter()
        .filter(|&&level| level >= requirements.min_feature_level)
        .map(|&level| level)
        .collect()
}

fn init_device_for_adapter(
    adapter: ComPtr<dxgi::IDXGIAdapter1>,
    feature_levels: &[D3D_FEATURE_LEVEL],
) -> Result<(ComPtr<ID3D11Device>, ComPtr<d3d11::ID3D11DeviceContext>), String> {
    let adapter = adapter.up::<dxgi::IDXGIAdapter>();
    unsafe {
        let mut device_ptr = ptr::null_mut();
        let mut device_context_ptr = ptr::null_mut();
        let hr = d3d11::D3D11CreateDevice(
            adapter.as_raw(),
            D3D_DRIVER_TYPE_UNKNOWN,
            ptr::null_mut(),
            // add d3d11::D3D11_CREATE_DEVICE_DEBUG below for debug output
            d3d11::D3D11_CREATE_DEVICE_BGRA_SUPPORT | d3d11::D3D11_CREATE_DEVICE_DEBUG,
            feature_levels.as_ptr(),
            feature_levels.len() as u32,
            d3d11::D3D11_SDK_VERSION,
            &mut device_ptr,
            ptr::null_mut(),
            &mut device_context_ptr,
        );
        assert_eq!(hr, S_OK);
        let device = ComPtr::from_raw(device_ptr);
        let device_context = ComPtr::from_raw(device_context_ptr);
        mem::forget(device_context.clone());
        Ok((device, device_context))
    }
}


fn run2(entry: &Entry) {
    let instance = create_instance(entry);

    let system = instance
        .system(FormFactor::HEAD_MOUNTED_DISPLAY)
        .unwrap();

    let requirements = D3D11::requirements(&instance, system).unwrap();

    let adapter = get_matching_adapter(&requirements).unwrap();
    let feature_levels = select_feature_levels(&requirements);
    let (d3d11_device, device_context) = init_device_for_adapter(adapter, &feature_levels).unwrap();

    let (session, mut frame_waiter, mut frame_stream) = unsafe {
        instance
            .create_session::<D3D11>(
                system,
                &SessionCreateInfo {
                    device: d3d11_device.as_raw(),
                },
            )
            .unwrap()
    };

    // XXXPaul initialisation should happen on SessionStateChanged(Ready)?

    let pose = Posef {
        orientation: Quaternionf {
            x: 0.,
            y: 0.,
            z: 0.,
            w: 1.,
        },
        position: Vector3f {
            x: 0.,
            y: 0.,
            z: 0.,
        },
    };
    let space = session
        .create_reference_space(ReferenceSpaceType::LOCAL, pose)
        .unwrap();

    let viewer_space = session
        .create_reference_space(ReferenceSpaceType::VIEW, pose)
        .unwrap();

    let view_configurations = instance
        .enumerate_view_configuration_views(system, ViewConfigurationType::PRIMARY_STEREO)
        .unwrap();

    let left_view_configuration = view_configurations[0];
    let right_view_configuration = view_configurations[1];
    let left_extent = Extent2Di {
        width: left_view_configuration.recommended_image_rect_width as i32,
        height: left_view_configuration.recommended_image_rect_height as i32,
    };
    let right_extent = Extent2Di {
        width: right_view_configuration.recommended_image_rect_width as i32,
        height: right_view_configuration.recommended_image_rect_height as i32,
    };

    // Obtain view info
    //let _frame_state = frame_waiter.wait().expect("error waiting for frame");

    // Create swapchains

    // XXXManishearth should we be doing this, or letting Servo set the format?
    let formats = session.enumerate_swapchain_formats().unwrap();
    let format = pick_format(&formats);
    let swapchain_create_info = SwapchainCreateInfo {
        create_flags: SwapchainCreateFlags::EMPTY,
        usage_flags: SwapchainUsageFlags::COLOR_ATTACHMENT | SwapchainUsageFlags::SAMPLED,
        format,
        sample_count: 1,
        // XXXManishearth what if the recommended widths are different?
        width: left_view_configuration.recommended_image_rect_width,
        height: left_view_configuration.recommended_image_rect_height,
        face_count: 1,
        array_size: 1,
        mip_count: 1,
    };

    let mut left_swapchain = session.create_swapchain(&swapchain_create_info).unwrap();
    let left_images = left_swapchain.enumerate_images().unwrap();
    let mut right_swapchain = session.create_swapchain(&swapchain_create_info).unwrap();
    let right_images = right_swapchain.enumerate_images().unwrap();
    
    let mut state = State::NotReady;

    while let Some((frame_state, views)) = wait_for_animation_frame(
        &instance, &session, &mut frame_waiter, &mut frame_stream, &viewer_space, &space, &mut state,
    ) {
        render_animation_frame(
            &mut left_swapchain,
            &mut right_swapchain,
            &left_images,
            &right_images,
            &view_configurations,
            format,
            d3d11_device.clone(),
            device_context.clone(),
            &mut frame_stream,
            &frame_state,
            &space,
            &views,
            &left_extent,
            &right_extent,
        );
    }
}

#[derive(Copy, Clone, PartialEq)]
enum State {
    NotReady,
    Ready,
    ShutDown,
}

fn handle_openxr_events(instance: &Instance, mut state: State) -> State {
    use openxr::Event::*;
    loop {
        let mut buffer = openxr::EventDataBuffer::new();
        println!("polling");
        let event = instance.poll_event(&mut buffer).unwrap();
        println!("poll result: {}", event.is_some());
        match event {
            Some(SessionStateChanged(session_change)) => match session_change.state() {
                openxr::SessionState::EXITING | openxr::SessionState::LOSS_PENDING => {
                    return State::ShutDown;
                }
                openxr::SessionState::READY => state = State::Ready,
                _ => {
                    // FIXME: Handle other states
                }
            },
            Some(InstanceLossPending(_)) => {
                return State::ShutDown;
            }
            Some(_) => {
                // FIXME: Handle other events
            }
            None => {
                // No more events to process
                return state;
            }
        }
    }
}

fn wait_for_animation_frame(
    instance: &Instance,
    session: &Session<D3D11>,
    frame_waiter: &mut FrameWaiter,
    frame_stream: &mut FrameStream<D3D11>,
    viewer_space: &Space,
    space: &Space,
    state: &mut State,
) -> Option<(FrameState, Vec<openxr::View>)> {
    let frame_state = loop {
        let old_state = *state;
        *state = handle_openxr_events(instance, old_state);
        if *state == State::ShutDown {
            // Session is not running anymore.
            return None;
        }

        if *state == State::NotReady {
            std::thread::sleep(std::time::Duration::from_millis(250));
            continue;
        } else if old_state == State::NotReady && *state == State::Ready {
            session.begin(ViewConfigurationType::PRIMARY_STEREO).unwrap();
        }

        let frame_state = frame_waiter.wait().expect("error waiting for frame");

        frame_stream
            .begin()
            .expect("failed to start frame stream");
            
        if frame_state.should_render {
            println!("rendering this frame");
            break frame_state;
        }
        println!("not rendering this frame");
        frame_stream.end(
            frame_state.predicted_display_time,
            EnvironmentBlendMode::ADDITIVE,
            &[],
        ).unwrap();
    };

    let (_view_flags, views) = session
        .locate_views(
            ViewConfigurationType::PRIMARY_STEREO,
            frame_state.predicted_display_time,
            space,
        )
        .expect("error locating views");
    let _pose = viewer_space
        .locate(space, frame_state.predicted_display_time)
        .unwrap();
    Some((frame_state, views))
}

fn render_animation_frame(
    left_swapchain: &mut Swapchain<D3D11>,
    right_swapchain: &mut Swapchain<D3D11>,
    left_images: &[<D3D11 as Graphics>::SwapchainImage],
    right_images: &[<D3D11 as Graphics>::SwapchainImage],
    view_configurations: &[openxr::ViewConfigurationView],
    format: dxgiformat::DXGI_FORMAT,
    d3d11_device: ComPtr<ID3D11Device>,
    device_context: ComPtr<ID3D11DeviceContext>,
    frame_stream: &mut FrameStream<D3D11>,
    frame_state: &FrameState,
    space: &Space,
    openxr_views: &[openxr::View],
    left_extent: &Extent2Di,
    right_extent: &Extent2Di,
) {
    let left_image = left_swapchain.acquire_image().unwrap();
    left_swapchain
        .wait_image(openxr::Duration::INFINITE)
        .unwrap();
    let right_image = right_swapchain.acquire_image().unwrap();
    right_swapchain
        .wait_image(openxr::Duration::INFINITE)
        .unwrap();

    let left_image = left_images[left_image as usize];
    let right_image = right_images[right_image as usize];
    
    let width = view_configurations[0].recommended_image_rect_width + view_configurations[1].recommended_image_rect_width;
    let height = view_configurations[0].recommended_image_rect_height;
    let texture_desc = d3d11::D3D11_TEXTURE2D_DESC {
        Width: (width / 2) as u32,
        Height: height as u32,
        Format: format,
        MipLevels: 1,
        ArraySize: 1,
        SampleDesc: dxgitype::DXGI_SAMPLE_DESC {
            Count: 1,
            Quality: 0,
        },
        Usage: d3d11::D3D11_USAGE_DEFAULT,
        BindFlags: d3d11::D3D11_BIND_RENDER_TARGET | d3d11::D3D11_BIND_SHADER_RESOURCE,
        CPUAccessFlags: 0,
        MiscFlags: 0,
    };
    let byte_len = (width as usize / 2) * height as usize * mem::size_of::<u32>();
    let left_data = vec![0xFF; byte_len];
    let init = d3d11::D3D11_SUBRESOURCE_DATA {
        pSysMem: left_data.as_ptr() as *const _,
        SysMemPitch: (width / 2) as u32 * mem::size_of::<u32>() as u32,
        SysMemSlicePitch: byte_len as u32,
    };
    let mut d3dtex_ptr = ptr::null_mut();
    println!("creating solid texture");
    let hr = unsafe { d3d11_device.CreateTexture2D(&texture_desc, &init, &mut d3dtex_ptr) };
    let solid_texture = unsafe { ComPtr::from_raw(d3dtex_ptr) };
    let solid_resource = solid_texture.up::<d3d11::ID3D11Resource>();
    assert_eq!(hr, S_OK);

   unsafe {
        // from_raw adopts instead of retaining, so we need to manually addref
        // alternatively we can just forget after the CopySubresourceRegion call,
        // since these images are guaranteed to live at least as long as the frame
        let left_resource = ComPtr::from_raw(left_image).up::<d3d11::ID3D11Resource>();
        mem::forget(left_resource.clone());
        let right_resource = ComPtr::from_raw(right_image).up::<d3d11::ID3D11Resource>();
        mem::forget(right_resource.clone());
        println!("copying solid texture");
        device_context.CopyResource(left_resource.as_raw(), solid_resource.as_raw());
        device_context.CopyResource(right_resource.as_raw(), solid_resource.as_raw());
        device_context.Flush();
        println!("flushed");
    
        let texture_desc = d3d11::D3D11_TEXTURE2D_DESC {
            Width: (width / 2) as u32,
            Height: height as u32,
            Format: format,
            MipLevels: 1,
            ArraySize: 1,
            SampleDesc: dxgitype::DXGI_SAMPLE_DESC {
                Count: 1,
                Quality: 0,
            },
            Usage: d3d11::D3D11_USAGE_STAGING,
            BindFlags: 0,
            CPUAccessFlags: d3d11::D3D11_CPU_ACCESS_READ,
            MiscFlags: 0,
        };
        let initial_data = vec![0xFF0000FFu32; byte_len / mem::size_of::<u32>()];
        let init = d3d11::D3D11_SUBRESOURCE_DATA {
            pSysMem: initial_data.as_ptr() as *const _ as *const _,
            SysMemPitch: (width / 2) as u32 * mem::size_of::<u32>() as u32,
            SysMemSlicePitch: byte_len as u32,
        };
        println!("creating mapped texture");
        let hr = d3d11_device.CreateTexture2D(&texture_desc, &init, &mut d3dtex_ptr);
        assert_eq!(hr, S_OK);
        let solid_texture = ComPtr::from_raw(d3dtex_ptr);
        let solid_resource = solid_texture.up::<d3d11::ID3D11Resource>();
        device_context.CopyResource(solid_resource.as_raw(), left_resource.as_raw());
        println!("copied mapped texture");
        
        let mut mapped = d3d11::D3D11_MAPPED_SUBRESOURCE {
            pData: ptr::null_mut(),
            RowPitch: 0,
            DepthPitch: 0,
        };
        
        let hr = device_context.Map(solid_resource.as_raw(), 0, d3d11::D3D11_MAP_READ, 0, &mut mapped);
        assert_eq!(hr, S_OK);
        println!("finished mapped texture");
        assert_eq!(*(mapped.pData as *const u32), 0xFFFFFFFF);
    }
    
    left_swapchain.release_image().unwrap();
    right_swapchain.release_image().unwrap();
    println!("ending the frame");
    frame_stream
        .end(
            frame_state.predicted_display_time,
            EnvironmentBlendMode::ADDITIVE,
            &[&CompositionLayerProjection::new()
                .space(&space)
                .layer_flags(CompositionLayerFlags::BLEND_TEXTURE_SOURCE_ALPHA)
                .views(&[
                    openxr::CompositionLayerProjectionView::new()
                        .pose(openxr_views[0].pose)
                        .fov(openxr_views[0].fov)
                        .sub_image(
                            // XXXManishearth is this correct?
                            openxr::SwapchainSubImage::new()
                                .swapchain(&left_swapchain)
                                .image_array_index(0)
                                .image_rect(openxr::Rect2Di {
                                    offset: openxr::Offset2Di { x: 0, y: 0 },
                                    extent: *left_extent,
                                }),
                        ),
                    openxr::CompositionLayerProjectionView::new()
                        .pose(openxr_views[1].pose)
                        .fov(openxr_views[1].fov)
                        .sub_image(
                            openxr::SwapchainSubImage::new()
                                .swapchain(&right_swapchain)
                                .image_array_index(0)
                                .image_rect(openxr::Rect2Di {
                                    offset: openxr::Offset2Di { x: 0, y: 0 },
                                    extent: *right_extent,
                                }),
                        ),
                ])],
        )
        .unwrap();
        println!("ended");
}
