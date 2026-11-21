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
            url: "https://download.zeroc.com/ice/nightly/3.8/Ice-3.8.3-nightly.20261121.1.xcframework.zip",
            checksum: "974ba538e4f36eb0dcdc1ad7c30029f8fbbdf0bdd9bd9f10c93503eb7972bbb3"
        ),
        .binaryTarget(
            name: "IceDiscoveryCpp",
            url: "https://download.zeroc.com/ice/nightly/3.8/IceDiscovery-3.8.3-nightly.20261121.1.xcframework.zip",
            checksum: "fa6b86f738cc97accb9b3e0ddca4de0b413b649a6ef78a72fb9d3823326f1821"
        ),
        .binaryTarget(
            name: "IceLocatorDiscoveryCpp",
            url: "https://download.zeroc.com/ice/nightly/3.8/IceLocatorDiscovery-3.8.3-nightly.20261121.1.xcframework.zip",
            checksum: "f1faedadb9df23143cfff095cf6602c888bf7b578cf56e7af8cc485a14101fd5"
        ),
        .binaryTarget(
            name: "slice2swift",
            url: "https://download.zeroc.com/ice/nightly/3.8/slice2swift-3.8.3-nightly.20261121.1.artifactbundle.zip",
            checksum: "4c089802838ae7c9828e22231cb2156acf06f8341cda812c95ace86341e09476"
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
