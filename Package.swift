// swift-tools-version: 6.1

import Foundation
import PackageDescription

let iceUtilSources: [String] = [
    "src/Ice/ConsoleUtil.cpp",
    "src/Ice/CtrlCHandler.cpp",
    "src/Ice/Demangle.cpp",
    "src/Ice/Exception.cpp",
    "src/Ice/FileUtil.cpp",
    "src/Ice/LocalException.cpp",
    "src/Ice/Options.cpp",
    "src/Ice/OutputUtil.cpp",
    "src/Ice/Random.cpp",
    "src/Ice/StringConverter.cpp",
    "src/Ice/StringUtil.cpp",
    "src/Ice/UUID.cpp",
]

let package = Package(
    name: "ice",
    defaultLocalization: "en",
    platforms: [
        .macOS(.v15),
        .iOS(.v18),
    ],
    products: [
        .library(name: "Ice", targets: ["Ice"]),
        .library(name: "Glacier2", targets: ["Glacier2"]),
        .library(name: "IceGrid", targets: ["IceGrid"]),
        .library(name: "IceStorm", targets: ["IceStorm"]),
        .plugin(name: "CompileSlice", targets: ["CompileSlice"]),
    ],
    dependencies: [
        .package(url: "https://github.com/zeroc-ice/mcpp.git", revision: "4137e39e62bd22c232b470c9516669fc3d1a1353"),
        .package(url: "https://github.com/apple/swift-docc-plugin", from: "1.4.3"),
    ],
    targets: [
        .target(
            name: "Ice",
            dependencies: ["IceImpl"],
            path: "swift/src/Ice",
            plugins: [.plugin(name: "CompileSlice")]
        ),
        .target(
            name: "Glacier2",
            dependencies: ["Ice"],
            path: "swift/src/Glacier2",
            plugins: [.plugin(name: "CompileSlice")]
        ),
        .target(
            name: "IceGrid",
            dependencies: ["Ice", "Glacier2"],
            path: "swift/src/IceGrid",
            plugins: [.plugin(name: "CompileSlice")]
        ),
        .target(
            name: "IceStorm",
            dependencies: ["Ice"],
            path: "swift/src/IceStorm",
            plugins: [.plugin(name: "CompileSlice")]
        ),
        .target(
            name: "IceImpl",
            dependencies: [
                "IceCpp",
                "IceDiscoveryCpp",
                "IceLocatorDiscoveryCpp",
            ],
            path: "swift/src/IceImpl",
            linkerSettings: [
                .linkedLibrary("bz2"),
                .linkedFramework("ExternalAccessory"),
            ]
        ),
        .binaryTarget(
            name: "IceCpp",
            url: "https://download.zeroc.com/ice/nightly/Ice-3.9.0-nightly.20251224.1.xcframework.zip",
            checksum: "23b5212935c7139e6d9b06b77c2a5884c891e10b2c0b4030af71f50ca37f5972"
        ),
        .binaryTarget(
            name: "IceDiscoveryCpp",
            url: "https://download.zeroc.com/ice/nightly/IceDiscovery-3.9.0-nightly.20251224.1.xcframework.zip",
            checksum: "bc30240c78ceb5bd6ed33de2cc46f4e2f86577940212a900d9286d871690f165"

        ),
        .binaryTarget(
            name: "IceLocatorDiscoveryCpp",
            url: "https://download.zeroc.com/ice/nightly/IceLocatorDiscovery-3.9.0-nightly.20251224.1.xcframework.zip",
            checksum: "15894bcbdd2e6ac058f1d65067366123fcc83ff59b011b5f46b49a5660d2dd31"
        ),
        .executableTarget(
            name: "slice2swift",
            dependencies: ["mcpp"],
            path: "cpp",
            exclude: [
                "test",
                "src/slice2swift/build",
                "src/slice2swift/msbuild",
                "src/slice2swift/Slice2Swift.rc",
                "src/Slice/build",
                "src/Slice/msbuild",
                "src/Slice/Scanner.l",
                "src/Slice/Grammar.y",
                "src/Slice/Makefile.mk",
            ],
            sources: ["src/slice2swift", "src/Slice"] + iceUtilSources,
            publicHeadersPath: "src/slice2swift",
            cxxSettings: [
                .headerSearchPath("src"),
                .headerSearchPath("include"),
            ]
        ),
        .plugin(
            name: "CompileSlice",
            capability: .buildTool(),
            dependencies: ["slice2swift"],
            path: "swift/Plugins/CompileSlice"
        ),
    ],
    swiftLanguageModes: [.v6],
    cxxLanguageStandard: .cxx20
)
