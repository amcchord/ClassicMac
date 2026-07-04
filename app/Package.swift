// swift-tools-version: 6.2
import PackageDescription

let package = Package(
    name: "ClassicMac",
    platforms: [
        .macOS(.v15)
    ],
    targets: [
        .executableTarget(
            name: "ClassicMac",
            path: "Sources/ClassicMac",
            swiftSettings: [
                // The app predates Swift 6 strict concurrency; keep the v5
                // language mode until the concurrency audit is done.
                .swiftLanguageMode(.v5)
            ]
        )
    ]
)
