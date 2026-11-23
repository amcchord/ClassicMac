import Foundation

/// What the Mac OS Library knows about a catalog entry beyond the catalog
/// itself: a friendlier title, what is inside, how to get the most from it,
/// and which graphics card it is prepared for. Catalog entries without an
/// edition still appear, described by the catalog alone.
///
/// The Deluxe and SuperDeluxe contents come from RetroBoot's edition manifests
/// (apps/manifests/superdeluxe-v2.json, docs/guide/deluxe-image.md).
struct LibraryEdition {
    let catalogID: String
    let title: String
    let osLabel: String
    let tagline: String
    let description: String
    let contents: [(category: String, items: [String])]
    let notes: [String]
    /// The card a fresh download starts with. ATI-ready Macs carry ATI's own
    /// Radeon 3D software and RetroBoot Display.
    let graphicsCard: GraphicsCard
    let icon: String
    /// Lower sorts first.
    let order: Int

    static func edition(for id: String) -> LibraryEdition? { all.first { $0.catalogID == id } }

    static let all: [LibraryEdition] = [
        LibraryEdition(
            catalogID: "os922-superdeluxe-v2",
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
                ("Utilities", ["ACTION GoMac and Menus", "After Dark 4.94 (Flying Toasters)", "Audion 3",
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
            icon: "library",
            order: 0
        ),
        LibraryEdition(
            catalogID: "os922-deluxe-v6",
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
                ("Utilities", ["ACTION GoMac and Menus", "After Dark 4.94 (Flying Toasters)", "Audion 3",
                               "SoundJam MP", "Transmit", "Speedometer 4.02", "MacBench 5.0", "Gauge PRO",
                               "USB Overdrive", "Internet Explorer 5.1.7"]),
            ],
            notes: [
                "Open Deluxe Read Me on the desktop. Games are in Macintosh HD:Games and in Start → Games.",
                "Choose 256 colors in Monitors before Prince of Persia. Myst runs at any depth.",
                "Original shareware and demo limits remain.",
            ],
            graphicsCard: .atiRadeon,
            icon: "library",
            order: 1
        ),
        LibraryEdition(
            catalogID: "mac-os-9.2.1-ati-32gb-v4",
            title: "Mac OS 9.2.1",
            osLabel: "Mac OS 9.2.1",
            tagline: "A clean Mac OS 9 to make your own",
            description: "A fresh Mac OS 9.2.1 with Apple’s ATI graphics software and a 32 GB disk that grows on your computer only as you add files. Start here to install your own software.",
            contents: [
                ("Included", ["Mac OS 9.2.1", "32 GB startup disk"]),
            ],
            notes: [
                "Uses the emulated ATI Radeon with Apple’s own Mac OS drivers for accelerated RAVE and OpenGL games.",
                "Choose the Standard graphics card for live window resizing or Black & White, 4 and 16 colors.",
            ],
            graphicsCard: .atiRadeon,
            icon: "library",
            order: 2
        ),
        LibraryEdition(
            catalogID: "copland-d11e4-activities-v3",
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
            icon: "powermac_7500",
            order: 3
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

    var title: String { edition?.title ?? machine.name }

    var osLabel: String {
        if let edition { return edition.osLabel }
        let version = machine.osVersion
        guard version.first?.isNumber == true else { return version }
        if machine.family == .powerMacOSX { return "Mac OS X \(version)" }
        if version.hasPrefix("7.") { return "System \(version)" }
        return "Mac OS \(version)"
    }

    var tagline: String { edition?.tagline ?? machine.summary }
    var description: String { edition?.description ?? machine.summary }
    var icon: String { edition?.icon ?? "library" }
    var graphicsCard: GraphicsCard {
        guard machine.family == .powerMacG4 else { return .standard }
        // New catalog revisions keep their edition's prepared hardware even
        // when this app has no revision-specific contents description yet.
        return edition?.graphicsCard ?? (["os922-deluxe", "os922-superdeluxe", "mac-os-9.2.1-ati-32gb"].contains(family.name) ? .atiRadeon : .standard)
    }

    /// "os922-deluxe-v6" → ("os922-deluxe", 6). Entries without a version
    /// suffix are their own family.
    var family: (name: String, version: Int) {
        let id = machine.id
        if let range = id.range(of: "-v", options: .backwards),
           let version = Int(id[range.upperBound...]) {
            return (String(id[..<range.lowerBound]), version)
        }
        return (id, 0)
    }

    var versionLabel: String? {
        let version = family.version
        return version > 0 ? "v\(version)" : nil
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
        func rank(_ e: LibraryEntry) -> Int { e.edition?.order ?? 100 }
        let sortedCurrent = current.sorted { (rank($0), $0.title) < (rank($1), $1.title) }
        guard includeOlder else { return sortedCurrent }
        return sortedCurrent + older.sorted { (rank($0), $0.title, -$0.family.version) < (rank($1), $1.title, -$1.family.version) }
    }
}
