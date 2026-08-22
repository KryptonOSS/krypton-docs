{
  description = "PagePP - a static site generator";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      supportedSystems = [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ];
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
    in
    {
      packages = forAllSystems (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          pagepp = pkgs.stdenv.mkDerivation {
            pname = "pagepp";
            version = "unstable";
            src = ./.;

            nativeBuildInputs = [ pkgs.cmake ];
            buildInputs = [ pkgs.cmark pkgs.tomlplusplus ];

            postPatch = ''
              sed -i '/include(FetchContent)/d' CMakeLists.txt
              sed -i '/^FetchContent_Declare/,/^)/d' CMakeLists.txt
              sed -i '/FetchContent_MakeAvailable/d' CMakeLists.txt
              sed -i '/^add_executable/i find_package(cmark REQUIRED)\nfind_package(tomlplusplus REQUIRED)' CMakeLists.txt
            '';
          };
        in {
          inherit pagepp;
          default = pagepp;
        }
      );
    };
}
