wxl_mapper.h, mlp_probe.h, mlp_routes.h are vendored UNMODIFIED from lane/tlm commit 1e6b08d6 (xbox-test/uwph/host/).
launch.h reuses mlp::resolve/summaries/assets/stub_names/log and runs wxl::run per image with its own target loop.
The mapper never calls an entry point, DllMain or TLS callbacks: no guest code is executed.
