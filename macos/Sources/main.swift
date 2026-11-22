import AppKit
import AmigaCore

private final class AmigaLedBarView: NSView {
    private let pwrDot = NSView()
    private let dfDot = NSView()
    private let hdDot = NSView()
    private let dfTrackLabel = NSTextField(labelWithString: "--")
    private var lastState: UInt32 = 0xFFFF_FFFF

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        wantsLayer = true
        layer?.backgroundColor = MonokaiTheme.bgDark.cgColor
        layer?.cornerRadius = 6
        layer?.borderWidth = 1
        layer?.borderColor = MonokaiTheme.border.cgColor

        let stack = NSStackView()
        stack.orientation = .horizontal
        stack.spacing = 10
        stack.alignment = .centerY
        stack.translatesAutoresizingMaskIntoConstraints = false
        addSubview(stack)

        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 10),
            stack.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -10),
            stack.topAnchor.constraint(equalTo: topAnchor, constant: 4),
            stack.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -4)
        ])

        func makeLedUnit(label: String, dot: NSView, extra: NSView? = nil) -> NSView {
            let row = NSStackView()
            row.orientation = .horizontal
            row.spacing = 4
            row.alignment = .centerY

            dot.wantsLayer = true
            dot.layer?.cornerRadius = 2.5
            dot.translatesAutoresizingMaskIntoConstraints = false
            NSLayoutConstraint.activate([
                dot.widthAnchor.constraint(equalToConstant: 14),
                dot.heightAnchor.constraint(equalToConstant: 5)
            ])

            let lbl = NSTextField(labelWithString: label)
            lbl.font = .monospacedSystemFont(ofSize: 9, weight: .bold)
            lbl.textColor = NSColor(white: 0.78, alpha: 1.0)

            row.addArrangedSubview(lbl)
            row.addArrangedSubview(dot)
            if let extra = extra {
                row.addArrangedSubview(extra)
            }
            return row
        }

        dfTrackLabel.font = .monospacedDigitSystemFont(ofSize: 9, weight: .medium)
        dfTrackLabel.textColor = NSColor(white: 0.55, alpha: 1.0)

        stack.addArrangedSubview(makeLedUnit(label: "PWR", dot: pwrDot))
        stack.addArrangedSubview(makeLedUnit(label: "DF", dot: dfDot, extra: dfTrackLabel))
        stack.addArrangedSubview(makeLedUnit(label: "HD", dot: hdDot))

        update(state: 0x01)
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override var intrinsicContentSize: NSSize {
        return NSSize(width: 162, height: 22)
    }

    func update(state: UInt32) {
        guard state != lastState else { return }
        lastState = state

        let pwrOn = (state & 0x01) != 0
        let filterBright = (state & 0x08) != 0
        let dfOn = (state & 0x02) != 0
        let hdOn = (state & 0x04) != 0
        let cyl = (state >> 8) & 0xFF

        let pwrColor = pwrOn
            ? NSColor(red: 0.15, green: filterBright ? 0.98 : 0.72, blue: 0.25, alpha: 1.0)
            : NSColor(white: 0.22, alpha: 1.0)
        pwrDot.layer?.backgroundColor = pwrColor.cgColor

        let dfColor = dfOn
            ? NSColor(red: 0.98, green: 0.65, blue: 0.10, alpha: 1.0)
            : NSColor(red: 0.22, green: 0.17, blue: 0.08, alpha: 1.0)
        dfDot.layer?.backgroundColor = dfColor.cgColor
        dfTrackLabel.stringValue = (dfOn && cyl < 80) ? String(format: "%02d", cyl) : "--"
        dfTrackLabel.textColor = dfOn ? NSColor(red: 0.98, green: 0.78, blue: 0.35, alpha: 1.0) : NSColor(white: 0.45, alpha: 1.0)

        let hdColor = hdOn
            ? NSColor(red: 0.96, green: 0.22, blue: 0.18, alpha: 1.0)
            : NSColor(red: 0.24, green: 0.10, blue: 0.10, alpha: 1.0)
        hdDot.layer?.backgroundColor = hdColor.cgColor
    }
}

private final class CenterScreenViewController: NSViewController {
    let metalView: AmigaMetalView

    init(metalView: AmigaMetalView) {
        self.metalView = metalView
        super.init(nibName: nil, bundle: nil)
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override func loadView() {
        let container = NSView()
        container.wantsLayer = true
        container.layer?.backgroundColor = NSColor.black.cgColor

        metalView.translatesAutoresizingMaskIntoConstraints = false
        container.addSubview(metalView)
        NSLayoutConstraint.activate([
            metalView.topAnchor.constraint(equalTo: container.topAnchor),
            metalView.bottomAnchor.constraint(equalTo: container.bottomAnchor),
            metalView.leadingAnchor.constraint(equalTo: container.leadingAnchor),
            metalView.trailingAnchor.constraint(equalTo: container.trailingAnchor)
        ])
        self.view = container
    }
}

private final class AmigaWindow: NSWindow {
    override var canBecomeKey: Bool { true }
    override var canBecomeMain: Bool { true }
}

final class AppDelegate: NSObject, NSApplicationDelegate, NSWindowDelegate, NSToolbarDelegate, LeftSidebarDelegate, RightDiskBrowserDelegate {
    private var window: AmigaWindow!
    private var mainToolbar: NSToolbar!
    private var isTrueFullScreen = false
    private var widescreenFullScreen = true
    private var widescreenMenuItem: NSMenuItem?
    private var savedWindowFrame: NSRect = .zero
    private var savedLeftCollapsed = false
    private var savedRightCollapsed = false
    private var splitVC: NSSplitViewController!
    private var leftSidebarItem: NSSplitViewItem!
    private var rightSidebarItem: NSSplitViewItem!
    private var leftVC: LeftSidebarViewController!
    private var rightVC: RightDiskBrowserViewController!
    private var metalView: AmigaMetalView!
    private var audioOutput: AmigaAudioOutput!
    private var machine: OpaquePointer!
    private var mouseMenuItem: NSMenuItem?
    private let ledBarView = AmigaLedBarView()
    private var ledTimer: Timer?

    private var profiles: [AmigaModelType: AmigaProfileConfig] = [
        .a500: AmigaProfileConfig(model: .a500),
        .a600: AmigaProfileConfig(model: .a600),
        .a1200: AmigaProfileConfig(model: .a1200)
    ]
    private var activeModel: AmigaModelType = .a600

    private let tbLeftSidebar = NSToolbarItem.Identifier("toggleLeftSidebar")
    private let tbLeds = NSToolbarItem.Identifier("amigaLeds")
    private let tbReset = NSToolbarItem.Identifier("resetAmiga")
    private let tbWarp = NSToolbarItem.Identifier("toggleWarp")
    private let tbJoy = NSToolbarItem.Identifier("toggleJoystick")
    private let tbFullScreen = NSToolbarItem.Identifier("toggleFullScreen")
    private let tbRightSidebar = NSToolbarItem.Identifier("toggleRightSidebar")

    func applicationDidFinishLaunching(_ notification: Notification) {
        AmigaPaths.ensureDirectoryStructure()
        NSApp.applicationIconImage = AmigaAppIcon.makeIcon()
        let loaded = ConfigStore.load()
        activeModel = loaded.activeModel
        profiles = loaded.profiles

        machine = amiga_create()
        if let profile = profiles[activeModel] {
            applyProfileToMachine(profile)
        } else {
            amiga_reset(machine)
        }

        metalView = AmigaMetalView(frame: NSRect(x: 0, y: 0, width: 800, height: 600), machine: machine)
        metalView.onMouseGrabChanged = { [weak self] grabbed in
            self?.mouseMenuItem?.state = grabbed ? .on : .off
            self?.updateWindowTitle()
        }

        leftVC = LeftSidebarViewController()
        leftVC.delegate = self
        leftVC.configure(profiles: profiles, selected: activeModel)

        let centerVC = CenterScreenViewController(metalView: metalView)

        rightVC = RightDiskBrowserViewController()
        rightVC.delegate = self
        if let profile = profiles[activeModel] {
            rightVC.updateAvailableUnits(profile.floppies.map { $0.unit })
        }

        splitVC = NSSplitViewController()
        leftSidebarItem = NSSplitViewItem(sidebarWithViewController: leftVC)
        leftSidebarItem.minimumThickness = 230
        leftSidebarItem.maximumThickness = 320
        leftSidebarItem.canCollapse = true

        let centerItem = NSSplitViewItem(viewController: centerVC)
        centerItem.minimumThickness = 480

        rightSidebarItem = NSSplitViewItem(inspectorWithViewController: rightVC)
        rightSidebarItem.minimumThickness = 230
        rightSidebarItem.maximumThickness = 340
        rightSidebarItem.canCollapse = true

        splitVC.addSplitViewItem(leftSidebarItem)
        splitVC.addSplitViewItem(centerItem)
        splitVC.addSplitViewItem(rightSidebarItem)

        let contentRect = NSRect(x: 0, y: 0, width: 1260, height: 700)
        window = AmigaWindow(
            contentRect: contentRect,
            styleMask: [.titled, .closable, .miniaturizable, .resizable, .fullSizeContentView],
            backing: .buffered,
            defer: false
        )
        window.collectionBehavior = [.fullScreenPrimary]
        window.appearance = NSAppearance(named: .darkAqua)
        window.backgroundColor = MonokaiTheme.bgDark
        window.contentViewController = splitVC
        window.delegate = self

        let toolbar = NSToolbar(identifier: "AmigaToolbar")
        toolbar.delegate = self
        toolbar.displayMode = .iconOnly
        toolbar.allowsUserCustomization = false
        mainToolbar = toolbar
        window.toolbar = toolbar
        window.toolbarStyle = .unified

        updateWindowTitle()
        window.center()
        window.makeFirstResponder(metalView)
        window.makeKeyAndOrderFront(nil)

        audioOutput = AmigaAudioOutput(machine: machine)

        setupMenu()
        ledTimer = Timer.scheduledTimer(withTimeInterval: 1.0 / 30.0, repeats: true) { [weak self] _ in
            guard let self = self, let m = self.machine else { return }
            self.ledBarView.update(state: amiga_get_led_state(m))
        }
        NSApp.activate(ignoringOtherApps: true)
    }

    func applicationWillTerminate(_ notification: Notification) {
        ledTimer?.invalidate()
        ledTimer = nil
        ConfigStore.save(activeModel: activeModel, profiles: profiles)
        audioOutput?.stop()
        if let machine = machine {
            amiga_destroy(machine)
        }
    }

    private func saveConfig() {
        ConfigStore.save(activeModel: activeModel, profiles: profiles)
    }

    private func applyProfileToMachine(_ profile: AmigaProfileConfig) {
        amiga_set_model(machine, profile.model.modelCode)
        amiga_set_fast_ram_mb(machine, UInt32(profile.fastRamMB))
        if FileManager.default.fileExists(atPath: profile.romPath) {
            _ = amiga_load_kickstart_path(machine, profile.romPath)
        }
        for u in 0..<4 {
            if let slot = profile.floppies.first(where: { $0.unit == u }) {
                amiga_set_floppy_enabled(machine, Int32(u), 1)
                amiga_set_floppy_write_protect(machine, Int32(u), slot.readOnly ? 1 : 0)
                if let p = slot.diskPath {
                    _ = amiga_insert_adf_unit(machine, Int32(u), p)
                } else {
                    amiga_eject_adf_unit(machine, Int32(u))
                }
            } else {
                amiga_eject_adf_unit(machine, Int32(u))
                amiga_set_floppy_enabled(machine, Int32(u), 0)
            }
        }
        amiga_clear_hdfs(machine)
        for hd in profile.hardDrives {
            _ = amiga_add_hdf_path(machine, hd.imagePath)
        }
        amiga_reset(machine)
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        return true
    }

    // MARK: - NSToolbarDelegate

    func toolbarDefaultItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        return [
            tbLeftSidebar,
            .flexibleSpace,
            tbLeds,
            .space,
            tbReset,
            tbWarp,
            tbJoy,
            tbFullScreen,
            .space,
            tbRightSidebar
        ]
    }

    func toolbarAllowedItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        return toolbarDefaultItemIdentifiers(toolbar)
    }

    func toolbar(_ toolbar: NSToolbar, itemForItemIdentifier itemIdentifier: NSToolbarItem.Identifier, willBeInsertedIntoToolbar flag: Bool) -> NSToolbarItem? {
        let item = NSToolbarItem(itemIdentifier: itemIdentifier)
        item.target = self
        switch itemIdentifier {
        case tbLeftSidebar:
            item.label = L10n.t("Maszyny", "Machines")
            item.toolTip = L10n.t("Pokaż/ukryj panel maszyn Amigi (Cmd+1)", "Show/hide Amiga machines panel (Cmd+1)")
            item.image = NSImage(systemSymbolName: "sidebar.left", accessibilityDescription: item.label)
            item.action = #selector(toggleLeftSidebar)
        case tbLeds:
            item.label = L10n.t("Diody Amigi", "Amiga LEDs")
            item.toolTip = L10n.t("PWR (zasilanie) • DF (stacja dyskietek + ścieżka) • HD (dysk twardy)", "PWR (power) • DF (floppy + track) • HD (hard drive)")
            item.view = ledBarView
        case tbRightSidebar:
            item.label = L10n.t("Obrazy dysków", "Disk Images")
            item.toolTip = L10n.t("Pokaż/ukryj panel obrazów dysków (Cmd+2)", "Show/hide disk images panel (Cmd+2)")
            item.image = NSImage(systemSymbolName: "sidebar.right", accessibilityDescription: item.label)
            item.action = #selector(toggleRightSidebar)
        case tbReset:
            item.label = "Reset"
            item.toolTip = L10n.t("Reset Amigi (Cmd+R)", "Reset Amiga (Cmd+R)")
            item.image = NSImage(systemSymbolName: "arrow.clockwise", accessibilityDescription: "Reset")
            item.action = #selector(resetMachine)
        case tbWarp:
            item.label = "Warp 12x"
            item.toolTip = L10n.t("Tryb Warp 12x (Cmd+T)", "Warp 12x mode (Cmd+T)")
            item.image = NSImage(systemSymbolName: "hare", accessibilityDescription: "Warp")
            item.action = #selector(toggleWarpToolbar)
        case tbJoy:
            item.label = "Joystick"
            item.toolTip = L10n.t("Joystick na strzałkach + Shift (Cmd+J)", "Arrow keys + Shift Joystick (Cmd+J)")
            item.image = NSImage(systemSymbolName: "gamecontroller", accessibilityDescription: "Joystick")
            item.action = #selector(toggleJoystickToolbar)
        case tbFullScreen:
            item.label = L10n.t("Pełen ekran", "Full Screen")
            item.toolTip = L10n.t("Pełen ekran (Cmd+F)", "Full Screen (Cmd+F)")
            item.image = NSImage(systemSymbolName: "arrow.up.left.and.arrow.down.right", accessibilityDescription: item.label)
            item.action = #selector(toggleFullScreen)
        default:
            return nil
        }
        return item
    }

    // MARK: - Menu

    private func setupMenu() {
        let mainMenu = NSMenu()

        let appItem = NSMenuItem()
        mainMenu.addItem(appItem)
        let appMenu = NSMenu()
        appItem.submenu = appMenu
        appMenu.addItem(withTitle: L10n.t("Otwórz Kickstart ROM...", "Open Kickstart ROM..."), action: #selector(openRom), keyEquivalent: "o")
        appMenu.addItem(withTitle: L10n.t("Włóż dyskietkę (.adf)...", "Insert Floppy (.adf)..."), action: #selector(insertAdf), keyEquivalent: "d")
        appMenu.addItem(withTitle: L10n.t("Resetuj Amigę", "Reset Amiga"), action: #selector(resetMachine), keyEquivalent: "r")
        appMenu.addItem(NSMenuItem.separator())
        appMenu.addItem(withTitle: L10n.t("Tryb Warp", "Toggle Warp Speed"), action: #selector(toggleWarp), keyEquivalent: "t")
        appMenu.addItem(withTitle: L10n.t("Tryb Joysticka (Strzałki + Shift)", "Toggle Joystick Mode (Arrows + Shift)"), action: #selector(toggleJoystick), keyEquivalent: "j")
        mouseMenuItem = appMenu.addItem(withTitle: L10n.t("Przechwyć mysz", "Capture Mouse"), action: #selector(toggleMouseGrab), keyEquivalent: "m")
        appMenu.addItem(withTitle: L10n.t("Pełen ekran", "Toggle Full Screen"), action: #selector(toggleFullScreen), keyEquivalent: "f")
        let wsItem = appMenu.addItem(withTitle: L10n.t("Widescreen na pełnym ekranie", "Widescreen in Full Screen"), action: #selector(toggleWidescreenFullScreen(_:)), keyEquivalent: "F")
        wsItem.state = widescreenFullScreen ? .on : .off
        widescreenMenuItem = wsItem
        appMenu.addItem(NSMenuItem.separator())
        appMenu.addItem(withTitle: L10n.t("Panel maszyn", "Toggle Machines Panel"), action: #selector(toggleLeftSidebar), keyEquivalent: "1")
        appMenu.addItem(withTitle: L10n.t("Panel obrazów dysków", "Toggle Disks Panel"), action: #selector(toggleRightSidebar), keyEquivalent: "2")
        appMenu.addItem(NSMenuItem.separator())
        let langRoot = NSMenuItem(title: L10n.t("Język / Language", "Language"), action: nil, keyEquivalent: "")
        let langMenu = NSMenu()
        let autoItem = NSMenuItem(title: "Auto (System)", action: #selector(selectLanguage(_:)), keyEquivalent: "")
        autoItem.tag = 0
        autoItem.state = (L10n.setting == .auto) ? .on : .off
        let plItem = NSMenuItem(title: "Polski", action: #selector(selectLanguage(_:)), keyEquivalent: "")
        plItem.tag = 1
        plItem.state = (L10n.setting == .pl) ? .on : .off
        let enItem = NSMenuItem(title: "English", action: #selector(selectLanguage(_:)), keyEquivalent: "")
        enItem.tag = 2
        enItem.state = (L10n.setting == .en) ? .on : .off
        langMenu.addItem(autoItem)
        langMenu.addItem(plItem)
        langMenu.addItem(enItem)
        langRoot.submenu = langMenu
        appMenu.addItem(langRoot)
        appMenu.addItem(NSMenuItem.separator())
        appMenu.addItem(withTitle: L10n.t("Zakończ AmigaEmu", "Quit AmigaEmu"), action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")

        NSApp.mainMenu = mainMenu
    }

    @objc private func selectLanguage(_ sender: NSMenuItem) {
        switch sender.tag {
        case 1: L10n.setting = .pl
        case 2: L10n.setting = .en
        default: L10n.setting = .auto
        }
        setupMenu()
        leftVC.reloadUI()
        rightVC.refreshLocalization()
    }

    @objc private func toggleLeftSidebar() {
        leftSidebarItem.animator().isCollapsed.toggle()
    }

    @objc private func toggleRightSidebar() {
        rightSidebarItem.animator().isCollapsed.toggle()
    }

    @objc private func openRom() {
        guard let profile = profiles[activeModel] else { return }
        sidebarDidRequestRomPick(for: profile)
    }

    @objc private func insertAdf() {
        guard let profile = profiles[activeModel] else { return }
        sidebarDidRequestInsertFloppy(unit: 0, for: profile)
    }

    @objc private func resetMachine() {
        if let profile = profiles[activeModel] {
            amiga_clear_hdfs(machine)
            for hd in profile.hardDrives {
                _ = amiga_add_hdf_path(machine, hd.imagePath)
            }
        }
        amiga_reset(machine)
        window.makeFirstResponder(metalView)
    }

    private func updateWindowTitle() {
        var tags: [String] = []
        if metalView.joystickMode { tags.append("[JOY]") }
        if metalView.mouseGrabbed { tags.append("[MOUSE - Cmd+M release]") }
        if metalView.warpMode { tags.append("[WARP 12x]") }
        let base = "\(activeModel.rawValue) — Native macOS Emulator"
        window.title = tags.isEmpty ? base : (base + " " + tags.joined(separator: " "))
    }

    func windowDidResignKey(_ notification: Notification) {
        metalView.mouseGrabbed = false
    }

    @objc private func toggleMouseGrab(_ sender: NSMenuItem) {
        metalView.mouseGrabbed.toggle()
        sender.state = metalView.mouseGrabbed ? .on : .off
        updateWindowTitle()
    }

    @objc private func toggleFullScreen() {
        if window.styleMask.contains(.fullScreen) {
            window.toggleFullScreen(nil)
            return
        }
        if !isTrueFullScreen {
            savedWindowFrame = window.frame
            savedLeftCollapsed = leftSidebarItem.isCollapsed
            savedRightCollapsed = rightSidebarItem.isCollapsed
            isTrueFullScreen = true

            leftSidebarItem.isCollapsed = true
            rightSidebarItem.isCollapsed = true
            window.toolbar = nil
            window.styleMask = [.borderless]
            NSApp.presentationOptions = [.hideDock, .hideMenuBar]
            let targetFrame = (window.screen ?? NSScreen.main)?.frame ?? savedWindowFrame
            window.setFrame(targetFrame, display: true)
            metalView.stretchWidescreen = widescreenFullScreen
            window.makeKeyAndOrderFront(nil)
            window.makeFirstResponder(metalView)
        } else {
            isTrueFullScreen = false
            metalView.stretchWidescreen = false
            NSApp.presentationOptions = []
            window.styleMask = [.titled, .closable, .miniaturizable, .resizable, .fullSizeContentView]
            window.toolbar = mainToolbar
            window.toolbarStyle = .unified
            leftSidebarItem.isCollapsed = savedLeftCollapsed
            rightSidebarItem.isCollapsed = savedRightCollapsed
            window.setFrame(savedWindowFrame, display: true)
            updateWindowTitle()
            window.makeKeyAndOrderFront(nil)
            window.makeFirstResponder(metalView)
        }
    }

    @objc private func toggleWidescreenFullScreen(_ sender: NSMenuItem) {
        widescreenFullScreen.toggle()
        sender.state = widescreenFullScreen ? .on : .off
        if isTrueFullScreen {
            metalView.stretchWidescreen = widescreenFullScreen
        }
    }

    @objc private func toggleJoystick(_ sender: NSMenuItem) {
        metalView.joystickMode.toggle()
        sender.state = metalView.joystickMode ? .on : .off
        metalView.resetJoystickState()
        updateWindowTitle()
    }

    @objc private func toggleJoystickToolbar() {
        metalView.joystickMode.toggle()
        metalView.resetJoystickState()
        updateWindowTitle()
        window.makeFirstResponder(metalView)
    }

    @objc private func toggleWarp(_ sender: NSMenuItem) {
        metalView.warpMode.toggle()
        sender.state = metalView.warpMode ? .on : .off
        updateWindowTitle()
    }

    @objc private func toggleWarpToolbar() {
        metalView.warpMode.toggle()
        updateWindowTitle()
        window.makeFirstResponder(metalView)
    }

    // MARK: - LeftSidebarDelegate

    func sidebarDidSelectModel(_ model: AmigaModelType) {
        activeModel = model
        if let profile = profiles[model] {
            rightVC.updateAvailableUnits(profile.floppies.map { $0.unit })
            sidebarDidRequestApplyAndReset(profile: profile)
        } else {
            updateWindowTitle()
        }
    }

    func sidebarDidRequestRomPick(for profile: AmigaProfileConfig) {
        let panel = NSOpenPanel()
        panel.directoryURL = AmigaPaths.preferredDirectory(subpath: "Kickstart")
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        if panel.runModal() == .OK, let url = panel.url {
            profile.romPath = url.path
            leftVC.reloadUI()
            saveConfig()
            if profile.model == activeModel && amiga_load_kickstart_path(machine, url.path) != 0 {
                amiga_clear_hdfs(machine)
        for hd in profile.hardDrives {
            _ = amiga_add_hdf_path(machine, hd.imagePath)
        }
        amiga_reset(machine)
                window.makeFirstResponder(metalView)
            }
        }
    }

    func sidebarDidRequestInsertFloppy(unit: Int, for profile: AmigaProfileConfig) {
        let panel = NSOpenPanel()
        panel.directoryURL = URL(fileURLWithPath: DiskLibrary.rootPath)
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        if panel.runModal() == .OK, let url = panel.url {
            mountFloppy(path: url.path, unit: unit, in: profile, resetMachine: false)
        }
    }

    func sidebarDidRequestCreateBlankFloppy(unit: Int, for profile: AmigaProfileConfig) {
        createBlankFloppy(unit: unit, in: profile)
    }

    func sidebarDidEjectFloppy(unit: Int, for profile: AmigaProfileConfig) {
        if let idx = profile.floppies.firstIndex(where: { $0.unit == unit }) {
            profile.floppies[idx].diskPath = nil
            leftVC.reloadUI()
            saveConfig()
        }
        if profile.model == activeModel {
            amiga_eject_adf_unit(machine, Int32(unit))
        }
    }

    func sidebarDidToggleFloppyWriteProtect(unit: Int, for profile: AmigaProfileConfig) {
        if let idx = profile.floppies.firstIndex(where: { $0.unit == unit }) {
            profile.floppies[idx].readOnly.toggle()
            let ro = profile.floppies[idx].readOnly
            leftVC.reloadUI()
            saveConfig()
            if profile.model == activeModel {
                amiga_set_floppy_write_protect(machine, Int32(unit), ro ? 1 : 0)
            }
        }
    }

    func sidebarDidAddFloppy(for profile: AmigaProfileConfig) {
        if let unit = profile.addFloppyDrive() {
            leftVC.reloadUI()
            saveConfig()
            if profile.model == activeModel {
                amiga_set_floppy_enabled(machine, Int32(unit), 1)
                rightVC.updateAvailableUnits(profile.floppies.map { $0.unit })
            }
        }
    }

    func sidebarDidRemoveFloppy(unit: Int, for profile: AmigaProfileConfig) {
        profile.removeFloppyDrive(unit: unit)
        leftVC.reloadUI()
        saveConfig()
        if profile.model == activeModel {
            amiga_eject_adf_unit(machine, Int32(unit))
            amiga_set_floppy_enabled(machine, Int32(unit), 0)
            rightVC.updateAvailableUnits(profile.floppies.map { $0.unit })
        }
    }

    func sidebarDidRequestAddHardDrive(for profile: AmigaProfileConfig) {
        let panel = NSOpenPanel()
        panel.directoryURL = AmigaPaths.preferredDirectory(subpath: "Hard Drives")
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        if panel.runModal() == .OK, let url = panel.url {
            profile.addHardDrive(path: url.path)
            leftVC.reloadUI()
            saveConfig()
        }
    }

    func sidebarDidRequestCreateBlankHardDrive(for profile: AmigaProfileConfig) {
        let alert = NSAlert()
        alert.messageText = L10n.t("Utwórz pusty dysk twardy (.hdf)", "Create Blank Hard Drive (.hdf)")
        alert.informativeText = L10n.t("Wybierz rozmiar nowego obrazu dysku:", "Choose the size of the new disk image:")
        alert.addButton(withTitle: L10n.t("Dalej...", "Next..."))
        alert.addButton(withTitle: L10n.t("Anuluj", "Cancel"))

        let sizePopup = NSPopUpButton(frame: NSRect(x: 0, y: 0, width: 180, height: 26), pullsDown: false)
        for mb in [20, 40, 80, 128] {
            sizePopup.addItem(withTitle: "\(mb) MB")
            sizePopup.lastItem?.tag = mb
        }
        alert.accessoryView = sizePopup

        guard alert.runModal() == .alertFirstButtonReturn else { return }
        let sizeMB = UInt32(max(20, sizePopup.selectedTag()))

        let panel = NSSavePanel()
        panel.directoryURL = AmigaPaths.preferredDirectory(subpath: "Hard Drives")
        panel.nameFieldStringValue = "AmigaHD_\(sizeMB)MB.hdf"
        panel.canCreateDirectories = true
        if panel.runModal() == .OK, let url = panel.url {
            let volName = url.deletingPathExtension().lastPathComponent
            if amiga_create_blank_hdf_path(url.path, sizeMB, volName) != 0 {
                profile.addHardDrive(path: url.path)
                leftVC.reloadUI()
                saveConfig()
            }
        }
    }

    func sidebarDidRemoveHardDrive(unit: Int, for profile: AmigaProfileConfig) {
        profile.removeHardDrive(unit: unit)
        leftVC.reloadUI()
        saveConfig()
    }

    func sidebarDidChangeFastRam(mb: Int, for profile: AmigaProfileConfig) {
        profile.fastRamMB = mb
        saveConfig()
        if profile.model == activeModel {
            applyProfileToMachine(profile)
            updateWindowTitle()
            window.makeFirstResponder(metalView)
        }
    }

    func sidebarDidRequestApplyAndReset(profile: AmigaProfileConfig) {
        activeModel = profile.model
        saveConfig()
        applyProfileToMachine(profile)
        updateWindowTitle()
        window.makeFirstResponder(metalView)
    }

    // MARK: - RightDiskBrowserDelegate

    func diskBrowserDidSelectDisk(_ path: String, unit: Int, resetMachine: Bool) {
        guard let profile = profiles[activeModel] else { return }
        mountFloppy(path: path, unit: unit, in: profile, resetMachine: resetMachine)
    }

    func diskBrowserDidRequestBlankDisk(unit: Int) {
        guard let profile = profiles[activeModel] else { return }
        createBlankFloppy(unit: unit, in: profile)
    }

    private func createBlankFloppy(unit: Int, in profile: AmigaProfileConfig) {
        let panel = NSSavePanel()
        panel.directoryURL = URL(fileURLWithPath: DiskLibrary.rootPath)
        panel.nameFieldStringValue = "NowyDysk.adf"
        panel.canCreateDirectories = true
        if panel.runModal() == .OK, let url = panel.url {
            let volName = url.deletingPathExtension().lastPathComponent
            if amiga_create_blank_adf_path(url.path, volName) != 0 {
                rightVC.reloadLibrary()
                mountFloppy(path: url.path, unit: unit, in: profile, resetMachine: false)
            }
        }
    }

    private func mountFloppy(path: String, unit: Int, in profile: AmigaProfileConfig, resetMachine: Bool) {
        if let idx = profile.floppies.firstIndex(where: { $0.unit == unit }) {
            profile.floppies[idx].diskPath = path
            leftVC.reloadUI()
            saveConfig()
        }
        if profile.model == activeModel {
            amiga_set_floppy_enabled(machine, Int32(unit), 1)
            _ = amiga_insert_adf_unit(machine, Int32(unit), path)
            if resetMachine {
                amiga_clear_hdfs(machine)
        for hd in profile.hardDrives {
            _ = amiga_add_hdf_path(machine, hd.imagePath)
        }
        amiga_reset(machine)
            }
            window.makeFirstResponder(metalView)
        }
    }
}

let app = NSApplication.shared
app.setActivationPolicy(.regular)
let delegate = AppDelegate()
app.delegate = delegate
app.run()
