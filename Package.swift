// swift-tools-version: 6.1

import Foundation
import PackageDescription

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
        .library(name: "IceBox", targets: ["IceBox"]),
        .library(name: "IceStorm", targets: ["IceStorm"]),
        .plugin(name: "CompileSlice", targets: ["CompileSlice"]),
    ],
    dependencies: [
        .package(url: "https://github.com/apple/swift-docc-plugin", from: "1.4.3")
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
            name: "IceBox",
            dependencies: ["Ice"],
            path: "swift/src/IceBox",
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
            url: "https://download.zeroc.com/ice/nightly/3.9/Ice-3.9.0-nightly.20260829.1.xcframework.zip",
            checksum: "26bc5d27f2c7a85bd069200daa23d7e441babd0c44afb877e06ce280dc2aeac2"
        ),
        .binaryTarget(
            name: "IceDiscoveryCpp",
            url: "https://download.zeroc.com/ice/nightly/3.9/IceDiscovery-3.9.0-nightly.20260829.1.xcframework.zip",
            checksum: "3cdff963fccbfb4695fabef41fde646f196c72d373aa0e2d7764bcef83cae08c"
        ),
        .binaryTarget(
            name: "IceLocatorDiscoveryCpp",
            url: "https://download.zeroc.com/ice/nightly/3.9/IceLocatorDiscovery-3.9.0-nightly.20260829.1.xcframework.zip",
            checksum: "02716932087e29a8afbf675a33287f109b07adb16f95b9b5976ee749fed7c377"
        ),
        .binaryTarget(
            name: "slice2swift",
            url: "https://download.zeroc.com/ice/nightly/3.9/slice2swift-3.9.0-nightly.20260829.1.artifactbundle.zip",
            checksum: "3468ed15f2f53e5f30dfdb280daa19679637383fd58d44a3751b339d09443f02"
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
