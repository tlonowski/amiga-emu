// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "AmigaEmu",
    platforms: [
        .macOS(.v13)
    ],
    products: [
        .executable(name: "AmigaEmu", targets: ["AmigaEmuApp"])
    ],
    targets: [
        .target(
            name: "AmigaCore",
            path: "core",
            sources: ["src"],
            publicHeadersPath: "include",
            cxxSettings: [
                .headerSearchPath("include"),
                .unsafeFlags(["-std=c++20", "-O2"])
            ]
        ),
        .executableTarget(
            name: "AmigaEmuApp",
            dependencies: ["AmigaCore"],
            path: "macos/Sources",
            exclude: ["Shaders.metal"],
            linkerSettings: [
                .linkedFramework("AppKit"),
                .linkedFramework("AVFoundation"),
                .linkedFramework("GameController"),
                .linkedFramework("Metal"),
                .linkedFramework("MetalKit")
            ]
        )
    ],
    cxxLanguageStandard: .cxx20
)
