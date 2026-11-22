#!/bin/zsh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
APP_DIR="$ROOT_DIR/build/AmigaEmu.app"
CONTENTS_DIR="$APP_DIR/Contents"
MACOS_DIR="$CONTENTS_DIR/MacOS"
RES_DIR="$CONTENTS_DIR/Resources"
ICONSET_DIR="$ROOT_DIR/build/AppIcon.iconset"

cd "$ROOT_DIR"

echo "==> Building release binary..."
swift build -c release

echo "==> Generating AppIcon.icns from macos/Sources/AppIcon.swift..."
rm -rf "$ICONSET_DIR"
mkdir -p "$ICONSET_DIR"

GEN_SWIFT="$ROOT_DIR/build/gen_icon.swift"
cat "$ROOT_DIR/macos/Sources/AppIcon.swift" > "$GEN_SWIFT"
cat >> "$GEN_SWIFT" <<'EOF'

let sizes: [(String, CGFloat)] = [
    ("icon_16x16.png", 16),
    ("icon_16x16@2x.png", 32),
    ("icon_32x32.png", 32),
    ("icon_32x32@2x.png", 64),
    ("icon_128x128.png", 128),
    ("icon_128x128@2x.png", 256),
    ("icon_256x256.png", 256),
    ("icon_256x256@2x.png", 512),
    ("icon_512x512.png", 512),
    ("icon_512x512@2x.png", 1024),
]

let outDir = CommandLine.arguments[1]
for (name, px) in sizes {
    let img = AmigaAppIcon.makeIcon(size: px)
    guard let tiff = img.tiffRepresentation,
          let rep = NSBitmapImageRep(data: tiff),
          let png = rep.representation(using: .png, properties: [:]) else {
        fputs("Failed to render \(name)\n", stderr)
        exit(1)
    }
    try png.write(to: URL(fileURLWithPath: "\(outDir)/\(name)"))
}
EOF

swift "$GEN_SWIFT" "$ICONSET_DIR"
rm -f "$GEN_SWIFT"

STAGE_DIR="$(mktemp -d /tmp/amigaemu_app.XXXXXX)"
trap 'rm -rf "$STAGE_DIR"' EXIT
STAGE_APP="$STAGE_DIR/AmigaEmu.app"
CONTENTS_DIR="$STAGE_APP/Contents"
MACOS_DIR="$CONTENTS_DIR/MacOS"
RES_DIR="$CONTENTS_DIR/Resources"

mkdir -p "$MACOS_DIR" "$RES_DIR"

iconutil -c icns "$ICONSET_DIR" -o "$RES_DIR/AppIcon.icns"
rm -rf "$ICONSET_DIR"

cp "$ROOT_DIR/.build/release/AmigaEmu" "$MACOS_DIR/AmigaEmu"
chmod +x "$MACOS_DIR/AmigaEmu"

APP_VERSION="$(tr -d '[:space:]' < "$ROOT_DIR/VERSION" 2>/dev/null || echo "1.0.0")"

cat > "$CONTENTS_DIR/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>AmigaEmu</string>
    <key>CFBundleDisplayName</key>
    <string>AmigaEmu</string>
    <key>CFBundleIdentifier</key>
    <string>local.tomasz.amigaemu</string>
    <key>CFBundleVersion</key>
    <string>${APP_VERSION}</string>
    <key>CFBundleShortVersionString</key>
    <string>${APP_VERSION}</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleExecutable</key>
    <string>AmigaEmu</string>
    <key>CFBundleIconFile</key>
    <string>AppIcon</string>
    <key>CFBundleLocalizations</key>
    <array>
        <string>pl</string>
        <string>en</string>
    </array>
    <key>LSMinimumSystemVersion</key>
    <string>13.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSSupportsAutomaticGraphicsSwitching</key>
    <true/>
    <key>NSHumanReadableCopyright</key>
    <string>Copyright © 2026 Tomasz Lonowski. Licensed under GNU GPL v3.0.</string>
</dict>
</plist>
EOF

xattr -cr "$STAGE_APP"
codesign --force --deep --sign - "$STAGE_APP"
rm -rf "$APP_DIR"
ditto "$STAGE_APP" "$APP_DIR"
echo "==> Built: $APP_DIR"
