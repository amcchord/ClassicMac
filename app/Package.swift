// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "ClassicMac",
    platforms: [
        .macOS(.v13)
    ],
    targets: [
        .executableTarget(
            name: "ClassicMac",
            path: "Sources/ClassicMac"
        )
    ]
)
