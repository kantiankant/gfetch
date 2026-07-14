{
  description = "Generated flake for gfetch";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in {
        packages = {
          default = pkgs.stdenv.mkDerivation {
            pname = "gfetch";
            version = "0.1.0";
            src = self;

            enableParallelBuilding = true;

            nativeBuildInputs = with pkgs; [
              # No native dependencies detected
            ];

            buildInputs = with pkgs; [
              # No build dependencies detected
            ];

            buildPhase = ''
              make -j$NIX_BUILD_CORES
            '';

            installPhase = ''
              make install DESTDIR=$out PREFIX=$out
            '';

            makeFlags = [
              "PREFIX=${placeholder "out"}"
              "DESTDIR="
            ];

            meta = with pkgs.lib; {
              description = "gfetch";
              platforms = platforms.linux;
              mainProgram = "gfetchgfetch";
            };
          };
        };

        devShells = {
          default = pkgs.mkShell {
            inputsFrom = [ self.packages.${system}.default ];
            packages = with pkgs; [
              pkg-config
              gdb
            ];
            shellHook = ''
              echo "Development environment for gfetch"
            '';
          };
        };

        apps.default = flake-utils.lib.mkApp {
          drv = self.packages.${system}.default;
        };
      }
    );
}
