fn main() {
    let cur_dir = std::path::PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap());
    let libs_dir = cur_dir.join(std::path::PathBuf::from(&r"..\packages\ANGLE.WindowsStore.Servo.2.1.18\bin\UAP"));
    let libs_dir = libs_dir.join(match &*std::env::var("TARGET").unwrap() {
        "x86_64-uwp-windows-msvc" | "x86_64-pc-windows-msvc" => "X64",
        "aarch64-uwp-windows-msvc" | "aarch64-pc-windows-msvc" => "ARM64",
        _ => unimplemented!(),
    });
    println!("cargo:rustc-env=LIB={}", libs_dir.display());
}