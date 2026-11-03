╭─ ☤ Hermes ────────────────────────────────────────────────────────────────────────────────────────────────────────────────╮
Done. Files modified (no build started):

- cmake/toolchain-armv7.cmake (new) — 32-bit ARMv7 hard-float toolchain (arm-linux-gnueabihf-gcc/g++, -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard for the Cortex-A7), sysroot via FLWR_ARMV7_SYSROOT, same find-root modes as the arm64 file.
- CMakePresets.json — added linux-armv7-cross configure + build presets (builds into build/linux-armv7-cross/). Validated with cmake --list-presets.
- README.md — cross-compile section rewritten to cover both arm64 and armv7, plus the ABI warning.

One important correction first: the compiler you installed is the wrong architecture for this board.

- Your NanoPi has a Cortex-A7 → 32-bit ARMv7, hard-float (gnueabihf) userland.
- aarch64-linux-gnu-gcc can only emit 64-bit ARM binaries; it cannot target armv7 at all.
- Fedora 44 has no hard-float armv7 cross compiler (I checked: gcc-arm-linux-gnu in Fedora is the soft-float gnueabi ABI — incompatible with the NanoPi's rootfs). So install one of:
  - The official Arm GNU Toolchain "arm-none-linux-gnueabihf" (x86_64 host) from developer.arm.com — self-contained, includes a glibc sysroot. Binaries are arm-none-linux-gnueabihf-gcc/g++.
  - Debian/Ubuntu's gcc-arm-linux-gnueabihf / g++-arm-linux-gnueabihf (e.g. in a podman container or chroot).

Build commands (in order):

1. Host protoc / grpc_cpp_plugin — already present from your native build and version-matched (protoc reports libprotoc 31.1 = gRPC v1.78.1's bundled protobuf). Rebuild or skip if present:
   
   cd /home/jmazan/Projects/flower/examples/embedded_cpp_client
   cmake --preset linux-gcc
   cmake --build --preset linux-gcc --target protoc grpc_cpp_plugin
   export PATH="$PWD/build/linux-gcc/_deps/grpc-build:$PWD/build/linux-gcc/_deps/grpc-build/third_party/protobuf:$PATH"
   
   (Verified these binaries exist at those paths: grpc_cpp_plugin and protoc -> protoc-31.1.0.)

2. Prepare a target sysroot from the NanoPi (needed for OpenSSL 3 headers/libs and glibc dev files — the cross toolchain does not ship OpenSSL). On the device, if dev files are missing: apt install libssl-dev (Armbian/Debian has OpenSSL 3). Then from the host:
   
   mkdir -p ~/sysroots/nanopi
   rsync -aH --copy-links --numeric-ids \
     --exclude=/proc --exclude=/sys --exclude=/dev --exclude=/run --exclude=/boot \
     root@NANOPI_IP:/ ~/sysroots/nanopi/
   

3. Configure + build:

```bash   
   cd /home/jmazan/Projects/flower/examples/embedded_cpp_client
   cmake --preset linux-armv7-cross -DFLWR_ARMV7_SYSROOT=$HOME/sysroots/nanopi
   cmake --build --preset linux-armv7-cross
   cmake --build --preset linux-armv7-cross -j $(nproc)
```
   If you use the Arm GNU Toolchain, add:
   -DCMAKE_C_COMPILER=arm-none-linux-gnueabihf-gcc -DCMAKE_CXX_COMPILER=arm-none-linux-gnueabihf-g++

4. Verify the artifact:

 ```bash  
   file build/linux-armv7-cross/flwr_client
```
expect: ELF 32-bit LSB executable, ARM, EABI5, hard-float
   

Fixed (cmake files):
- cmake/toolchain-armv7.cmake — rewritten: auto-detects the compiler among arm-linux-gnueabihf-* / arm-none-linux-gnueabihf-* / arm-linux-gnueabi-* / arm-linux-gnu-* (checked in <sysroot>/usr/bin then PATH), with specific error messages when the C or C++ front-end is missing; wires up the Debian multiarch sysroot layout (-isystem for C++/gcc headers, -B/-L for crt/libgcc/libstdc++, CMAKE_LIBRARY_ARCHITECTURE, PKG_CONFIG_LIBDIR scoped to the sysroot); sysroot now also accepted from the FLWR_ARMV7_SYSROOT env var.
- CMakeLists.txt — host protoc/grpc_cpp_plugin are now found via HINTS into the native build outputs (build/…, build/linux-gcc/…), not only PATH.

Environment gaps I had to fill (no sudo needed): your Fedora gcc-arm-linux-gnu has no C++ front-end and no matching libc, so I installed a complete Debian trixie cross toolchain inside ~/sysroots/nanopi (g++-14-arm-linux-gnueabihf, binutils, libgcc/libstdc++ dev, glibc dev files, linux-libc-dev, libssl-dev) and repaired the host-side binaries' library paths. Verified with a real test compile: ELF 32-bit LSB … ARM, EABI5 linked against the sysroot's OpenSSL.
