extern crate autocfg;
use std::process::Command;
use std::process::Output;

fn main() {
    // TODO how to get around rustc_private with autocfg
    // FIXME temporary solution: rust-script compiles for and runs in the local system? I want to test-compile for the target system or similar

    // does rust-script work? (warning: outputs to stdout/stderr)
    let status = Command::new("rust-script").arg("--version").status().expect("rust-script is required, running 'cargo install rust-script' in the console should fix this error\n");
    if !status.success() {
        panic!("rust-script: {}", status);
    }
    // sanity check: libc::size_t should always exist
    assert!(Command::new("rust-script").args(["--dep", "libc", "--expr", "{ let _: libc::size_t = 0; }"]).output().unwrap().status.success());
    fn probe_dep_path(dep: &str, path: &str) -> Output {
        let expr = format!("{{ use {}; }}", path);
        Command::new("rust-script").args(["--dep", dep, "--expr", &expr]).output().unwrap()
    }
    fn emit_dep_path_cfg(dep: &str, path: &str, cfg: &str) {
        if probe_dep_path(dep, path).status.success() {
            autocfg::emit(cfg);
        }
    }
    emit_dep_path_cfg("libc", "libc::B76800", "has_libc_B76800");
    emit_dep_path_cfg("libc", "libc::B230400", "has_libc_B230400");
    emit_dep_path_cfg("libc", "libc::CRTSCTS", "has_libc_CRTSCTS");
    emit_dep_path_cfg("libc", "libc::CNEW_RTSCTS", "has_libc_CNEW_RTSCTS");
    emit_dep_path_cfg("libc", "libc::cfmakeraw", "has_libc_cfmakeraw");
    #[rustfmt::skip]
    let has_ipv6 = [
        probe_dep_path("libc", "libc::getaddrinfo").status.success(),
        probe_dep_path("libc", "libc::freeaddrinfo").status.success(),
        probe_dep_path("libc", "libc::gai_strerror").status.success(),
    ].into_iter().reduce(|x, y| x && y).unwrap_or(false);
    if has_ipv6 {
        autocfg::emit("has_ipv6");
    }
    #[cfg(feature = "ENABLE_IPV6")]
    {
        println!("ENABLE_IPV6");
        assert!(has_ipv6);
    }
}
