import AppKit
import SwiftUI
import PlatinumKit

/// About ClassicMac, as a Platinum window.
struct AboutView: View {
    private var version: String {
        let short = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "4.0"
        let build = Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String
        return build.map { $0 == short ? "Version \(short)" : "Version \(short) (\($0))" } ?? "Version \(short)"
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(alignment: .top, spacing: 16) {
                Image(nsImage: NSApp.applicationIconImage)
                    .resizable()
                    .interpolation(.high)
                    .frame(width: 96, height: 96)
                VStack(alignment: .leading, spacing: 6) {
                    Text("ClassicMac").platinumText(.large)
                    Text(version).platinumText(.emphasis)
                    Text("The whole classic Mac OS era, from System 7 to Mac OS 9 and Copland, on this computer.")
                        .platinumText(.body)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
            PlatinumSeparator()
            VStack(alignment: .leading, spacing: 3) {
                credit("Emulation", "QEMU 11.0.2 with ClassicMac's Power Mac and Quadra improvements")
                credit("Graphics", "Emulated ATI Radeon from RetroBoot, using Mac OS 9's own ATI drivers")
                credit("Firmware", "OpenBIOS; Copland on DingusPPC")
                credit("Sharing", "classicvirtio by Elliot Nunn")
                credit("Appearance", "Platinum fonts, icons and controls shared with RetroBoot")
                credit("System icons", "Original Mac OS 9 artwork © Apple Computer, Inc.")
                if IconPacks.shared.isUsingIconfactory {
                    credit("Mac icons", "The Iconfactory — used with permission")
                }
            }
            Spacer(minLength: 0)
            Text("© 2026 Austin McChord. Mac OS and Macintosh are trademarks of Apple Inc. See Third-Party Notices for licenses.")
                .platinumText(.detail)
                .fixedSize(horizontal: false, vertical: true)
        }
        .padding(14)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .background(Platinum.face.color)
    }

    private func credit(_ title: String, _ text: String) -> some View {
        HStack(alignment: .firstTextBaseline, spacing: 6) {
            Text(title + ":").platinumText(.small).frame(width: 70, alignment: .trailing)
            Text(text).platinumText(.small).fixedSize(horizontal: false, vertical: true)
        }
    }
}
