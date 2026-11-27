import Foundation

/// OS category used to group and filter machines in the Library.
enum LibraryOSCategory: String, CaseIterable, Identifiable, Hashable {
    case all = "All Systems"
    case system7 = "System 7"
    case macOS8 = "Mac OS 8"
    case macOS9 = "Mac OS 9"
    case macOSX = "Mac OS X"
    case copland = "Copland"

    var id: String { rawValue }
    var label: String { rawValue }

    /// Clean default ranking order: OS 9, OS X, OS 8, System 7, Copland.
    var sortOrder: Int {
        switch self {
        case .macOS9: return 0
        case .macOSX: return 1
        case .macOS8: return 2
        case .system7: return 3
        case .copland: return 4
        case .all: return 5
        }
    }
}

/// What the Mac OS Library knows about a catalog entry beyond the catalog
/// itself: a friendlier title, what is inside, how to get the most from it,
/// and which graphics card it is prepared for. Catalog entries without an
/// edition still appear, described by the catalog alone.
///
/// The Deluxe and SuperDeluxe contents come from RetroBoot's edition manifests
/// (apps/manifests/superdeluxe-v2.json, docs/guide/deluxe-image.md).
struct LibraryEdition {
    let familyName: String
    /// Latest revision for which the static `tagline` and `description` apply;
    /// newer catalog revisions fall back to the catalog's own cleaned summary
    /// while preserving curated `contents` and `notes`.
    let baselineVersion: Int
    let title: String
    let osLabel: String
    let tagline: String
    let description: String
    let contents: [(category: String, items: [String])]
    let notes: [String]
    /// The card a fresh download starts with. ATI-ready Macs carry ATI's own
    /// Radeon 3D software and RetroBoot Display.
    let graphicsCard: GraphicsCard
    /// Lower sorts first.
    let order: Int

    static func edition(for id: String) -> LibraryEdition? {
        let name = LibraryEntry.family(for: id).name
        return all.first { $0.familyName == name }
    }

    static let all: [LibraryEdition] = [
        LibraryEdition(
            familyName: "os922-superdeluxe",
            baselineVersion: 7,
            title: "Mac OS 9.2.2 SuperDeluxe",
            osLabel: "Mac OS 9.2.2",
            tagline: "54 classic games, ClarisWorks and Home Page",
            description: "The biggest Mac in the Library: 54 classic games in five categories, genuine color Crystal Quest, ClarisWorks 5 and Claris Home Page 3, with every Deluxe utility. Ready for the ATI Radeon card, so 3D games run accelerated.",
            contents: [
                ("Action & 3D", ["Quake III Arena Demo", "Quake", "Marathon 2", "Nanosaur", "Cro-Mag Rally",
                                 "Weekend Warrior", "Avara", "Armor Alley", "Spectre Supreme"]),
                ("Adventure", ["Myst", "The Secret of Monkey Island", "Loom", "King's Quest VI", "Prince of Persia",
                               "Scarab of Ra", "Cosmic Osmo", "The Oregon Trail", "TaskMaker"]),
                ("Strategy & Simulation", ["SimCity Classic", "SimCity 2000", "SimTower", "SimAnt", "Civilization",
                                           "Escape Velocity", "Bolo", "Strategic Conquest", "Spaceward Ho!",
                                           "Rescue!", "DinoPark Tycoon"]),
                ("Arcade", ["Glider PRO", "Crystal Quest", "Maelstrom", "Swoop", "Glypha III", "MacSki",
                            "Apeiron", "Mars Rising", "Munchies", "Diamonds"]),
                ("Puzzle & Cards", ["Lemmings", "Oh No! More Lemmings", "Tetris Max", "Shanghai II", "Snood",
                                    "Solitaire Till Dawn", "Spin Doctor", "Oxyd", "Jewelbox", "3 in Three",
                                    "The Fool's Errand", "Barrack", "Bubble Trouble", "Chiral", "Pipe Dream"]),
                ("Productivity", ["ClarisWorks 5.0", "Claris Home Page 3"]),
                ("Utilities", ["ACTION GoMac", "After Dark 4.94 (Flying Toasters)", "Audion 3",
                               "SoundJam MP", "Transmit", "Speedometer 4.02", "MacBench 5.0", "Gauge PRO",
                               "USB Overdrive", "Internet Explorer 5.1.7"]),
            ],
            notes: [
                "Open the Games Guide on the desktop for controls. Games launch from Start → Games in the GoMac bar at the bottom of the screen.",
                "A few older games ask for 256 colors; the game launchers switch for you.",
                "King's Quest VI starts new saved games; saves from other editions do not load.",
                "Original shareware and demo limits remain.",
            ],
            graphicsCard: .atiRadeon,
            order: 0
        ),
        LibraryEdition(
            familyName: "os922-mega-deluxe",
            baselineVersion: 6,
            title: "Mac OS 9.2.2 Mega Deluxe",
            osLabel: "Mac OS 9.2.2",
            tagline: "146 classic games, creative apps, and utilities across 8 Launcher tabs",
            description: "The largest Classic Mac collection: 146 classic games, creative apps, and utilities across 8 Launcher tabs and ACTION GoMac. Ready for the ATI Radeon card, so 3D games run accelerated.",
            contents: [
                ("Games & Activities", ["146 classic games, creative apps, and utilities across 8 Launcher tabs"]),
                ("Utilities & 3D", ["Launcher", "ACTION GoMac", "ATI Radeon 3D drivers"]),
            ],
            notes: [
                "Games and creative activities launch from Launcher and Start → Games in the ACTION GoMac bar.",
                "Uses the emulated ATI Radeon for accelerated RAVE and OpenGL 3D games, with live window resizing up to 1920 × 1440.",
                "Original shareware and demo limits remain.",
            ],
            graphicsCard: .atiRadeon,
            order: 1
        ),
        LibraryEdition(
            familyName: "os922-deluxe",
            baselineVersion: 10,
            title: "Mac OS 9.2.2 Deluxe",
            osLabel: "Mac OS 9.2.2",
            tagline: "16 classic games, After Dark, music and benchmarks",
            description: "A hand-picked classic Mac: sixteen games, After Dark's Flying Toasters, Audion and SoundJam for music, Transmit for files and the classic benchmarks. Ready for the ATI Radeon card, so 3D games run accelerated.",
            contents: [
                ("Casual & Puzzle", ["Shanghai II", "Tetris Max", "Snood", "Solitaire Till Dawn"]),
                ("Arcade & Exploration", ["Glider PRO", "Crystal Quest", "Maelstrom", "Escape Velocity",
                                          "Prince of Persia"]),
                ("Building & Strategy", ["SimCity Classic", "SimCity 2000", "Bolo"]),
                ("Adventure & 3D", ["Myst", "Marathon 2", "Quake", "Quake III Arena Demo"]),
                ("Utilities", ["ACTION GoMac", "After Dark 4.94 (Flying Toasters)", "Audion 3",
                               "SoundJam MP", "Transmit", "Speedometer 4.02", "MacBench 5.0", "Gauge PRO",
                               "USB Overdrive", "Internet Explorer 5.1.7"]),
            ],
            notes: [
                "Games are in Macintosh HD:Games and in Start → Games.",
                "Choose 256 colors in Monitors before Prince of Persia. Myst runs at any depth.",
                "Original shareware and demo limits remain.",
            ],
            graphicsCard: .atiRadeon,
            order: 2
        ),
        LibraryEdition(
            familyName: "mac-os-9.2.1-ati-32gb",
            baselineVersion: 4,
            title: "Mac OS 9.2.1 (Clean)",
            osLabel: "Mac OS 9.2.1",
            tagline: "A clean Mac OS 9 to make your own",
            description: "A fresh Mac OS 9.2.1 with Apple’s ATI graphics software and a 32 GB disk that grows on your computer only as you add files. Start here to install your own software.",
            contents: [
                ("Included", ["Mac OS 9.2.1", "32 GB startup disk"]),
            ],
            notes: [
                "Uses the emulated ATI Radeon with Apple’s own Mac OS drivers for accelerated RAVE and OpenGL games.",
                "ATI Radeon supports live window resizing up to 1920 × 1440. Choose the Standard graphics card for larger resolutions or Black & White, 4 and 16 colors.",
            ],
            graphicsCard: .atiRadeon,
            order: 3
        ),
        LibraryEdition(
            familyName: "copland-d11e4-activities",
            baselineVersion: 3,
            title: "Copland D11E4",
            osLabel: "Copland D11E4",
            tagline: "Apple's unfinished Mac OS 8 rewrite",
            description: "Explore Copland, the never-released Mac OS that Apple abandoned in 1996, on its own Power Mac 7500 engine. Includes Anarcho text editing, MineSweeper, Solitaire, GX graphics demos and the original experiment guides.",
            contents: [
                ("Activities", ["Anarcho (text editing)", "MineSweeper", "Solitaire", "GXSlidemaster",
                                "GX graphics demos", "Experiment guides and a notebook"]),
            ],
            notes: [
                "Experimental: some Finder actions stop at developer assertions. Use Continue Copland when that happens.",
                "Sound, networking, shared folders and removable media are unavailable.",
            ],
            graphicsCard: .standard,
            order: 50
        ),
    ]
}

/// A catalog entry as the Library shows it.
struct LibraryEntry: Identifiable, Hashable {
    let machine: DownloadableMachine
    let edition: LibraryEdition?

    var id: String { machine.id }

    static func == (lhs: Self, rhs: Self) -> Bool { lhs.machine == rhs.machine }
    func hash(into hasher: inout Hasher) { hasher.combine(machine) }

    private static let disambiguatedTitles: [String: String] = [
        "system-7.5.5-base": "System 7.5.5 (Clean)",
        "mac-os-7.5.5": "System 7.5.5 (RetroBoot)",
        "mac-os-8.1-base": "Mac OS 8.1 (Clean)",
        "mac-os-8.1": "Mac OS 8.1 (Full)",
        "mac-os-9.2.1-ati-32gb": "Mac OS 9.2.1 (Clean)",
        "os921-full": "Mac OS 9.2.1 (Full)",
    ]

    private static let atiFamilies: Set<String> = [
        "os922-deluxe",
        "os922-superdeluxe",
        "os922-mega-deluxe",
        "os922-full",
        "os921-full",
        "mac-os-9.2.1-ati-32gb",
    ]

    private static let knownFamilyOrder: [String: Int] = [
        "os922-superdeluxe": 0,
        "os922-mega-deluxe": 1,
        "os922-deluxe": 2,
        "mac-os-9.2.1-ati-32gb": 3,
        "os922-full": 10,
        "os921-full": 11,
        "mac-os-x-10-4-11-tiger-deluxe": 20,
        "mac-os-x-10-4-11-tiger-period": 21,
        "mac-os-x-10-3-9-panther-period": 22,
        "mac-os-x-10-2-8-jaguar-period": 23,
        "mac-os-8.6-base": 30,
        "mac-os-8-1-deluxe": 31,
        "mac-os-8.1": 32,
        "mac-os-8.1-base": 33,
        "mac-os-7-5-5-deluxe": 40,
        "mac-os-7.5.5": 41,
        "system-7.5.5-base": 42,
        "copland-d11e4-activities": 50,
    ]

    var title: String {
        Self.disambiguatedTitles[family.name] ?? edition?.title ?? machine.name
    }

    var osLabel: String {
        if let edition { return edition.osLabel }
        let version = machine.osVersion
        guard version.first?.isNumber == true else { return version }
        if machine.family == .powerMacOSX { return "Mac OS X \(version)" }
        if version.hasPrefix("7.") { return "System \(version)" }
        return "Mac OS \(version)"
    }

    var category: LibraryOSCategory {
        if machine.family == .powerMac7500 || osLabel.localizedCaseInsensitiveContains("Copland") {
            return .copland
        }
        if machine.family == .powerMacOSX || osLabel.hasPrefix("Mac OS X") {
            return .macOSX
        }
        if osLabel.hasPrefix("System 7") || machine.osVersion.hasPrefix("7.") {
            return .system7
        }
        if osLabel.hasPrefix("Mac OS 8") || machine.osVersion.hasPrefix("8.") {
            return .macOS8
        }
        return .macOS9
    }

    private var usesEditionCopy: Bool {
        guard let edition else { return false }
        return family.version <= edition.baselineVersion
    }

    var tagline: String {
        if usesEditionCopy, let edition { return edition.tagline }
        return Self.cleanSummary(machine.summary)
    }

    var description: String {
        if usesEditionCopy, let edition { return edition.description }
        return Self.cleanSummary(machine.summary)
    }

    var graphicsCard: GraphicsCard {
        guard machine.family == .powerMacG4 else { return .standard }
        if let edition { return edition.graphicsCard }
        return Self.atiFamilies.contains(family.name) ? .atiRadeon : .standard
    }

    /// "os922-deluxe-v6" → ("os922-deluxe", 6). Entries without a version
    /// suffix are their own family.
    static func family(for id: String) -> (name: String, version: Int) {
        if let range = id.range(of: "-v", options: .backwards),
           let version = Int(id[range.upperBound...]) {
            return (String(id[..<range.lowerBound]), version)
        }
        return (id, 0)
    }

    var family: (name: String, version: Int) {
        Self.family(for: machine.id)
    }

    var versionLabel: String? {
        let version = family.version
        guard version > 0 else { return nil }
        let pattern = "\\bv\(version)\\b"
        if title.range(of: pattern, options: [.regularExpression, .caseInsensitive]) != nil {
            return nil
        }
        return "v\(version)"
    }

    func matches(query: String, category filter: LibraryOSCategory) -> Bool {
        guard filter == .all || category == filter else { return false }
        let trimmed = query.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return true }
        var haystack = [title, osLabel, machine.family.label, tagline, description]
        if let edition {
            haystack.append(contentsOf: edition.contents.map(\.category))
            haystack.append(contentsOf: edition.contents.flatMap(\.items))
        }
        if let highlights = machine.highlights {
            haystack.append(contentsOf: highlights)
        }
        if let tags = machine.tags {
            haystack.append(contentsOf: tags)
        }
        return haystack.contains { $0.localizedCaseInsensitiveContains(trimmed) }
    }

    static func cleanSummary(_ summary: String) -> String {
        var text = summary.replacingOccurrences(
            of: "Select 2048 MB of Mac OS X memory and the ATI Radeon for the Deluxe setup.",
            with: "Runs on ClassicMac's standard Mac OS X display; 1024 MB RAM recommended."
        )
        let stripPatterns = [
            "\\s*from the user['’]s CD",
            "\\s*Includes the Quadra 800 ROM this Mac needs \\(Apple['’]s; kept private\\)\\.",
            "\\s*(?:Needs|Requires) RetroBoot (?:\\d+(?:\\.\\d+)*|[^.\\n])*\\.(?=\\s|$)",
            "\\s*RetroBoot lab build\\.",
        ]
        for pattern in stripPatterns {
            text = text.replacingOccurrences(of: pattern, with: "", options: [.regularExpression, .caseInsensitive])
        }
        text = text.replacingOccurrences(of: "[ \\t]{2,}", with: " ", options: .regularExpression)
        text = text.trimmingCharacters(in: .whitespacesAndNewlines)
        return text.isEmpty ? summary.trimmingCharacters(in: .whitespacesAndNewlines) : text
    }

    static func hasOlderVersions(in machines: [DownloadableMachine]) -> Bool {
        let all = machines.map { family(for: $0.id) }
        var newest: [String: Int] = [:]
        for item in all {
            newest[item.name] = max(newest[item.name] ?? 0, item.version)
        }
        return all.contains { newest[$0.name] != $0.version }
    }

    private var defaultRank: (Int, Int, String) {
        if let known = Self.knownFamilyOrder[family.name] {
            return (known, 0, title)
        }
        if let edition {
            return (edition.order, 0, title)
        }
        return (100, category.sortOrder, title)
    }

    /// The catalog in Library order, with older versions of a family last
    /// (and hidden unless asked for).
    static func entries(from machines: [DownloadableMachine], includeOlder: Bool) -> [LibraryEntry] {
        let all = machines.map { LibraryEntry(machine: $0, edition: LibraryEdition.edition(for: $0.id)) }
        var newest: [String: Int] = [:]
        for entry in all {
            newest[entry.family.name] = max(newest[entry.family.name] ?? 0, entry.family.version)
        }
        let current = all.filter { newest[$0.family.name] == $0.family.version }
        let older = all.filter { newest[$0.family.name] != $0.family.version }
        let sortedCurrent = current.sorted { $0.defaultRank < $1.defaultRank }
        guard includeOlder else { return sortedCurrent }
        return sortedCurrent + older.sorted {
            ($0.defaultRank.0, $0.defaultRank.1, $0.title, -$0.family.version) <
                ($1.defaultRank.0, $1.defaultRank.1, $1.title, -$1.family.version)
        }
    }
}
