import Foundation

enum AmigaModelType: String, CaseIterable, Codable {
    case a500 = "Amiga 500"
    case a600 = "Amiga 600"
    case a1200 = "Amiga 1200"
    case a4000 = "Amiga 4000"

    var subtitle: String {
        switch self {
        case .a500:  return "OCS • 68000 • 1 MB Chip"
        case .a600:  return "ECS • 68000 • 1 MB Chip"
        case .a1200: return "AGA • 68EC020 • 2 MB Chip"
        case .a4000: return "AGA • 68030 + 68882 • 2 MB Chip"
        }
    }

    var modelCode: Int32 {
        switch self {
        case .a500:  return 0
        case .a600:  return 1
        case .a1200: return 2
        case .a4000: return 3
        }
    }

    var defaultRomPath: String {
        let base = AmigaPaths.appRoot + "/Kickstart"
        switch self {
        case .a500:  return "\(base)/A500-KS1.3.rom"
        case .a600:  return "\(base)/A600-KS3.1.rom"
        case .a1200: return "\(base)/A1200-KS3.1.rom"
        case .a4000: return "\(base)/A4000-KS3.1.rom"
        }
    }
}

struct FloppyDriveSlot: Codable {
    let unit: Int
    var diskPath: String?
    var readOnly: Bool = false

    enum CodingKeys: String, CodingKey {
        case unit, diskPath, readOnly
    }

    init(unit: Int, diskPath: String?, readOnly: Bool = false) {
        self.unit = unit
        self.diskPath = diskPath
        self.readOnly = readOnly
    }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        self.unit = try c.decode(Int.self, forKey: .unit)
        self.diskPath = try c.decodeIfPresent(String.self, forKey: .diskPath)
        self.readOnly = try c.decodeIfPresent(Bool.self, forKey: .readOnly) ?? false
    }
}

struct HardDriveSlot: Codable {
    let unit: Int
    var imagePath: String
}

final class AmigaProfileConfig {
    let model: AmigaModelType
    var romPath: String
    var floppies: [FloppyDriveSlot]
    var hardDrives: [HardDriveSlot]
    var fastRamMB: Int
    var a500Chip512KB: Bool
    var a600Chip2MB: Bool

    init(model: AmigaModelType) {
        self.model = model
        if FileManager.default.fileExists(atPath: model.defaultRomPath) {
            self.romPath = model.defaultRomPath
        } else {
            self.romPath = ""
        }
        self.floppies = [FloppyDriveSlot(unit: 0, diskPath: nil)]
        self.hardDrives = []
        self.fastRamMB = (model == .a4000) ? 64 : 0
        self.a500Chip512KB = false
        self.a600Chip2MB = false
    }

    @discardableResult
    func addFloppyDrive() -> Int? {
        let used = Set(floppies.map { $0.unit })
        for u in 0..<4 where !used.contains(u) {
            floppies.append(FloppyDriveSlot(unit: u, diskPath: nil))
            floppies.sort { $0.unit < $1.unit }
            return u
        }
        return nil
    }

    func removeFloppyDrive(unit: Int) {
        guard unit != 0 else { return }
        floppies.removeAll { $0.unit == unit }
    }

    @discardableResult
    func addHardDrive(path: String) -> Int {
        let nextUnit = (hardDrives.map { $0.unit }.max() ?? -1) + 1
        hardDrives.append(HardDriveSlot(unit: nextUnit, imagePath: path))
        return nextUnit
    }

    func removeHardDrive(unit: Int) {
        hardDrives.removeAll { $0.unit == unit }
    }
}

struct PersistedProfile: Codable {
    var romPath: String
    var floppies: [FloppyDriveSlot]
    var hardDrives: [HardDriveSlot]
    var fastRamMB: Int?
    var a500Chip512KB: Bool?
    var a600Chip2MB: Bool?
}

struct PersistedConfig: Codable {
    var activeModel: AmigaModelType
    var profiles: [String: PersistedProfile]
}

enum ConfigStore {
    private static var configURL: URL {
        let appSupport = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first
            ?? URL(fileURLWithPath: NSHomeDirectory()).appendingPathComponent("Library/Application Support")
        return appSupport.appendingPathComponent("AmigaEmu", isDirectory: true).appendingPathComponent("config.json")
    }

    static func save(activeModel: AmigaModelType, profiles: [AmigaModelType: AmigaProfileConfig]) {
        var encodedProfiles: [String: PersistedProfile] = [:]
        for (model, cfg) in profiles {
            encodedProfiles[model.rawValue] = PersistedProfile(
                romPath: cfg.romPath,
                floppies: cfg.floppies,
                hardDrives: cfg.hardDrives,
                fastRamMB: cfg.fastRamMB,
                a500Chip512KB: cfg.a500Chip512KB,
                a600Chip2MB: cfg.a600Chip2MB
            )
        }
        let payload = PersistedConfig(activeModel: activeModel, profiles: encodedProfiles)
        let url = configURL
        let fm = FileManager.default
        try? fm.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        if let data = try? JSONEncoder().encode(payload) {
            try? data.write(to: url, options: .atomic)
        }
    }

    static func load() -> (activeModel: AmigaModelType, profiles: [AmigaModelType: AmigaProfileConfig]) {
        let result: [AmigaModelType: AmigaProfileConfig] = [
            .a500: AmigaProfileConfig(model: .a500),
            .a600: AmigaProfileConfig(model: .a600),
            .a1200: AmigaProfileConfig(model: .a1200),
            .a4000: AmigaProfileConfig(model: .a4000)
        ]
        var active: AmigaModelType = .a600

        let fm = FileManager.default
        guard let data = try? Data(contentsOf: configURL),
              let decoded = try? JSONDecoder().decode(PersistedConfig.self, from: data) else {
            return (active, result)
        }

        active = decoded.activeModel
        for model in AmigaModelType.allCases {
            guard let saved = decoded.profiles[model.rawValue], let cfg = result[model] else { continue }
            if fm.fileExists(atPath: saved.romPath) {
                cfg.romPath = saved.romPath
            }
            var floppies = saved.floppies
                .filter { $0.unit >= 0 && $0.unit < 4 }
                .map { slot -> FloppyDriveSlot in
                    if let p = slot.diskPath, !fm.fileExists(atPath: p) {
                        return FloppyDriveSlot(unit: slot.unit, diskPath: nil, readOnly: slot.readOnly)
                    }
                    return slot
                }
            if !floppies.contains(where: { $0.unit == 0 }) {
                floppies.insert(FloppyDriveSlot(unit: 0, diskPath: nil), at: 0)
            }
            cfg.floppies = floppies.sorted { $0.unit < $1.unit }
            cfg.hardDrives = saved.hardDrives.filter { fm.fileExists(atPath: $0.imagePath) }
            if let fast = saved.fastRamMB, [0, 2, 4, 8, 16, 64, 128].contains(fast) {
                cfg.fastRamMB = fast
            }
            if let chip512 = saved.a500Chip512KB {
                cfg.a500Chip512KB = chip512
            }
            if let chip2mb = saved.a600Chip2MB {
                cfg.a600Chip2MB = chip2mb
            }
        }
        return (active, result)
    }
}

struct DiskImageItem {
    let name: String
    let category: String
    let path: String
}

enum AmigaPaths {
    static let appRoot: String = {
        FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("AmigaEmu").path
    }()

    static func ensureDirectoryStructure() {
        let fm = FileManager.default
        let subdirs = ["Kickstart", "adf", "adf/Saves", "Saves", "Hard Drives"]
        for sub in subdirs {
            let url = URL(fileURLWithPath: appRoot).appendingPathComponent(sub, isDirectory: true)
            if !fm.fileExists(atPath: url.path) {
                try? fm.createDirectory(at: url, withIntermediateDirectories: true)
            }
        }
    }

    static var savesDirectoryPath: String {
        let adfSaves = appRoot + "/adf/Saves"
        if FileManager.default.fileExists(atPath: adfSaves) {
            return adfSaves
        }
        return appRoot + "/Saves"
    }

    static func preferredDirectory(subpath: String) -> URL {
        let url = URL(fileURLWithPath: appRoot).appendingPathComponent(subpath, isDirectory: true)
        if FileManager.default.fileExists(atPath: url.path) {
            return url
        }
        return URL(fileURLWithPath: appRoot, isDirectory: true)
    }
}

enum DiskLibrary {
    static let rootPath: String = {
        let adf = AmigaPaths.appRoot + "/adf"
        return FileManager.default.fileExists(atPath: adf) ? adf : AmigaPaths.appRoot
    }()

    static func scanImages() -> [DiskImageItem] {
        let fm = FileManager.default
        let rootURL = URL(fileURLWithPath: rootPath)
        guard let enumerator = fm.enumerator(
            at: rootURL,
            includingPropertiesForKeys: [.isRegularFileKey],
            options: [.skipsHiddenFiles]
        ) else {
            return []
        }

        var items: [DiskImageItem] = []
        var seenPaths = Set<String>()
        for case let fileURL as URL in enumerator {
            let ext = fileURL.pathExtension.lowercased()
            guard ext == "adf" || ext == "scp" || ext == "arf" else { continue }
            seenPaths.insert(fileURL.path)
            let rel = fileURL.path.replacingOccurrences(of: rootPath + "/", with: "")
            let parts = rel.split(separator: "/")
            let category = parts.count > 1 ? String(parts[0]) : (ext == "arf" ? "Saves" : "Inne")
            items.append(DiskImageItem(
                name: fileURL.deletingPathExtension().lastPathComponent,
                category: category,
                path: fileURL.path
            ))
        }
        for extraSaves in [AmigaPaths.appRoot + "/Saves", AmigaPaths.appRoot + "/saves"] {
            let savesURL = URL(fileURLWithPath: extraSaves)
            if let savesEnum = fm.enumerator(
                at: savesURL,
                includingPropertiesForKeys: [.isRegularFileKey],
                options: [.skipsHiddenFiles]
            ) {
                for case let fileURL as URL in savesEnum {
                    let ext = fileURL.pathExtension.lowercased()
                    guard ext == "arf", !seenPaths.contains(fileURL.path) else { continue }
                    seenPaths.insert(fileURL.path)
                    items.append(DiskImageItem(
                        name: fileURL.deletingPathExtension().lastPathComponent,
                        category: "Saves",
                        path: fileURL.path
                    ))
                }
            }
        }
        return items.sorted {
            if $0.category != $1.category { return $0.category < $1.category }
            return $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending
        }
    }
}
