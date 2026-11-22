import Foundation

enum AppLanguage: String, CaseIterable {
    case auto
    case pl
    case en
}

enum L10n {
    private static let defaultsKey = "AmigaEmu.language"

    static var setting: AppLanguage {
        get {
            if let raw = UserDefaults.standard.string(forKey: defaultsKey),
               let lang = AppLanguage(rawValue: raw) {
                return lang
            }
            return .auto
        }
        set {
            UserDefaults.standard.set(newValue.rawValue, forKey: defaultsKey)
        }
    }

    static var isPolish: Bool {
        switch setting {
        case .pl: return true
        case .en: return false
        case .auto:
            return Locale.preferredLanguages.first?.lowercased().hasPrefix("pl") ?? false
        }
    }

    static func t(_ pl: String, _ en: String) -> String {
        return isPolish ? pl : en
    }
}
