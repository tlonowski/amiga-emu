import AppKit
import AmigaCore

private final class AmigaSlotLedView: NSView {
    private let coreLayer = CAGradientLayer()
    private let splitLine = CALayer()
    private let ledWidth: CGFloat
    private let ledHeight: CGFloat

    init(width: CGFloat = 18, height: CGFloat = 4.5) {
        self.ledWidth = width
        self.ledHeight = height
        super.init(frame: NSRect(x: 0, y: 0, width: width, height: height))
        wantsLayer = true
        layer?.masksToBounds = false
        layer?.cornerRadius = 0.8
        layer?.borderWidth = 0.75
        layer?.borderColor = NSColor(white: 0.04, alpha: 0.95).cgColor

        coreLayer.frame = NSRect(x: 0.75, y: 0.75, width: width - 1.5, height: height - 1.5)
        coreLayer.cornerRadius = 0.4
        coreLayer.startPoint = CGPoint(x: 0.0, y: 0.5)
        coreLayer.endPoint = CGPoint(x: 1.0, y: 0.5)
        coreLayer.locations = [0.0, 0.25, 0.5, 0.75, 1.0]
        layer?.addSublayer(coreLayer)

        splitLine.frame = NSRect(x: (width - 0.8) * 0.5, y: 0.75, width: 0.8, height: height - 1.5)
        splitLine.backgroundColor = NSColor(white: 0.0, alpha: 0.28).cgColor
        layer?.addSublayer(splitLine)
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override var intrinsicContentSize: NSSize {
        return NSSize(width: ledWidth, height: ledHeight)
    }

    func setAppearance(edgeColor: NSColor, coreColor: NSColor, glowColor: NSColor, offColor: NSColor, isOn: Bool, glowStrength: Float = 1.0) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        if isOn {
            layer?.backgroundColor = edgeColor.cgColor
            coreLayer.colors = [
                edgeColor.cgColor,
                coreColor.cgColor,
                edgeColor.cgColor,
                coreColor.cgColor,
                edgeColor.cgColor
            ]
            splitLine.backgroundColor = NSColor(white: 0.0, alpha: 0.22).cgColor
            layer?.shadowColor = glowColor.cgColor
            layer?.shadowOffset = .zero
            layer?.shadowRadius = 6.0
            layer?.shadowOpacity = glowStrength
        } else {
            layer?.backgroundColor = offColor.cgColor
            let dim = offColor.blended(withFraction: 0.25, of: .black) ?? offColor
            coreLayer.colors = [dim.cgColor, offColor.cgColor, dim.cgColor, offColor.cgColor, dim.cgColor]
            splitLine.backgroundColor = NSColor(white: 0.0, alpha: 0.38).cgColor
            layer?.shadowColor = nil
            layer?.shadowRadius = 0
            layer?.shadowOpacity = 0
        }
        CATransaction.commit()
    }
}

private final class AmigaLedBarView: NSView {
    private let pwrDot = AmigaSlotLedView(width: 24, height: 6.5)
    private let dfDot = AmigaSlotLedView(width: 24, height: 6.5)
    private let hdDot = AmigaSlotLedView(width: 24, height: 6.5)
    private var lastState: UInt32 = 0xFFFF_FFFF

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        wantsLayer = true
        layer?.backgroundColor = NSColor(white: 0.12, alpha: 1.0).cgColor
        layer?.cornerRadius = 5
        layer?.borderWidth = 1
        layer?.borderColor = NSColor(white: 0.23, alpha: 1.0).cgColor

        let stack = NSStackView()
        stack.orientation = .horizontal
        stack.spacing = 8
        stack.alignment = .centerY
        stack.translatesAutoresizingMaskIntoConstraints = false
        addSubview(stack)

        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 10),
            stack.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -10),
            stack.topAnchor.constraint(equalTo: topAnchor, constant: 3),
            stack.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -3)
        ])

        pwrDot.translatesAutoresizingMaskIntoConstraints = false
        dfDot.translatesAutoresizingMaskIntoConstraints = false
        hdDot.translatesAutoresizingMaskIntoConstraints = false
        pwrDot.toolTip = "POWER"
        dfDot.toolTip = "F. DISK"
        hdDot.toolTip = "H. DISK"

        stack.addArrangedSubview(pwrDot)
        stack.addArrangedSubview(dfDot)
        stack.addArrangedSubview(hdDot)

        update(state: 0x01)
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override var intrinsicContentSize: NSSize {
        return NSSize(width: 108, height: 20)
    }

    func update(state: UInt32) {
        guard state != lastState else { return }
        lastState = state

        let pwrOn = (state & 0x01) != 0
        let filterBright = (state & 0x08) != 0
        let dfOn = (state & 0x02) != 0
        let hdOn = (state & 0x04) != 0

        pwrDot.setAppearance(
            edgeColor: filterBright
                ? NSColor(red: 0.08, green: 0.92, blue: 0.06, alpha: 1.0)
                : NSColor(red: 0.06, green: 0.52, blue: 0.08, alpha: 1.0),
            coreColor: filterBright
                ? NSColor(red: 0.45, green: 1.0, blue: 0.25, alpha: 1.0)
                : NSColor(red: 0.14, green: 0.72, blue: 0.16, alpha: 1.0),
            glowColor: NSColor(red: 0.15, green: 1.0, blue: 0.15, alpha: 1.0),
            offColor: NSColor(red: 0.10, green: 0.18, blue: 0.10, alpha: 1.0),
            isOn: pwrOn,
            glowStrength: filterBright ? 1.0 : 0.45
        )

        dfDot.setAppearance(
            edgeColor: NSColor(red: 1.0, green: 0.74, blue: 0.0, alpha: 1.0),
            coreColor: NSColor(red: 1.0, green: 0.96, blue: 0.25, alpha: 1.0),
            glowColor: NSColor(red: 1.0, green: 0.82, blue: 0.05, alpha: 1.0),
            offColor: NSColor(red: 0.20, green: 0.17, blue: 0.07, alpha: 1.0),
            isOn: dfOn
        )

        hdDot.setAppearance(
            edgeColor: NSColor(red: 1.0, green: 0.20, blue: 0.10, alpha: 1.0),
            coreColor: NSColor(red: 1.0, green: 0.56, blue: 0.42, alpha: 1.0),
            glowColor: NSColor(red: 1.0, green: 0.22, blue: 0.12, alpha: 1.0),
            offColor: NSColor(red: 0.22, green: 0.10, blue: 0.09, alpha: 1.0),
            isOn: hdOn
        )
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
    private var floppySoundEnabled = (UserDefaults.standard.object(forKey: "floppySoundEnabled") as? Bool) ?? true
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
    private let printerSpooler = AmigaPrinterSpooler()

    private var profiles: [AmigaModelType: AmigaProfileConfig] = [
        .a500: AmigaProfileConfig(model: .a500),
        .a600: AmigaProfileConfig(model: .a600),
        .a1200: AmigaProfileConfig(model: .a1200),
        .a4000: AmigaProfileConfig(model: .a4000)
    ]
    private var activeModel: AmigaModelType = .a600

    private let tbLeftSidebar = NSToolbarItem.Identifier("toggleLeftSidebar")
    private let tbLeds = NSToolbarItem.Identifier("amigaLeds")
    private let tbRightSidebar = NSToolbarItem.Identifier("toggleRightSidebar")
    private let titleBarLabel = NSTextField(labelWithString: "")

    func applicationDidFinishLaunching(_ notification: Notification) {
        AmigaPaths.ensureDirectoryStructure()
        NSApp.applicationIconImage = AmigaAppIcon.makeIcon()
        let loaded = ConfigStore.load()
        activeModel = loaded.activeModel
        profiles = loaded.profiles

        machine = amiga_create()
        amiga_set_floppy_sound_enabled(machine, floppySoundEnabled ? 1 : 0)
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
        metalView.onControllerStatusChanged = { [weak self] count, name in
            self?.connectedControllerName = (count > 0) ? (name ?? "USB/BT") : nil
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
        window.titleVisibility = .hidden
        window.toolbar = toolbar
        window.toolbarStyle = .unified

        updateWindowTitle()
        if let visibleFrame = (window.screen ?? NSScreen.main)?.visibleFrame {
            window.setFrame(visibleFrame, display: true)
        } else {
            window.center()
        }
        window.makeFirstResponder(metalView)
        window.makeKeyAndOrderFront(nil)

        audioOutput = AmigaAudioOutput(machine: machine)

        setupMenu()
        ledTimer = Timer.scheduledTimer(withTimeInterval: 1.0 / 30.0, repeats: true) { [weak self] _ in
            guard let self = self, let m = self.machine else { return }
            let ledState = amiga_get_led_state(m)
            self.ledBarView.update(state: ledState)
            self.leftVC.updateDriveActivity(state: ledState)
            var ioBuf = [UInt8](repeating: 0, count: 4096)
            while true {
                let n = amiga_read_parallel_output(m, &ioBuf, ioBuf.count)
                if n == 0 { break }
                self.printerSpooler.appendParallelBytes(Array(ioBuf.prefix(n)))
            }
            while true {
                let n = amiga_read_serial_output(m, &ioBuf, ioBuf.count)
                if n == 0 { break }
                self.printerSpooler.appendSerialBytes(Array(ioBuf.prefix(n)))
            }
            self.printerSpooler.pollAutoPrint(in: self.window)
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
        amiga_set_a500_chip_ram_512k(machine, profile.a500Chip512KB ? 1 : 0)
        amiga_set_a600_chip_ram_2mb(machine, profile.a600Chip2MB ? 1 : 0)
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
            .sidebarTrackingSeparator,
            tbLeftSidebar,
            .flexibleSpace,
            tbLeds,
            tbRightSidebar
        ]
    }

    func toolbarAllowedItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        return toolbarDefaultItemIdentifiers(toolbar)
    }

    func toolbar(_ toolbar: NSToolbar, itemForItemIdentifier itemIdentifier: NSToolbarItem.Identifier, willBeInsertedIntoToolbar flag: Bool) -> NSToolbarItem? {
        if itemIdentifier == .sidebarTrackingSeparator {
            return NSTrackingSeparatorToolbarItem(identifier: .sidebarTrackingSeparator, splitView: splitVC.splitView, dividerIndex: 0)
        }
        let item = NSToolbarItem(itemIdentifier: itemIdentifier)
        item.target = self
        switch itemIdentifier {
        case tbLeftSidebar:
            let btn = NSButton(
                image: NSImage(systemSymbolName: "sidebar.left", accessibilityDescription: L10n.t("Maszyny", "Machines")) ?? NSImage(),
                target: self,
                action: #selector(toggleLeftSidebar)
            )
            btn.bezelStyle = .texturedRounded
            btn.isBordered = false
            btn.imagePosition = .imageOnly
            btn.toolTip = L10n.t("Pokaż/ukryj panel maszyn Amigi (Cmd+1)", "Show/hide Amiga machines panel (Cmd+1)")
            btn.translatesAutoresizingMaskIntoConstraints = false
            btn.widthAnchor.constraint(equalToConstant: 28).isActive = true
            btn.heightAnchor.constraint(equalToConstant: 24).isActive = true

            titleBarLabel.font = NSFont.systemFont(ofSize: 13, weight: .semibold)
            titleBarLabel.textColor = MonokaiTheme.text
            titleBarLabel.lineBreakMode = .byTruncatingTail
            titleBarLabel.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)

            let stack = NSStackView(views: [btn, titleBarLabel])
            stack.orientation = .horizontal
            stack.alignment = .centerY
            stack.spacing = 6
            item.label = L10n.t("Maszyny", "Machines")
            item.view = stack
        case tbLeds:
            item.label = L10n.t("Diody Amigi", "Amiga LEDs")
            item.toolTip = L10n.t("PWR (zasilanie) • DF (stacja dyskietek + ścieżka) • HD (dysk twardy)", "PWR (power) • DF (floppy + track) • HD (hard drive)")
            item.view = ledBarView
        case tbRightSidebar:
            item.label = L10n.t("Obrazy dysków", "Disk Images")
            item.toolTip = L10n.t("Pokaż/ukryj panel obrazów dysków (Cmd+2)", "Show/hide disk images panel (Cmd+2)")
            item.image = NSImage(systemSymbolName: "sidebar.right", accessibilityDescription: item.label)
            item.action = #selector(toggleRightSidebar)
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
        let floppySoundItem = appMenu.addItem(withTitle: L10n.t("Dźwięk stacji dyskietek", "Floppy Drive Sound"), action: #selector(toggleFloppySound(_:)), keyEquivalent: "")
        floppySoundItem.state = floppySoundEnabled ? .on : .off
        appMenu.addItem(NSMenuItem.separator())
        appMenu.addItem(withTitle: L10n.t("Drukuj z Amigi (PRT: / PAR: / SER:)...", "Print from Amiga (PRT: / PAR: / SER:)..."), action: #selector(printAmigaOutput), keyEquivalent: "p")
        let autoPrintItem = appMenu.addItem(withTitle: L10n.t("Automatyczny wydruk z Amigi na macOS", "Auto-Print from Amiga to macOS"), action: #selector(toggleAutoPrint(_:)), keyEquivalent: "")
        autoPrintItem.state = printerSpooler.autoPrintEnabled ? .on : .off
        appMenu.addItem(withTitle: L10n.t("Wyczyść bufor drukarki Amigi", "Clear Amiga Printer Buffer"), action: #selector(clearAmigaPrinterBuffer), keyEquivalent: "")
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

    private var connectedControllerName: String?

    private func updateWindowTitle() {
        var tags: [String] = []
        if metalView.joystickMode { tags.append("[JOY]") }
        if let pad = connectedControllerName { tags.append("[PAD: \(pad)]") }
        if metalView.mouseGrabbed { tags.append("[MOUSE - Cmd+M release]") }
        if metalView.warpMode { tags.append("[WARP 12x]") }
        let base = "\(activeModel.rawValue) — Native macOS Emulator"
        let fullTitle = tags.isEmpty ? base : (base + " " + tags.joined(separator: " "))
        window.title = fullTitle
        titleBarLabel.stringValue = fullTitle
        leftVC?.setToolStates(warp: metalView.warpMode, joystick: metalView.joystickMode)
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
            window.titleVisibility = .hidden
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

    @objc private func toggleFloppySound(_ sender: NSMenuItem) {
        floppySoundEnabled.toggle()
        UserDefaults.standard.set(floppySoundEnabled, forKey: "floppySoundEnabled")
        sender.state = floppySoundEnabled ? .on : .off
        if let machine = machine {
            amiga_set_floppy_sound_enabled(machine, floppySoundEnabled ? 1 : 0)
        }
    }

    @objc private func printAmigaOutput() {
        printerSpooler.printCurrentJob(in: window)
        window.makeFirstResponder(metalView)
    }

    @objc private func toggleAutoPrint(_ sender: NSMenuItem) {
        printerSpooler.autoPrintEnabled.toggle()
        sender.state = printerSpooler.autoPrintEnabled ? .on : .off
    }

    @objc private func clearAmigaPrinterBuffer() {
        printerSpooler.clearBuffers()
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

    func sidebarDidRequestTool(_ tool: SidebarTool) {
        switch tool {
        case .reset: resetMachine()
        case .warp: toggleWarpToolbar()
        case .joystick: toggleJoystickToolbar()
        case .print: printAmigaOutput()
        case .fullScreen: toggleFullScreen()
        }
    }

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

    func sidebarDidChangeA500ChipRam(use512KB: Bool, for profile: AmigaProfileConfig) {
        profile.a500Chip512KB = use512KB
        leftVC.reloadUI()
        saveConfig()
        if profile.model == activeModel {
            applyProfileToMachine(profile)
            updateWindowTitle()
            window.makeFirstResponder(metalView)
        }
    }

    func sidebarDidChangeA600ChipRam(use2MB: Bool, for profile: AmigaProfileConfig) {
        profile.a600Chip2MB = use2MB
        leftVC.reloadUI()
        saveConfig()
        if profile.model == activeModel {
            applyProfileToMachine(profile)
            updateWindowTitle()
            window.makeFirstResponder(metalView)
        }
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

    func diskBrowserDidSelectDiskSet(_ paths: [String], resetMachine: Bool) {
        guard let profile = profiles[activeModel], !paths.isEmpty else { return }
        let count = min(4, paths.count)
        for u in 0..<count {
            if !profile.floppies.contains(where: { $0.unit == u }) {
                profile.floppies.append(FloppyDriveSlot(unit: u, diskPath: nil))
            }
        }
        if count < 4 {
            for u in count..<4 {
                amiga_eject_adf_unit(machine, Int32(u))
                amiga_set_floppy_enabled(machine, Int32(u), 0)
            }
            profile.floppies.removeAll { $0.unit >= count }
        }
        profile.floppies.sort { $0.unit < $1.unit }
        for u in 0..<count {
            if let idx = profile.floppies.firstIndex(where: { $0.unit == u }) {
                profile.floppies[idx].diskPath = paths[u]
            }
            amiga_set_floppy_enabled(machine, Int32(u), 1)
            _ = amiga_insert_adf_unit(machine, Int32(u), paths[u])
        }
        leftVC.reloadUI()
        rightVC.updateAvailableUnits(profile.floppies.map { $0.unit })
        saveConfig()
        if resetMachine {
            amiga_clear_hdfs(machine)
            for hd in profile.hardDrives {
                _ = amiga_add_hdf_path(machine, hd.imagePath)
            }
            amiga_reset(machine)
        }
        window.makeFirstResponder(metalView)
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
