import AppKit

enum MonokaiTheme {
    // Chłodny grafit w stylu ciemnego motywu Antigravity / VS Code Monokai Pro
    static let bg = NSColor(calibratedRed: 0.110, green: 0.114, blue: 0.133, alpha: 1.0)        // #1C1D22 (panele boczne)
    static let bgDark = NSColor(calibratedRed: 0.082, green: 0.086, blue: 0.102, alpha: 1.0)    // #15161A (górny pasek / okno)
    static let bgBlack = NSColor(calibratedRed: 0.055, green: 0.059, blue: 0.071, alpha: 1.0)   // #0E0F12
    static let border = NSColor(calibratedRed: 0.212, green: 0.220, blue: 0.259, alpha: 1.0)    // #363842
    static let text = NSColor(calibratedRed: 0.929, green: 0.937, blue: 0.957, alpha: 1.0)      // #EDEFF4
    static let muted = NSColor(calibratedRed: 0.518, green: 0.545, blue: 0.620, alpha: 1.0)     // #848B9E
    static let green = NSColor(calibratedRed: 0.651, green: 0.886, blue: 0.180, alpha: 1.0)     // #A6E22E
    static let cyan = NSColor(calibratedRed: 0.369, green: 0.788, blue: 0.961, alpha: 1.0)      // #5EC9F5
    static let orange = NSColor(calibratedRed: 0.992, green: 0.592, blue: 0.122, alpha: 1.0)    // #FD971F
    static let yellow = NSColor(calibratedRed: 0.902, green: 0.859, blue: 0.455, alpha: 1.0)    // #E6DB74
    static let pink = NSColor(calibratedRed: 0.847, green: 0.435, blue: 0.796, alpha: 1.0)      // #D86FCB

    static func makePanelView() -> NSView {
        let v = NSView()
        v.wantsLayer = true
        v.layer?.backgroundColor = bg.cgColor
        return v
    }
}
