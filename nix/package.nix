{
  lib,
  stdenv,
  cmake,
  ninja,
  openssl,
  catch2_3,
  glibcLocales ? null,
  src ? lib.cleanSource ../.,
  withRepl ? true,
  doCheck ? true,
}:
stdenv.mkDerivation {
  pname = "numen";
  # single source of truth: project(... VERSION x.y.z) in CMakeLists.txt
  version = builtins.head (
    builtins.match ".*project\\([^)]*VERSION ([0-9.]+).*" (builtins.readFile ../CMakeLists.txt)
  );
  inherit src;

  nativeBuildInputs = [
    cmake
    ninja
  ];
  buildInputs = lib.optionals withRepl [ openssl ];
  nativeCheckInputs = [ catch2_3 ] ++ lib.optionals stdenv.hostPlatform.isLinux [ glibcLocales ];

  # the check sandbox has no locale data of its own
  env = lib.optionalAttrs (stdenv.hostPlatform.isLinux && glibcLocales != null) {
    LOCALE_ARCHIVE = "${glibcLocales}/lib/locale/locale-archive";
  };

  cmakeFlags = [
    (lib.cmakeBool "BUILD_SHARED_LIBS" true)
    (lib.cmakeBool "BUILD_REPL" withRepl)
    (lib.cmakeBool "BUILD_TESTS" doCheck)
    (lib.cmakeBool "USE_SYSTEM_CATCH" true)
  ];

  inherit doCheck;

  meta = {
    description = "Natural language calculator library";
    homepage = "https://github.com/vicinaehq/numen";
    platforms = lib.platforms.unix;
  }
  // lib.optionalAttrs withRepl { mainProgram = "numen"; };
}
