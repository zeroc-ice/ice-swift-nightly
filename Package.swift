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
            url: "https://download.zeroc.com/ice/nightly/3.8/Ice-3.8.2-nightly.20260506.1.xcframework.zip",
            checksum: "d857ff60a7f51a1660ddacc80c98598d30c319f4e3d33deb5ebc7f1c61e2966c"
        ),
        .binaryTarget(
            name: "IceDiscoveryCpp",
            url: "https://download.zeroc.com/ice/nightly/3.8/IceDiscovery-3.8.2-nightly.20260506.1.xcframework.zip",
            checksum: "2ba3d2777cf5aaabb39417ffa11bdb349eaff2920401c07b9d68dfefc8b2e6a5"

        ),
        .binaryTarget(
            name: "IceLocatorDiscoveryCpp",
            url: "https://download.zeroc.com/ice/nightly/3.8/IceLocatorDiscovery-3.8.2-nightly.20260506.1.xcframework.zip",
            checksum: "e40237a708726bca72008c65747be10ef0a922119aa731b811006e31bcad3ae2"
        ),
        .binaryTarget(
            name: "slice2swift",
            url: "https://download.zeroc.com/ice/nightly/3.8/slice2swift-3.8.2-nightly.20260506.1.artifactbundle.zip",
            checksum: "4fa2cac06b551311be0057e12313f77d87956d7e8a5a91015651568f24561f42"
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
