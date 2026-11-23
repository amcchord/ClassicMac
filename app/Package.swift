// swift-tools-version: 6.2
import PackageDescription

let package = Package(
    name: "ClassicMac",
    platforms: [
        .macOS(.v15),
        .iOS(.v18)
    ],
    products: [
        .executable(name: "ClassicMac", targets: ["ClassicMac"]),
        .library(name: "PlatinumKit", targets: ["PlatinumKit"])
    ],
    targets: [
        // The Platinum appearance (Mac OS 8/9) shared by every ClassicMac
        // front end: fonts, icons, pixel-exact controls and window chrome.
        // Platform-neutral SwiftUI/CoreGraphics, with the macOS window and
        // menu layer behind `#if os(macOS)`, so an iPad front end can reuse it.
        .target(
            name: "PlatinumKit",
            path: "Sources/PlatinumKit",
            swiftSettings: [
                .swiftLanguageMode(.v5)
            ]
        ),
        .target(name: "CMachineArchive"),
        // Socket and shared-memory helpers for the guest display protocol.
        .target(
            name: "CDisplaySocket",
            path: "Sources/CDisplaySocket"
        ),
        .executableTarget(
            name: "ClassicMac",
            dependencies: ["PlatinumKit", "CDisplaySocket", "CMachineArchive"],
            path: "Sources/ClassicMac",
            swiftSettings: [
                // The app predates Swift 6 strict concurrency; keep the v5
                // language mode until the concurrency audit is done.
                .swiftLanguageMode(.v5)
            ]
        ),
        .testTarget(
            name: "ClassicMacTests",
            dependencies: ["ClassicMac", "PlatinumKit"],
            path: "Tests/ClassicMacTests",
            resources: [.copy("Fixtures/Platinum")]
        )
    ]
)
