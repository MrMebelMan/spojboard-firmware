{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  name = "spojboard-firmware";

  packages = with pkgs; [
    platformio
    esptool
    picocom
    python3
  ];

  # PlatformIO stores its toolchains and packages in $PLATFORMIO_CORE_DIR.
  # Default (~/.platformio) works fine; override here if needed.
  # PLATFORMIO_CORE_DIR = "/path/to/.platformio";

  shellHook = ''
    echo "SpojBoard firmware dev shell"
    echo "  pio run -e adafruit_matrixportal_esp32s3   — build ESP32-S3"
    echo "  pio run -e adafruit_matrix_portal_m4        — build M4"
    echo "  pio run -e adafruit_matrixportal_esp32s3 -t upload && pio device monitor"
  '';
}
