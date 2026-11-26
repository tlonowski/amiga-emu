import AppKit

enum SidebarTool: Int {
    case reset, freeze, warp, joystick, print, fullScreen
}

protocol LeftSidebarDelegate: AnyObject {
    func sidebarDidRequestTool(_ tool: SidebarTool)
    func sidebarDidChangeSlomo(enabled: Bool, factor: Double)
    func sidebarDidRequestActionReplayMonitor()
    func sidebarDidSelectModel(_ model: AmigaModelType)
    func sidebarDidRequestRomPick(for profile: AmigaProfileConfig)
    func sidebarDidRequestInsertFloppy(unit: Int, for profile: AmigaProfileConfig)
    func sidebarDidRequestCreateBlankFloppy(unit: Int, for profile: AmigaProfileConfig)
    func sidebarDidEjectFloppy(unit: Int, for profile: AmigaProfileConfig)
    func sidebarDidToggleFloppyWriteProtect(unit: Int, for profile: AmigaProfileConfig)
    func sidebarDidAddFloppy(for profile: AmigaProfileConfig)
    func sidebarDidRemoveFloppy(unit: Int, for profile: AmigaProfileConfig)
    func sidebarDidRequestAddHardDrive(for profile: AmigaProfileConfig)
    func sidebarDidRequestCreateBlankHardDrive(for profile: AmigaProfileConfig)
    func sidebarDidRemoveHardDrive(unit: Int, for profile: AmigaProfileConfig)
    func sidebarDidChangeA500ChipRam(use512KB: Bool, for profile: AmigaProfileConfig)
    func sidebarDidChangeA600ChipRam(use2MB: Bool, for profile: AmigaProfileConfig)
    func sidebarDidChangeFastRam(mb: Int, for profile: AmigaProfileConfig)
    func sidebarDidRequestApplyAndReset(profile: AmigaProfileConfig)
}

/// Custom Action Replay Mk III "SLOMO" rotary potentiometer knob + metallic toggle switch + red ACTIVE LED + red FREEZE button.
final class ActionReplaySlomoView: NSView {
    var onChanged: ((Bool, Double) -> Void)?
    var onFreezeMonitorRequested: (() -> Void)?

    private final class RotaryKnobControl: NSControl {
        /// Normalized knob value 0.0 ("-" minimum slow) ... 1.0 ("+" maximum slow)
        var doubleValueNormalized: Double = 0.5 {
            didSet {
                doubleValueNormalized = min(max(doubleValueNormalized, 0.0), 1.0)
                needsDisplay = true
            }
        }
        private var dragStartY: CGFloat = 0
        private var dragStartVal: Double = 0.5

        override var intrinsicContentSize: NSSize { NSSize(width: 24, height: 24) }

        override func mouseDown(with event: NSEvent) {
            dragStartY = event.locationInWindow.y
            dragStartVal = doubleValueNormalized
            let pt = convert(event.locationInWindow, from: nil)
            let cx = bounds.midX
            let cy = bounds.midY
            let dx = Double(pt.x - cx)
            let dy = Double(pt.y - cy)
            if hypot(dx, dy) > 4.0 {
                // Map angle around center to 0..1 (from +135 deg top-left "-" to -135 deg bottom/right "+")
                let deg = atan2(dx, dy) * 180.0 / .pi // 0 at top, -135 at bottom-left, +135 at bottom-right
                let clamped = min(max(deg, -135.0), 135.0)
                doubleValueNormalized = (clamped + 135.0) / 270.0
                sendAction(action, to: target)
            }
        }

        override func mouseDragged(with event: NSEvent) {
            let dy = Double(event.locationInWindow.y - dragStartY)
            doubleValueNormalized = dragStartVal + dy / 70.0
            sendAction(action, to: target)
        }

        override func scrollWheel(with event: NSEvent) {
            let delta = Double(event.scrollingDeltaY) * (event.hasPreciseScrollingDeltas ? 0.004 : 0.04)
            doubleValueNormalized += delta
            sendAction(action, to: target)
        }

        override func draw(_ dirtyRect: NSRect) {
            guard let ctx = NSGraphicsContext.current?.cgContext else { return }
            let cx = bounds.midX
            let cy = bounds.midY
            let r: CGFloat = 8.5

            // Tick marks around knob
            ctx.saveGState()
            for i in 0...6 {
                let frac = Double(i) / 6.0
                let angle = (-135.0 + frac * 270.0) * .pi / 180.0
                let sinA = CGFloat(sin(angle))
                let cosA = CGFloat(cos(angle))
                let r1 = r + 1.8
                let r2 = r + 3.4
                ctx.move(to: CGPoint(x: cx + sinA * r1, y: cy + cosA * r1))
                ctx.addLine(to: CGPoint(x: cx + sinA * r2, y: cy + cosA * r2))
            }
            ctx.setStrokeColor(MonokaiTheme.muted.withAlphaComponent(0.75).cgColor)
            ctx.setLineWidth(1.1)
            ctx.strokePath()
            ctx.restoreGState()

            // Outer knurled black body
            let bodyRect = NSRect(x: cx - r, y: cy - r, width: r * 2, height: r * 2)
            ctx.saveGState()
            ctx.setShadow(offset: CGSize(width: 0, height: -1), blur: 2.5, color: NSColor.black.withAlphaComponent(0.7).cgColor)
            ctx.setFillColor(NSColor(calibratedWhite: 0.14, alpha: 1.0).cgColor)
            ctx.fillEllipse(in: bodyRect)
            ctx.restoreGState()

            ctx.setStrokeColor(NSColor(calibratedWhite: 0.35, alpha: 1.0).cgColor)
            ctx.setLineWidth(1.2)
            ctx.strokeEllipse(in: bodyRect.insetBy(dx: 0.6, dy: 0.6))

            // Inner cap
            let capRect = bodyRect.insetBy(dx: 2.4, dy: 2.4)
            ctx.setFillColor(NSColor(calibratedWhite: 0.20, alpha: 1.0).cgColor)
            ctx.fillEllipse(in: capRect)

            // White pointer line
            let angle = (-135.0 + doubleValueNormalized * 270.0) * .pi / 180.0
            let sinA = CGFloat(sin(angle))
            let cosA = CGFloat(cos(angle))
            ctx.setStrokeColor(MonokaiTheme.orange.cgColor)
            ctx.setLineWidth(1.8)
            ctx.setLineCap(.round)
            ctx.move(to: CGPoint(x: cx + sinA * 1.5, y: cy + cosA * 1.5))
            ctx.addLine(to: CGPoint(x: cx + sinA * (r - 1.8), y: cy + cosA * (r - 1.8)))
            ctx.strokePath()
        }
    }

    private final class ToggleSwitchControl: NSControl {
        var isOn: Bool = false {
            didSet { needsDisplay = true }
        }

        override var intrinsicContentSize: NSSize { NSSize(width: 28, height: 18) }

        override func mouseDown(with event: NSEvent) {
            isOn.toggle()
            sendAction(action, to: target)
        }

        override func draw(_ dirtyRect: NSRect) {
            guard let ctx = NSGraphicsContext.current?.cgContext else { return }
            let cx = bounds.midX
            let cy = bounds.midY

            // Metallic hex bushing / washer
            let washerRect = NSRect(x: cx - 7, y: cy - 7, width: 14, height: 14)
            ctx.setFillColor(NSColor(calibratedWhite: 0.48, alpha: 1.0).cgColor)
            ctx.fillEllipse(in: washerRect)
            ctx.setStrokeColor(NSColor(calibratedWhite: 0.72, alpha: 1.0).cgColor)
            ctx.setLineWidth(1.1)
            ctx.strokeEllipse(in: washerRect.insetBy(dx: 0.5, dy: 0.5))

            // Inner dark hole
            let holeRect = NSRect(x: cx - 3.5, y: cy - 3.5, width: 7, height: 7)
            ctx.setFillColor(NSColor(calibratedWhite: 0.12, alpha: 1.0).cgColor)
            ctx.fillEllipse(in: holeRect)

            // Chrome toggle bat (bat points right/up when ON, left/down when OFF)
            let tipX: CGFloat = isOn ? (cx + 8.0) : (cx - 8.0)
            let tipY: CGFloat = isOn ? (cy + 2.5) : (cy - 1.5)
            ctx.saveGState()
            ctx.setShadow(offset: CGSize(width: 0, height: -1), blur: 2.0, color: NSColor.black.withAlphaComponent(0.75).cgColor)
            ctx.setStrokeColor(isOn ? NSColor(calibratedWhite: 0.92, alpha: 1.0).cgColor : NSColor(calibratedWhite: 0.68, alpha: 1.0).cgColor)
            ctx.setLineWidth(3.8)
            ctx.setLineCap(.round)
            ctx.move(to: CGPoint(x: cx, y: cy))
            ctx.addLine(to: CGPoint(x: tipX, y: tipY))
            ctx.strokePath()

            // Highlight on bat tip
            ctx.setFillColor(NSColor.white.cgColor)
            ctx.fillEllipse(in: NSRect(x: tipX - 1.6, y: tipY - 1.6, width: 3.2, height: 3.2))
            ctx.restoreGState()
        }
    }

    private let knob = RotaryKnobControl()
    private let toggleSwitch = ToggleSwitchControl()
    private let ledView = NSView()
    private let titleLabel = NSTextField(labelWithString: "SLOMO")
    private let onLabel = NSTextField(labelWithString: "ON")
    private let activeLabel = NSTextField(labelWithString: "ACTIVE")
    private let pctLabel = NSTextField(labelWithString: "50%")
    private let monitorBtn = NSButton()

    var isSlomoOn: Bool {
        get { toggleSwitch.isOn }
        set {
            toggleSwitch.isOn = newValue
            updateVisuals()
        }
    }

    /// Slowdown speed multiplier in 0.10 (10% speed at max "+") ... 0.85 (85% speed at min "-")
    var speedMultiplier: Double {
        let v = knob.doubleValueNormalized
        return max(0.08, 0.85 - v * 0.75)
    }

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setup()
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    private func setup() {
        wantsLayer = true
        layer?.backgroundColor = NSColor.white.withAlphaComponent(0.04).cgColor
        layer?.borderColor = MonokaiTheme.border.cgColor
        layer?.borderWidth = 1.0
        layer?.cornerRadius = 6

        let minusLbl = NSTextField(labelWithString: "−")
        minusLbl.font = .systemFont(ofSize: 10, weight: .bold)
        minusLbl.textColor = MonokaiTheme.muted
        minusLbl.translatesAutoresizingMaskIntoConstraints = false

        let plusLbl = NSTextField(labelWithString: "+")
        plusLbl.font = .systemFont(ofSize: 10, weight: .bold)
        plusLbl.textColor = MonokaiTheme.muted
        plusLbl.translatesAutoresizingMaskIntoConstraints = false

        knob.target = self
        knob.action = #selector(knobChanged)
        knob.toolTip = L10n.t("Pokrętło Action Replay SLOMO: regulacja spowolnienia (przeciągnij lub kręć kółkiem myszy)", "Action Replay SLOMO knob: adjust slowdown level (drag or scroll mouse wheel)")
        knob.translatesAutoresizingMaskIntoConstraints = false

        toggleSwitch.target = self
        toggleSwitch.action = #selector(switchToggled)
        toggleSwitch.toolTip = L10n.t("Przełącznik Action Replay SLOMO ON/OFF (Ctrl+G)", "Action Replay SLOMO ON/OFF toggle switch (Ctrl+G)")
        toggleSwitch.translatesAutoresizingMaskIntoConstraints = false

        onLabel.font = .systemFont(ofSize: 9, weight: .bold)
        onLabel.textColor = MonokaiTheme.muted
        onLabel.translatesAutoresizingMaskIntoConstraints = false

        ledView.wantsLayer = true
        ledView.layer?.cornerRadius = 4.5
        ledView.translatesAutoresizingMaskIntoConstraints = false
        NSLayoutConstraint.activate([
            ledView.widthAnchor.constraint(equalToConstant: 9),
            ledView.heightAnchor.constraint(equalToConstant: 9)
        ])

        activeLabel.font = .systemFont(ofSize: 9, weight: .bold)
        activeLabel.textColor = MonokaiTheme.muted
        activeLabel.translatesAutoresizingMaskIntoConstraints = false

        titleLabel.font = .systemFont(ofSize: 10, weight: .heavy)
        titleLabel.textColor = MonokaiTheme.text
        titleLabel.wantsLayer = true
        titleLabel.layer?.borderColor = MonokaiTheme.border.cgColor
        titleLabel.layer?.borderWidth = 1.0
        titleLabel.layer?.cornerRadius = 3
        titleLabel.alignment = .center
        titleLabel.translatesAutoresizingMaskIntoConstraints = false

        pctLabel.font = .monospacedDigitSystemFont(ofSize: 10, weight: .semibold)
        pctLabel.textColor = MonokaiTheme.muted
        pctLabel.alignment = .right
        pctLabel.translatesAutoresizingMaskIntoConstraints = false

        monitorBtn.title = "MEM"
        monitorBtn.font = .systemFont(ofSize: 9.5, weight: .heavy)
        monitorBtn.bezelStyle = . texturedRounded
        monitorBtn.isBordered = false
        monitorBtn.wantsLayer = true
        monitorBtn.layer?.backgroundColor = NSColor(red: 0.88, green: 0.18, blue: 0.22, alpha: 0.25).cgColor
        monitorBtn.layer?.borderColor = NSColor(red: 1.0, green: 0.24, blue: 0.28, alpha: 0.85).cgColor
        monitorBtn.layer?.borderWidth = 1.0
        monitorBtn.layer?.cornerRadius = 4
        monitorBtn.contentTintColor = NSColor(red: 1.0, green: 0.35, blue: 0.38, alpha: 1.0)
        monitorBtn.toolTip = L10n.t("Action Replay Mk III Freeze Monitor — podgląd, wyszukiwarka (Trainer) i edycja pamięci RAM (Ctrl+E)", "Action Replay Mk III Freeze Monitor — inspect, search (Trainer) & edit RAM (Ctrl+E)")
        monitorBtn.target = self
        monitorBtn.action = #selector(monitorClicked)
        monitorBtn.translatesAutoresizingMaskIntoConstraints = false

        for v in [minusLbl, knob, plusLbl, toggleSwitch, onLabel, ledView, activeLabel, titleLabel, pctLabel, monitorBtn] {
            addSubview(v)
        }

        NSLayoutConstraint.activate([
            heightAnchor.constraint(equalToConstant: 30),

            minusLbl.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 6),
            minusLbl.centerYAnchor.constraint(equalTo: centerYAnchor),

            knob.leadingAnchor.constraint(equalTo: minusLbl.trailingAnchor, constant: 1),
            knob.centerYAnchor.constraint(equalTo: centerYAnchor),
            knob.widthAnchor.constraint(equalToConstant: 22),
            knob.heightAnchor.constraint(equalToConstant: 22),

            plusLbl.leadingAnchor.constraint(equalTo: knob.trailingAnchor, constant: 1),
            plusLbl.centerYAnchor.constraint(equalTo: centerYAnchor),

            toggleSwitch.leadingAnchor.constraint(equalTo: plusLbl.trailingAnchor, constant: 5),
            toggleSwitch.centerYAnchor.constraint(equalTo: centerYAnchor),
            toggleSwitch.widthAnchor.constraint(equalToConstant: 24),
            toggleSwitch.heightAnchor.constraint(equalToConstant: 18),

            onLabel.leadingAnchor.constraint(equalTo: toggleSwitch.trailingAnchor, constant: 2),
            onLabel.centerYAnchor.constraint(equalTo: centerYAnchor),

            ledView.leadingAnchor.constraint(equalTo: onLabel.trailingAnchor, constant: 5),
            ledView.centerYAnchor.constraint(equalTo: centerYAnchor),

            activeLabel.leadingAnchor.constraint(equalTo: ledView.trailingAnchor, constant: 3),
            activeLabel.centerYAnchor.constraint(equalTo: centerYAnchor),

            titleLabel.leadingAnchor.constraint(equalTo: activeLabel.trailingAnchor, constant: 5),
            titleLabel.centerYAnchor.constraint(equalTo: centerYAnchor),
            titleLabel.widthAnchor.constraint(equalToConstant: 42),

            pctLabel.leadingAnchor.constraint(equalTo: titleLabel.trailingAnchor, constant: 4),
            pctLabel.centerYAnchor.constraint(equalTo: centerYAnchor),

            monitorBtn.leadingAnchor.constraint(greaterThanOrEqualTo: pctLabel.trailingAnchor, constant: 4),
            monitorBtn.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -6),
            monitorBtn.centerYAnchor.constraint(equalTo: centerYAnchor),
            monitorBtn.widthAnchor.constraint(equalToConstant: 34),
            monitorBtn.heightAnchor.constraint(equalToConstant: 20)
        ])

        updateVisuals()
    }

    @objc private func knobChanged() {
        updateVisuals()
        onChanged?(toggleSwitch.isOn, speedMultiplier)
    }

    @objc private func switchToggled() {
        updateVisuals()
        onChanged?(toggleSwitch.isOn, speedMultiplier)
    }

    @objc private func monitorClicked() {
        onFreezeMonitorRequested?()
    }

    private func updateVisuals() {
        let on = toggleSwitch.isOn
        let pct = Int((speedMultiplier * 100.0).rounded())
        pctLabel.stringValue = "\(pct)%"

        let redOn = NSColor(red: 1.0, green: 0.18, blue: 0.22, alpha: 1.0)
        let redOff = NSColor(red: 0.24, green: 0.08, blue: 0.10, alpha: 1.0)
        ledView.layer?.backgroundColor = (on ? redOn : redOff).cgColor
        ledView.layer?.shadowColor = on ? redOn.cgColor : nil
        ledView.layer?.shadowOffset = .zero
        ledView.layer?.shadowRadius = on ? 5.0 : 0.0
        ledView.layer?.shadowOpacity = on ? 0.95 : 0.0

        onLabel.textColor = on ? MonokaiTheme.text : MonokaiTheme.muted
        activeLabel.textColor = on ? redOn : MonokaiTheme.muted
        pctLabel.textColor = on ? MonokaiTheme.orange : MonokaiTheme.muted
        titleLabel.textColor = on ? redOn : MonokaiTheme.text
        titleLabel.layer?.borderColor = (on ? redOn.withAlphaComponent(0.8) : MonokaiTheme.border).cgColor
        layer?.borderColor = (on ? redOn.withAlphaComponent(0.75) : MonokaiTheme.border).cgColor
        layer?.backgroundColor = (on ? redOn.withAlphaComponent(0.12) : NSColor.white.withAlphaComponent(0.04)).cgColor
    }
}

final class LeftSidebarViewController: NSViewController, NSTableViewDataSource, NSTableViewDelegate {
    weak var delegate: LeftSidebarDelegate?

    private var profiles: [AmigaModelType: AmigaProfileConfig] = [:]
    private var selectedModel: AmigaModelType = .a600

    private let modelTable = NSTableView()
    private let hardwareStack = NSStackView()
    private let scrollView = NSScrollView()
    private var titleLabel: NSTextField!
    private var hwHeaderLabel: NSTextField!
    private var applyBtn: NSButton!
    private var resetToolBtn: NSButton!
    private var freezeToolBtn: NSButton!
    private var warpToolBtn: NSButton!
    private var joyToolBtn: NSButton!
    private var printToolBtn: NSButton!
    private var fullToolBtn: NSButton!
    private let toolsRow = NSStackView()
    private let slomoRow = ActionReplaySlomoView()
    private var floppyIconViews: [Int: NSImageView] = [:]
    private var floppyLedViews: [Int: NSView] = [:]
    private var hdIconViews: [NSImageView] = []
    private var hdLedViews: [NSView] = []
    private var lastLedState: UInt32 = 0xFFFF_FFFF
    private var currentWarpState = false
    private var currentJoystickState = false
    private var currentFullScreenState = false

    func configure(profiles: [AmigaModelType: AmigaProfileConfig], selected: AmigaModelType) {
        self.profiles = profiles
        self.selectedModel = selected
        reloadUI()
        setToolStates(warp: currentWarpState, joystick: currentJoystickState, fullScreen: currentFullScreenState)
    }

    func updateSelected(_ model: AmigaModelType) {
        self.selectedModel = model
        reloadUI()
        setToolStates(warp: currentWarpState, joystick: currentJoystickState, fullScreen: currentFullScreenState)
    }

    func setToolStates(warp: Bool, joystick: Bool, fullScreen: Bool = false, slomo: Bool? = nil) {
        currentWarpState = warp
        currentJoystickState = joystick
        currentFullScreenState = fullScreen
        if let slomo = slomo {
            slomoRow.isSlomoOn = slomo
        }
        guard isViewLoaded else { return }
        warpToolBtn?.state = warp ? .on : .off
        joyToolBtn?.state = joystick ? .on : .off
        fullToolBtn?.state = fullScreen ? .on : .off
        applyToolButtonStyle(warpToolBtn, active: warp, activeColor: MonokaiTheme.orange)
        applyToolButtonStyle(joyToolBtn, active: joystick, activeColor: MonokaiTheme.green)
        applyToolButtonStyle(fullToolBtn, active: fullScreen, activeColor: MonokaiTheme.cyan)
    }

    var slomoSpeedMultiplier: Double {
        return slomoRow.speedMultiplier
    }

    func flashTool(_ tool: SidebarTool) {
        guard isViewLoaded else { return }
        switch tool {
        case .reset:
            if let b = resetToolBtn { flashMomentaryToolButton(b, color: MonokaiTheme.yellow) }
        case .freeze:
            if let b = freezeToolBtn { flashMomentaryToolButton(b, color: MonokaiTheme.pink) }
        case .print:
            if let b = printToolBtn { flashMomentaryToolButton(b, color: MonokaiTheme.cyan) }
        default:
            break
        }
    }

    private func applyToolButtonStyle(_ button: NSButton?, active: Bool, activeColor: NSColor) {
        guard let b = button else { return }
        b.wantsLayer = true
        if active {
            b.contentTintColor = activeColor
            b.alphaValue = 1.0
            b.layer?.backgroundColor = activeColor.withAlphaComponent(0.22).cgColor
            b.layer?.borderColor = activeColor.withAlphaComponent(0.85).cgColor
            b.layer?.borderWidth = 1.2
            b.layer?.cornerRadius = 6
            b.layer?.shadowColor = activeColor.cgColor
            b.layer?.shadowOffset = .zero
            b.layer?.shadowRadius = 5.0
            b.layer?.shadowOpacity = 0.85
        } else {
            b.contentTintColor = MonokaiTheme.muted
            b.alphaValue = 0.72
            b.layer?.backgroundColor = NSColor.white.withAlphaComponent(0.04).cgColor
            b.layer?.borderColor = MonokaiTheme.border.cgColor
            b.layer?.borderWidth = 1.0
            b.layer?.cornerRadius = 6
            b.layer?.shadowOpacity = 0.0
        }
    }

    private func flashMomentaryToolButton(_ button: NSButton, color: NSColor) {
        applyToolButtonStyle(button, active: true, activeColor: color)
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.35) { [weak self, weak button] in
            self?.applyToolButtonStyle(button, active: false, activeColor: color)
        }
    }

    @objc private func onToolClicked(_ sender: NSButton) {
        guard let tool = SidebarTool(rawValue: sender.tag) else { return }
        delegate?.sidebarDidRequestTool(tool)
    }

    func updateDriveActivity(state: UInt32) {
        guard state != lastLedState else { return }
        lastLedState = state
        applyDriveActivityStyles(state: state)
    }

    private func applyDriveActivityStyles(state: UInt32) {
        let dfOnColor = NSColor(red: 1.0, green: 0.68, blue: 0.14, alpha: 1.0)
        let dfGlowColor = NSColor(red: 1.0, green: 0.58, blue: 0.08, alpha: 1.0)
        let dfOffLedColor = NSColor(red: 0.20, green: 0.15, blue: 0.08, alpha: 1.0)

        for (unit, img) in floppyIconViews {
            let active = (state & UInt32(0x10 << unit)) != 0
            img.contentTintColor = active ? dfOnColor : MonokaiTheme.muted
            img.layer?.shadowColor = active ? dfGlowColor.cgColor : nil
            img.layer?.shadowOffset = .zero
            img.layer?.shadowRadius = active ? 4.0 : 0
            img.layer?.shadowOpacity = active ? 0.85 : 0
        }
        for (unit, led) in floppyLedViews {
            let active = (state & UInt32(0x10 << unit)) != 0
            led.layer?.backgroundColor = (active ? dfOnColor : dfOffLedColor).cgColor
            led.layer?.shadowColor = active ? dfGlowColor.cgColor : nil
            led.layer?.shadowOffset = .zero
            led.layer?.shadowRadius = active ? 4.0 : 0
            led.layer?.shadowOpacity = active ? 0.95 : 0
        }

        let hdActive = (state & 0x04) != 0
        let hdOnColor = NSColor(red: 1.0, green: 0.28, blue: 0.20, alpha: 1.0)
        let hdGlowColor = NSColor(red: 1.0, green: 0.20, blue: 0.14, alpha: 1.0)
        let hdOffLedColor = NSColor(red: 0.22, green: 0.10, blue: 0.10, alpha: 1.0)

        for img in hdIconViews {
            img.contentTintColor = hdActive ? hdOnColor : MonokaiTheme.muted
            img.layer?.shadowColor = hdActive ? hdGlowColor.cgColor : nil
            img.layer?.shadowOffset = .zero
            img.layer?.shadowRadius = hdActive ? 4.0 : 0
            img.layer?.shadowOpacity = hdActive ? 0.85 : 0
        }
        for led in hdLedViews {
            led.layer?.backgroundColor = (hdActive ? hdOnColor : hdOffLedColor).cgColor
            led.layer?.shadowColor = hdActive ? hdGlowColor.cgColor : nil
            led.layer?.shadowOffset = .zero
            led.layer?.shadowRadius = hdActive ? 4.0 : 0
            led.layer?.shadowOpacity = hdActive ? 0.95 : 0
        }
    }

    func reloadUI() {
        guard isViewLoaded else { return }
        titleLabel?.stringValue = L10n.t("MASZYNY AMIGA", "AMIGA MACHINES")
        hwHeaderLabel?.stringValue = L10n.t("KONFIGURACJA SPRZĘTU", "HARDWARE CONFIGURATION")
        applyBtn?.title = L10n.t("Restartuj maszynę", "Restart Machine")
        resetToolBtn?.toolTip = L10n.t("Reset Amigi (Ctrl+R)", "Reset Amiga (Ctrl+R)")
        freezeToolBtn?.toolTip = L10n.t("Zamroź i zapisz stan maszyny (.arf) do Saves (Ctrl+S)", "Freeze & Save machine state (.arf) to Saves (Ctrl+S)")
        warpToolBtn?.toolTip = L10n.t("Tryb Warp 12x (Ctrl+T)", "Warp 12x mode (Ctrl+T)")
        joyToolBtn?.toolTip = L10n.t("Joystick na strzałkach + Shift (Ctrl+J)", "Arrow keys + Shift Joystick (Ctrl+J)")
        printToolBtn?.toolTip = L10n.t("Drukuj wydruk z Amigi (Ctrl+P)", "Print Amiga output (Ctrl+P)")
        fullToolBtn?.toolTip = L10n.t("Pełen ekran (Ctrl+F)", "Full Screen (Ctrl+F)")
        modelTable.reloadData()
        if let idx = AmigaModelType.allCases.firstIndex(of: selectedModel) {
            modelTable.selectRowIndexes(IndexSet(integer: idx), byExtendingSelection: false)
        }
        rebuildHardwareSection()
    }

    override func loadView() {
        let root = MonokaiTheme.makePanelView()
        self.view = root

        titleLabel = makeSectionHeader(L10n.t("MASZYNY AMIGA", "AMIGA MACHINES"))

        let col = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("model"))
        col.resizingMask = .autoresizingMask
        modelTable.addTableColumn(col)
        modelTable.headerView = nil
        modelTable.backgroundColor = .clear
        modelTable.rowHeight = 42
        modelTable.style = .sourceList
        modelTable.dataSource = self
        modelTable.delegate = self

        let tableScroll = NSScrollView()
        tableScroll.drawsBackground = false
        tableScroll.hasVerticalScroller = false
        tableScroll.documentView = modelTable
        tableScroll.translatesAutoresizingMaskIntoConstraints = false

        let divider = NSBox()
        divider.boxType = .separator
        divider.translatesAutoresizingMaskIntoConstraints = false

        let hwHeader = makeSectionHeader(L10n.t("KONFIGURACJA SPRZĘTU", "HARDWARE CONFIGURATION"))
        hwHeaderLabel = hwHeader

        hardwareStack.orientation = .vertical
        hardwareStack.alignment = .leading
        hardwareStack.spacing = 8
        hardwareStack.edgeInsets = NSEdgeInsets(top: 6, left: 12, bottom: 12, right: 12)
        hardwareStack.translatesAutoresizingMaskIntoConstraints = false

        scrollView.drawsBackground = false
        scrollView.hasVerticalScroller = true
        scrollView.documentView = hardwareStack
        scrollView.translatesAutoresizingMaskIntoConstraints = false

        applyBtn = NSButton(title: L10n.t("Restartuj maszynę", "Restart Machine"), target: self, action: #selector(onApplyClicked))
        applyBtn.bezelStyle = .rounded
        applyBtn.controlSize = .regular
        applyBtn.keyEquivalent = ""
        applyBtn.translatesAutoresizingMaskIntoConstraints = false

        func makeToolButton(_ symbol: String, _ tip: String, _ tool: SidebarTool, toggle: Bool) -> NSButton {
            let b = NSButton()
            let cfg = NSImage.SymbolConfiguration(pointSize: 13, weight: .semibold)
            b.image = NSImage(systemSymbolName: symbol, accessibilityDescription: tip)?.withSymbolConfiguration(cfg)
            b.imagePosition = .imageOnly
            b.bezelStyle = .texturedRounded
            b.isBordered = false
            b.setButtonType(toggle ? .pushOnPushOff : .momentaryPushIn)
            b.toolTip = tip
            b.tag = tool.rawValue
            b.target = self
            b.action = #selector(onToolClicked(_:))
            b.translatesAutoresizingMaskIntoConstraints = false
            b.heightAnchor.constraint(equalToConstant: 26).isActive = true
            applyToolButtonStyle(b, active: false, activeColor: MonokaiTheme.cyan)
            return b
        }
        resetToolBtn = makeToolButton("arrow.clockwise", L10n.t("Reset Amigi (Ctrl+R)", "Reset Amiga (Ctrl+R)"), .reset, toggle: false)
        freezeToolBtn = makeToolButton("snowflake", L10n.t("Zamroź i zapisz stan maszyny (.arf) do Saves (Ctrl+S)", "Freeze & Save machine state (.arf) to Saves (Ctrl+S)"), .freeze, toggle: false)
        warpToolBtn = makeToolButton("hare", L10n.t("Tryb Warp 12x (Ctrl+T)", "Warp 12x mode (Ctrl+T)"), .warp, toggle: true)
        joyToolBtn = makeToolButton("gamecontroller", L10n.t("Joystick na strzałkach + Shift (Ctrl+J)", "Arrow keys + Shift Joystick (Ctrl+J)"), .joystick, toggle: true)
        printToolBtn = makeToolButton("printer", L10n.t("Drukuj wydruk z Amigi (Ctrl+P)", "Print Amiga output (Ctrl+P)"), .print, toggle: false)
        fullToolBtn = makeToolButton("arrow.up.left.and.arrow.down.right", L10n.t("Pełen ekran (Ctrl+F)", "Full Screen (Ctrl+F)"), .fullScreen, toggle: false)
        toolsRow.orientation = .horizontal
        toolsRow.distribution = .fillEqually
        toolsRow.spacing = 6
        toolsRow.translatesAutoresizingMaskIntoConstraints = false
        for b in [resetToolBtn!, freezeToolBtn!, warpToolBtn!, joyToolBtn!, printToolBtn!, fullToolBtn!] {
            toolsRow.addArrangedSubview(b)
        }

        slomoRow.translatesAutoresizingMaskIntoConstraints = false
        slomoRow.onChanged = { [weak self] enabled, factor in
            self?.delegate?.sidebarDidChangeSlomo(enabled: enabled, factor: factor)
        }
        slomoRow.onFreezeMonitorRequested = { [weak self] in
            self?.delegate?.sidebarDidRequestActionReplayMonitor()
        }

        root.addSubview(titleLabel)
        root.addSubview(tableScroll)
        root.addSubview(divider)
        root.addSubview(scrollView)
        root.addSubview(toolsRow)
        root.addSubview(slomoRow)
        root.addSubview(applyBtn)

        NSLayoutConstraint.activate([
            toolsRow.leadingAnchor.constraint(equalTo: root.leadingAnchor, constant: 12),
            toolsRow.trailingAnchor.constraint(equalTo: root.trailingAnchor, constant: -12),
            toolsRow.topAnchor.constraint(equalTo: divider.bottomAnchor, constant: 10),

            slomoRow.leadingAnchor.constraint(equalTo: root.leadingAnchor, constant: 12),
            slomoRow.trailingAnchor.constraint(equalTo: root.trailingAnchor, constant: -12),
            slomoRow.topAnchor.constraint(equalTo: toolsRow.bottomAnchor, constant: 6),

            titleLabel.topAnchor.constraint(equalTo: root.safeAreaLayoutGuide.topAnchor, constant: 10),
            titleLabel.leadingAnchor.constraint(equalTo: root.leadingAnchor, constant: 14),
            titleLabel.trailingAnchor.constraint(equalTo: root.trailingAnchor, constant: -14),

            tableScroll.topAnchor.constraint(equalTo: titleLabel.bottomAnchor, constant: 6),
            tableScroll.leadingAnchor.constraint(equalTo: root.leadingAnchor),
            tableScroll.trailingAnchor.constraint(equalTo: root.trailingAnchor),
            tableScroll.heightAnchor.constraint(equalToConstant: 186),

            divider.topAnchor.constraint(equalTo: tableScroll.bottomAnchor, constant: 4),
            divider.leadingAnchor.constraint(equalTo: root.leadingAnchor, constant: 10),
            divider.trailingAnchor.constraint(equalTo: root.trailingAnchor, constant: -10),

            scrollView.topAnchor.constraint(equalTo: slomoRow.bottomAnchor, constant: 8),
            scrollView.leadingAnchor.constraint(equalTo: root.leadingAnchor),
            scrollView.trailingAnchor.constraint(equalTo: root.trailingAnchor),
            scrollView.bottomAnchor.constraint(equalTo: applyBtn.topAnchor, constant: -8),

            hardwareStack.topAnchor.constraint(equalTo: scrollView.contentView.topAnchor),
            hardwareStack.leadingAnchor.constraint(equalTo: scrollView.contentView.leadingAnchor),
            hardwareStack.trailingAnchor.constraint(equalTo: scrollView.contentView.trailingAnchor),

            applyBtn.leadingAnchor.constraint(equalTo: root.leadingAnchor, constant: 12),
            applyBtn.trailingAnchor.constraint(equalTo: root.trailingAnchor, constant: -12),
            applyBtn.bottomAnchor.constraint(equalTo: root.safeAreaLayoutGuide.bottomAnchor, constant: -12)
        ])

        reloadUI()
        setToolStates(warp: currentWarpState, joystick: currentJoystickState, fullScreen: currentFullScreenState)
    }

    private func makeSectionHeader(_ text: String) -> NSTextField {
        let lbl = NSTextField(labelWithString: text)
        lbl.font = .systemFont(ofSize: 11, weight: .semibold)
        lbl.textColor = .secondaryLabelColor
        lbl.translatesAutoresizingMaskIntoConstraints = false
        return lbl
    }

    private func rebuildHardwareSection() {
        floppyIconViews.removeAll()
        floppyLedViews.removeAll()
        hdIconViews.removeAll()
        hdLedViews.removeAll()
        for v in hardwareStack.arrangedSubviews {
            hardwareStack.removeArrangedSubview(v)
            v.removeFromSuperview()
        }
        defer {
            if lastLedState != 0xFFFF_FFFF {
                applyDriveActivityStyles(state: lastLedState)
            }
        }
        guard let profile = profiles[selectedModel] else { return }

        // 1. Kickstart ROM
        hardwareStack.addArrangedSubview(makeSubTitle("Kickstart ROM"))
        let romName = URL(fileURLWithPath: profile.romPath).lastPathComponent
        let romRow = makeRow(
            icon: "memorychip",
            title: romName,
            buttonTitle: L10n.t("Zmień", "Change"),
            action: #selector(onPickRom)
        )
        hardwareStack.addArrangedSubview(romRow)

        // 2. Stacje dyskietek DF0..DF3
        hardwareStack.addArrangedSubview(makeSubTitle(L10n.t("Stacje dyskietek", "Floppy Drives")))
        for slot in profile.floppies {
            let diskName = slot.diskPath.map { URL(fileURLWithPath: $0).lastPathComponent } ?? L10n.t("(pusta)", "(empty)")
            let row = makeFloppyRow(unit: slot.unit, diskName: diskName, readOnly: slot.readOnly)
            hardwareStack.addArrangedSubview(row)
        }
        if profile.floppies.count < 4 {
            let addDf = NSButton(title: L10n.t("+ Dodaj stację DF\(profile.floppies.count)", "+ Add Drive DF\(profile.floppies.count)"), target: self, action: #selector(onAddFloppy))
            addDf.bezelStyle = .inline
            addDf.controlSize = .small
            hardwareStack.addArrangedSubview(addDf)
        }

        // 3. Dyski twarde HDF
        hardwareStack.addArrangedSubview(makeSubTitle(L10n.t("Dyski twarde (.hdf)", "Hard Drives (.hdf)")))
        if profile.hardDrives.isEmpty {
            let empty = NSTextField(labelWithString: L10n.t("Brak podłączonych dysków", "No hard drives attached"))
            empty.font = .systemFont(ofSize: 11)
            empty.textColor = .tertiaryLabelColor
            hardwareStack.addArrangedSubview(empty)
        } else {
            for hd in profile.hardDrives {
                let hdName = URL(fileURLWithPath: hd.imagePath).lastPathComponent
                let row = makeHardDriveRow(unit: hd.unit, name: hdName)
                hardwareStack.addArrangedSubview(row)
            }
        }
        let hdBtns = NSStackView()
        hdBtns.orientation = .horizontal
        hdBtns.spacing = 6
        let addHd = NSButton(title: L10n.t("+ Dodaj (.hdf)", "+ Add (.hdf)"), target: self, action: #selector(onAddHardDrive))
        addHd.bezelStyle = .inline
        addHd.controlSize = .small
        let newHd = NSButton(title: L10n.t("+ Pusty (.hdf)", "+ Blank (.hdf)"), target: self, action: #selector(onCreateBlankHardDrive))
        newHd.bezelStyle = .inline
        newHd.controlSize = .small
        hdBtns.addArrangedSubview(addHd)
        hdBtns.addArrangedSubview(newHd)
        hardwareStack.addArrangedSubview(hdBtns)

        // 4. Chip RAM (A500 rev 5 / 6a switch or A600 / A601 1 MB / 2 MB switch)
        if selectedModel == .a500 {
            hardwareStack.addArrangedSubview(makeSubTitle("Chip RAM (A500 rev 5 / 6a)"))
            let chipSeg = NSSegmentedControl(
                labels: ["512 KB + 512 KB Slow", "1 MB + 512 KB Slow"],
                trackingMode: .selectOne,
                target: self,
                action: #selector(onA500ChipRamChanged(_:))
            )
            chipSeg.controlSize = .small
            chipSeg.selectedSegment = profile.a500Chip512KB ? 0 : 1
            hardwareStack.addArrangedSubview(chipSeg)
        } else if selectedModel == .a600 {
            hardwareStack.addArrangedSubview(makeSubTitle("Chip RAM (A600 / A601)"))
            let chipSeg = NSSegmentedControl(
                labels: ["1 MB Chip", "2 MB Chip (A601)"],
                trackingMode: .selectOne,
                target: self,
                action: #selector(onA600ChipRamChanged(_:))
            )
            chipSeg.controlSize = .small
            chipSeg.selectedSegment = profile.a600Chip2MB ? 1 : 0
            hardwareStack.addArrangedSubview(chipSeg)
        }

        // 5. Fast RAM
        let isA4000 = (selectedModel == .a4000)
        hardwareStack.addArrangedSubview(makeSubTitle(isA4000 ? "Fast RAM (Zorro III)" : "Fast RAM"))
        let ramLabels = isA4000 ? [L10n.t("Brak", "None"), "16 MB", "64 MB", "128 MB"]
                                : [L10n.t("Brak", "None"), "2 MB", "4 MB", "8 MB"]
        let ramValues = isA4000 ? [0, 16, 64, 128] : [0, 2, 4, 8]
        let ramSeg = NSSegmentedControl(labels: ramLabels, trackingMode: .selectOne, target: self, action: #selector(onFastRamChanged(_:)))
        ramSeg.controlSize = .small
        ramSeg.selectedSegment = ramValues.firstIndex(of: profile.fastRamMB) ?? 0
        hardwareStack.addArrangedSubview(ramSeg)
    }

    private func makeSubTitle(_ text: String) -> NSTextField {
        let lbl = NSTextField(labelWithString: text)
        lbl.font = .systemFont(ofSize: 11, weight: .medium)
        lbl.textColor = MonokaiTheme.yellow
        return lbl
    }

    private func makeRow(icon: String, title: String, buttonTitle: String, action: Selector) -> NSView {
        let row = NSStackView()
        row.orientation = .horizontal
        row.spacing = 6
        row.alignment = .centerY

        let img = NSImageView()
        img.image = NSImage(systemSymbolName: icon, accessibilityDescription: nil)
        img.contentTintColor = MonokaiTheme.green

        let lbl = NSTextField(labelWithString: title)
        lbl.font = .systemFont(ofSize: 11)
        lbl.textColor = MonokaiTheme.text
        lbl.lineBreakMode = .byTruncatingMiddle
        lbl.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)

        let btn = NSButton(title: buttonTitle, target: self, action: action)
        btn.bezelStyle = .rounded
        btn.controlSize = .mini

        row.addArrangedSubview(img)
        row.addArrangedSubview(lbl)
        row.addArrangedSubview(btn)
        return row
    }

    private func makeFloppyRow(unit: Int, diskName: String, readOnly: Bool) -> NSView {
        let box = NSStackView()
        box.orientation = .vertical
        box.alignment = .leading
        box.spacing = 3

        let top = NSStackView()
        top.orientation = .horizontal
        top.spacing = 6
        top.alignment = .centerY

        let img = NSImageView()
        img.wantsLayer = true
        img.image = NSImage(systemSymbolName: "opticaldiscdrive", accessibilityDescription: nil)
        img.contentTintColor = MonokaiTheme.muted
        floppyIconViews[unit] = img

        let led = NSView()
        led.wantsLayer = true
        led.layer?.cornerRadius = 1.1
        led.layer?.borderWidth = 0.5
        led.layer?.borderColor = NSColor(white: 0.05, alpha: 0.9).cgColor
        led.layer?.backgroundColor = NSColor(red: 0.20, green: 0.15, blue: 0.08, alpha: 1.0).cgColor
        led.translatesAutoresizingMaskIntoConstraints = false
        NSLayoutConstraint.activate([
            led.widthAnchor.constraint(equalToConstant: 8),
            led.heightAnchor.constraint(equalToConstant: 3)
        ])
        floppyLedViews[unit] = led

        let title = NSTextField(labelWithString: "DF\(unit): \(diskName)")
        title.font = .systemFont(ofSize: 11)
        title.textColor = MonokaiTheme.text
        title.lineBreakMode = .byTruncatingMiddle
        title.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)

        top.addArrangedSubview(img)
        top.addArrangedSubview(led)
        top.addArrangedSubview(title)

        let btns = NSStackView()
        btns.orientation = .horizontal
        btns.spacing = 4

        let insertBtn = NSButton(title: L10n.t("Włóż...", "Insert..."), target: self, action: #selector(onInsertFloppy(_:)))
        insertBtn.tag = unit
        insertBtn.bezelStyle = .rounded
        insertBtn.controlSize = .mini
        btns.addArrangedSubview(insertBtn)

        let blankBtn = NSButton(title: L10n.t("Pusty", "Blank"), target: self, action: #selector(onCreateBlankFloppy(_:)))
        blankBtn.tag = unit
        blankBtn.bezelStyle = .rounded
        blankBtn.controlSize = .mini
        btns.addArrangedSubview(blankBtn)

        let rwBtn = NSButton(title: readOnly ? "RO" : "RW", target: self, action: #selector(onToggleFloppyWriteProtect(_:)))
        rwBtn.tag = unit
        rwBtn.bezelStyle = .rounded
        rwBtn.controlSize = .mini
        rwBtn.contentTintColor = readOnly ? .systemOrange : .systemGreen
        rwBtn.toolTip = readOnly
            ? L10n.t("Tylko odczyt (RO) — kliknij, aby włączyć zapis (RW)", "Read-only (RO) — click to enable write (RW)")
            : L10n.t("Odczyt i zapis (RW) — kliknij, aby włączyć blokadę zapisu (RO)", "Read/Write (RW) — click to enable write-protect (RO)")
        btns.addArrangedSubview(rwBtn)

        let ejectBtn = NSButton(title: L10n.t("Wysuń", "Eject"), target: self, action: #selector(onEjectFloppy(_:)))
        ejectBtn.tag = unit
        ejectBtn.bezelStyle = .rounded
        ejectBtn.controlSize = .mini
        btns.addArrangedSubview(ejectBtn)

        if unit > 0 {
            let delBtn = NSButton(title: L10n.t("Usuń", "Remove"), target: self, action: #selector(onRemoveFloppy(_:)))
            delBtn.tag = unit
            delBtn.bezelStyle = .rounded
            delBtn.controlSize = .mini
            btns.addArrangedSubview(delBtn)
        }

        box.addArrangedSubview(top)
        box.addArrangedSubview(btns)
        return box
    }

    private func makeHardDriveRow(unit: Int, name: String) -> NSView {
        let row = NSStackView()
        row.orientation = .horizontal
        row.spacing = 6
        row.alignment = .centerY

        let img = NSImageView()
        img.wantsLayer = true
        img.image = NSImage(systemSymbolName: "internaldrive", accessibilityDescription: nil)
        img.contentTintColor = MonokaiTheme.muted
        hdIconViews.append(img)

        let led = NSView()
        led.wantsLayer = true
        led.layer?.cornerRadius = 1.1
        led.layer?.borderWidth = 0.5
        led.layer?.borderColor = NSColor(white: 0.05, alpha: 0.9).cgColor
        led.layer?.backgroundColor = NSColor(red: 0.22, green: 0.10, blue: 0.10, alpha: 1.0).cgColor
        led.translatesAutoresizingMaskIntoConstraints = false
        NSLayoutConstraint.activate([
            led.widthAnchor.constraint(equalToConstant: 8),
            led.heightAnchor.constraint(equalToConstant: 3)
        ])
        hdLedViews.append(led)

        let lbl = NSTextField(labelWithString: "DH\(unit): \(name)")
        lbl.font = .systemFont(ofSize: 11)
        lbl.lineBreakMode = .byTruncatingMiddle
        lbl.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)

        let delBtn = NSButton(title: L10n.t("Usuń", "Remove"), target: self, action: #selector(onRemoveHardDrive(_:)))
        delBtn.tag = unit
        delBtn.bezelStyle = .rounded
        delBtn.controlSize = .mini

        row.addArrangedSubview(img)
        row.addArrangedSubview(led)
        row.addArrangedSubview(lbl)
        row.addArrangedSubview(delBtn)
        return row
    }

    @objc private func onPickRom() {
        if let p = profiles[selectedModel] { delegate?.sidebarDidRequestRomPick(for: p) }
    }

    @objc private func onInsertFloppy(_ sender: NSButton) {
        if let p = profiles[selectedModel] { delegate?.sidebarDidRequestInsertFloppy(unit: sender.tag, for: p) }
    }

    @objc private func onCreateBlankFloppy(_ sender: NSButton) {
        if let p = profiles[selectedModel] { delegate?.sidebarDidRequestCreateBlankFloppy(unit: sender.tag, for: p) }
    }

    @objc private func onEjectFloppy(_ sender: NSButton) {
        if let p = profiles[selectedModel] { delegate?.sidebarDidEjectFloppy(unit: sender.tag, for: p) }
    }

    @objc private func onAddFloppy() {
        if let p = profiles[selectedModel] { delegate?.sidebarDidAddFloppy(for: p) }
    }

    @objc private func onRemoveFloppy(_ sender: NSButton) {
        if let p = profiles[selectedModel] { delegate?.sidebarDidRemoveFloppy(unit: sender.tag, for: p) }
    }

    @objc private func onAddHardDrive() {
        if let p = profiles[selectedModel] { delegate?.sidebarDidRequestAddHardDrive(for: p) }
    }

    @objc private func onCreateBlankHardDrive() {
        if let p = profiles[selectedModel] { delegate?.sidebarDidRequestCreateBlankHardDrive(for: p) }
    }

    @objc private func onRemoveHardDrive(_ sender: NSButton) {
        if let p = profiles[selectedModel] { delegate?.sidebarDidRemoveHardDrive(unit: sender.tag, for: p) }
    }

    @objc private func onToggleFloppyWriteProtect(_ sender: NSButton) {
        if let p = profiles[selectedModel] { delegate?.sidebarDidToggleFloppyWriteProtect(unit: sender.tag, for: p) }
    }

    @objc private func onA500ChipRamChanged(_ sender: NSSegmentedControl) {
        guard let p = profiles[.a500] else { return }
        delegate?.sidebarDidChangeA500ChipRam(use512KB: sender.selectedSegment == 0, for: p)
    }

    @objc private func onA600ChipRamChanged(_ sender: NSSegmentedControl) {
        guard let p = profiles[.a600] else { return }
        delegate?.sidebarDidChangeA600ChipRam(use2MB: sender.selectedSegment == 1, for: p)
    }

    @objc private func onFastRamChanged(_ sender: NSSegmentedControl) {
        let ramValues = (selectedModel == .a4000) ? [0, 16, 64, 128] : [0, 2, 4, 8]
        let idx = sender.selectedSegment
        guard idx >= 0 && idx < ramValues.count, let p = profiles[selectedModel] else { return }
        delegate?.sidebarDidChangeFastRam(mb: ramValues[idx], for: p)
    }

    @objc private func onApplyClicked() {
        if let p = profiles[selectedModel] { delegate?.sidebarDidRequestApplyAndReset(profile: p) }
    }

    // MARK: - NSTableViewDataSource & Delegate

    func numberOfRows(in tableView: NSTableView) -> Int {
        return AmigaModelType.allCases.count
    }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        let model = AmigaModelType.allCases[row]
        let cell = NSTableCellView()

        let img = NSImageView()
        img.image = NSImage(systemSymbolName: "desktopcomputer", accessibilityDescription: nil)
        img.contentTintColor = MonokaiTheme.cyan
        img.translatesAutoresizingMaskIntoConstraints = false

        let title = NSTextField(labelWithString: model.rawValue)
        title.font = .systemFont(ofSize: 13, weight: .medium)
        title.textColor = MonokaiTheme.text
        title.translatesAutoresizingMaskIntoConstraints = false

        let subtitleText: String
        if model == .a500, let p = profiles[.a500], p.a500Chip512KB {
            subtitleText = "OCS • 68000 • 512 KB Chip + 512 KB Slow"
        } else if model == .a600, let p = profiles[.a600], p.a600Chip2MB {
            subtitleText = "ECS • 68000 • 2 MB Chip (A601)"
        } else {
            subtitleText = model.subtitle
        }
        let sub = NSTextField(labelWithString: subtitleText)
        sub.font = .systemFont(ofSize: 10)
        sub.textColor = MonokaiTheme.muted
        sub.translatesAutoresizingMaskIntoConstraints = false

        cell.addSubview(img)
        cell.addSubview(title)
        cell.addSubview(sub)

        NSLayoutConstraint.activate([
            img.leadingAnchor.constraint(equalTo: cell.leadingAnchor, constant: 8),
            img.centerYAnchor.constraint(equalTo: cell.centerYAnchor),
            img.widthAnchor.constraint(equalToConstant: 20),

            title.leadingAnchor.constraint(equalTo: img.trailingAnchor, constant: 8),
            title.trailingAnchor.constraint(equalTo: cell.trailingAnchor, constant: -6),
            title.topAnchor.constraint(equalTo: cell.topAnchor, constant: 5),

            sub.leadingAnchor.constraint(equalTo: title.leadingAnchor),
            sub.trailingAnchor.constraint(equalTo: title.trailingAnchor),
            sub.topAnchor.constraint(equalTo: title.bottomAnchor, constant: 1)
        ])
        return cell
    }

    func tableViewSelectionDidChange(_ notification: Notification) {
        let row = modelTable.selectedRow
        guard row >= 0 && row < AmigaModelType.allCases.count else { return }
        let model = AmigaModelType.allCases[row]
        if model != selectedModel {
            selectedModel = model
            rebuildHardwareSection()
            delegate?.sidebarDidSelectModel(model)
        }
    }
}
