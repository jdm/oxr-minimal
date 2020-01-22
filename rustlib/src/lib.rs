use surfman_chains::SwapChains;
use surfman_chains_api::SwapChainAPI;
use surfman_chains_api::SwapChainsAPI;

extern "C" {
    fn OutputDebugStringA(s: *const u8);
}

struct Waker;
impl webxr_api::MainThreadWaker for Waker {
    fn clone_box(&self) -> Box<dyn webxr_api::MainThreadWaker> {
        Box::new(Waker)
    }
    fn wake(&self) {
    }
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

    /*pub type EGLNativeWindowType = *const libc::c_void;
    pub type khronos_utime_nanoseconds_t = khronos_uint64_t;
    pub type khronos_uint64_t = u64;
    pub type khronos_ssize_t = libc::c_long;
    pub type EGLint = i32;
    pub type EGLContext = *const libc::c_void;
    pub type EGLNativeDisplayType = *const libc::c_void;
    pub type EGLNativePixmapType = *const libc::c_void;
    pub type NativeDisplayType = EGLNativeDisplayType;
    pub type NativePixmapType = EGLNativePixmapType;
    pub type NativeWindowType = EGLNativeWindowType;*/

//include!(concat!(env!("OUT_DIR"), "/egl_bindings.rs"));

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
    
    stderrlog::StdErrLog::new().modules(&["webxr".to_string(), "webxr-api".to_string(), "webxr_api".to_string()]).verbosity(99).color(stderrlog::ColorChoice::Never).init().unwrap();
    
    debug("running");

    let _ = do_redirect_stdout_stderr();

    /*let gl_factory = || unsafe {
        gleam::gl::GlesFns::load_with(|addr| {
            let addr = std::ffi::CString::new(addr.as_bytes()).unwrap();
            let addr = addr.as_ptr();
            let egl = Egl;
            egl.GetProcAddress(addr) as *const libc::c_void
        })
    };*/
    
    let gl_factory = || panic!();
    
    //let gl = gl_factory();

    let (tx, rx) = webxr_api::channel().unwrap();
    let mut registry = webxr::MainThreadRegistry::new(Box::new(Waker))
        .expect("Failed to create WebXR device registry");

    debug("created registry");
    let discovery = webxr::openxr::OpenXrDiscovery::new(std::sync::Arc::new(gl_factory));
    registry.register(discovery);

    let webxr_swap_chains = SwapChains::new();
    registry.set_swap_chains(webxr_swap_chains.clone());
        
    debug("registered");

    let mut registry2 = registry.registry();
    let (raf_sender, raf_receiver) = webxr_api::channel().expect("error creating channel");
    registry2.request_session(webxr_api::SessionMode::ImmersiveAR, tx, raf_sender);

    debug("requested session");

    registry.run_one_frame();

    let mut session = rx.recv().unwrap().expect("request session failed");
    debug("got session");

    /*let (mut device, mut context) = unsafe {
        surfman::Device::from_current_context().expect("couldn't create surfman device")
    };
    let id = webxr_api::SwapChainId::new();
    webxr_swap_chains.create_detached_swap_chain(
        id, session.recommended_framebuffer_resolution().cast_unit(), &mut device, &mut context, surfman::SurfaceAccess::GPUOnly,
    ).unwrap();
    session.set_swap_chain(Some(id));*/

    session.start_render_loop();
    
    debug("waiting for first frame");
    while let Ok(_frame) = raf_receiver.recv() {
        debug("received frame");
        /*let swap_chain = webxr_swap_chains.get(id).unwrap();
        swap_chain.swap_buffers(&mut device, &mut context).unwrap();*/
        session.render_animation_frame();
        debug("rendering frame");
    }
    /*while let Ok(()) = rx.recv() {
        registry.run_one_frame
    }*/

    debug("done running");
}

#[cfg(test)]
mod tests {
    #[test]
    fn it_works() {
        assert_eq!(2 + 2, 4);
    }
}
