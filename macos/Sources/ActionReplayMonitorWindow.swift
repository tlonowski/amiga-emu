import AppKit
import AmigaCore

/// Action Replay Mk III Freeze Monitor, Memory Editor, and Trainer (TS / T / W) window.
final class ActionReplayMonitorWindowController: NSWindowController, NSTableViewDataSource, NSTableViewDelegate {
    private let machine: OpaquePointer
    private let onSaveSnapshot: () -> Void
    private let onResumeEmulation: () -> Void

    private var baseAddress: UInt32 = 0x000000
    private let bytesPerRow: UInt32 = 16
    private let visibleRows: Int = 24

    // Trainer state
    private var trainerCandidates: [UInt32] = []
    private var trainerWidth: Int = 1 // 1 = byte (8-bit), 2 = word (16-bit), 4 = long (32-bit)

    private let regsLabel = NSTextField(labelWithString: "")
    private let addrField = NSTextField()
    private let regionPopup = NSPopUpButton()
    private let hexTable = NSTableView()

    private let pokeAddrField = NSTextField()
    private let pokeValField = NSTextField()
    private let pokeWidthPopup = NSPopUpButton()

    private let trainerValField = NSTextField()
    private let trainerWidthPopup = NSPopUpButton()
    private let trainerCountLabel = NSTextField(labelWithString: "")
    private let trainerTable = NSTableView()

    init(machine: OpaquePointer, onSaveSnapshot: @escaping () -> Void, onResumeEmulation: @escaping () -> Void) {
        self.machine = machine
        self.onSaveSnapshot = onSaveSnapshot
        self.onResumeEmulation = onResumeEmulation

        let win = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 820, height: 560),
            styleMask: [.titled, .closable, .resizable],
            backing: .buffered,
            defer: false
        )
        win.title = L10n.t("Action Replay Mk III — Freeze Monitor & Edytor Pamięci", "Action Replay Mk III — Freeze Monitor & Memory Editor")
        win.appearance = NSAppearance(named: .darkAqua)
        win.backgroundColor = MonokaiTheme.bgDark
        win.minSize = NSSize(width: 760, height: 480)
        super.init(window: win)
        setupUI()
        refreshAll()
        win.center()
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    private func setupUI() {
        guard let contentView = window?.contentView else { return }
        contentView.wantsLayer = true
        contentView.layer?.backgroundColor = MonokaiTheme.bgDark.cgColor

        // Top Action Replay header banner + CPU registers
        let banner = NSTextField(labelWithString: "ACTION REPLAY MK III — FREEZE MONITOR")
        banner.font = .monospacedSystemFont(ofSize: 13, weight: .heavy)
        banner.textColor = NSColor(red: 1.0, green: 0.24, blue: 0.26, alpha: 1.0)
        banner.translatesAutoresizingMaskIntoConstraints = false

        let saveArfBtn = NSButton(title: L10n.t("Zapisz zrzut (.arf)", "Save Snapshot (.arf)"), target: self, action: #selector(onSaveArfClicked))
        saveArfBtn.bezelStyle = .rounded
        saveArfBtn.controlSize = .small
        saveArfBtn.translatesAutoresizingMaskIntoConstraints = false

        let resumeBtn = NSButton(title: L10n.t("Wznów grę (X)", "Resume Game (X)"), target: self, action: #selector(onResumeClicked))
        resumeBtn.bezelStyle = .rounded
        resumeBtn.controlSize = .small
        resumeBtn.keyEquivalent = "\r"
        resumeBtn.translatesAutoresizingMaskIntoConstraints = false

        regsLabel.font = .monospacedSystemFont(ofSize: 11, weight: .regular)
        regsLabel.textColor = MonokaiTheme.green
        regsLabel.maximumNumberOfLines = 3
        regsLabel.lineBreakMode = .byClipping
        regsLabel.wantsLayer = true
        regsLabel.layer?.backgroundColor = NSColor.black.withAlphaComponent(0.35).cgColor
        regsLabel.layer?.borderColor = MonokaiTheme.border.cgColor
        regsLabel.layer?.borderWidth = 1.0
        regsLabel.layer?.cornerRadius = 5
        regsLabel.translatesAutoresizingMaskIntoConstraints = false

        // Navigation bar (Memory Region + Address Jump + Page Up/Down)
        let addrLbl = NSTextField(labelWithString: L10n.t("Adres (Hex):", "Address (Hex):"))
        addrLbl.font = .systemFont(ofSize: 11, weight: .medium)
        addrLbl.textColor = MonokaiTheme.muted
        addrLbl.translatesAutoresizingMaskIntoConstraints = false

        addrField.stringValue = "000000"
        addrField.font = .monospacedSystemFont(ofSize: 12, weight: .medium)
        addrField.target = self
        addrField.action = #selector(onAddressEntered)
        addrField.translatesAutoresizingMaskIntoConstraints = false

        let goBtn = NSButton(title: L10n.t("Idź (M)", "Go (M)"), target: self, action: #selector(onAddressEntered))
        goBtn.bezelStyle = .inline
        goBtn.controlSize = .small
        goBtn.translatesAutoresizingMaskIntoConstraints = false

        let prevPageBtn = NSButton(title: "◀ -256B", target: self, action: #selector(onPrevPage))
        prevPageBtn.bezelStyle = .inline
        prevPageBtn.controlSize = .small
        prevPageBtn.translatesAutoresizingMaskIntoConstraints = false

        let nextPageBtn = NSButton(title: "+256B ▶", target: self, action: #selector(onNextPage))
        nextPageBtn.bezelStyle = .inline
        nextPageBtn.controlSize = .small
        nextPageBtn.translatesAutoresizingMaskIntoConstraints = false

        regionPopup.controlSize = .small
        regionPopup.font = .systemFont(ofSize: 11)
        regionPopup.target = self
        regionPopup.action = #selector(onRegionSelected)
        regionPopup.translatesAutoresizingMaskIntoConstraints = false

        // Hex table (left pane)
        let colAddr = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("addr"))
        colAddr.title = L10n.t("Adres", "Address")
        colAddr.width = 74
        let colHex = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("hex"))
        colHex.title = "00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F"
        colHex.width = 345
        let colAscii = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("ascii"))
        colAscii.title = "ASCII"
        colAscii.width = 128

        hexTable.addTableColumn(colAddr)
        hexTable.addTableColumn(colHex)
        hexTable.addTableColumn(colAscii)
        hexTable.backgroundColor = NSColor.black.withAlphaComponent(0.30)
        hexTable.rowHeight = 19
        hexTable.intercellSpacing = NSSize(width: 4, height: 1)
        hexTable.dataSource = self
        hexTable.delegate = self
        hexTable.target = self
        hexTable.doubleAction = #selector(onHexRowDoubleClicked)

        let hexScroll = NSScrollView()
        hexScroll.drawsBackground = false
        hexScroll.hasVerticalScroller = true
        hexScroll.documentView = hexTable
        hexScroll.wantsLayer = true
        hexScroll.layer?.borderColor = MonokaiTheme.border.cgColor
        hexScroll.layer?.borderWidth = 1.0
        hexScroll.layer?.cornerRadius = 5
        hexScroll.translatesAutoresizingMaskIntoConstraints = false

        // Bottom POKE bar (direct memory write)
        let pokeTitle = NSTextField(labelWithString: L10n.t("EDYCJA KOMÓRKI (POKE):", "EDIT MEMORY (POKE):"))
        pokeTitle.font = .systemFont(ofSize: 11, weight: .bold)
        pokeTitle.textColor = MonokaiTheme.yellow
        pokeTitle.translatesAutoresizingMaskIntoConstraints = false

        pokeAddrField.placeholderString = "Adres hex (np. 040000)"
        pokeAddrField.stringValue = "000000"
        pokeAddrField.font = .monospacedSystemFont(ofSize: 11, weight: .regular)
        pokeAddrField.translatesAutoresizingMaskIntoConstraints = false

        pokeWidthPopup.controlSize = .small
        pokeWidthPopup.font = .systemFont(ofSize: 11)
        pokeWidthPopup.addItems(withTitles: [
            L10n.t("Bajt (8-bit)", "Byte (8-bit)"),
            L10n.t("Słowo (16-bit)", "Word (16-bit)"),
            L10n.t("Długie słowo (32-bit)", "Long (32-bit)"),
            L10n.t("Ciąg bajtów Hex", "Hex byte sequence")
        ])
        pokeWidthPopup.translatesAutoresizingMaskIntoConstraints = false

        pokeValField.placeholderString = L10n.t("Wartość (np. 99 lub $63)", "Value (e.g. 99 or $63)")
        pokeValField.font = .monospacedSystemFont(ofSize: 11, weight: .regular)
        pokeValField.target = self
        pokeValField.action = #selector(onPokeClicked)
        pokeValField.translatesAutoresizingMaskIntoConstraints = false

        let pokeBtn = NSButton(title: L10n.t("Zapisz do RAM", "Write to RAM"), target: self, action: #selector(onPokeClicked))
        pokeBtn.bezelStyle = .rounded
        pokeBtn.controlSize = .small
        pokeBtn.translatesAutoresizingMaskIntoConstraints = false

        // Right Pane: Action Replay Trainer (TS / T)
        let trainerBox = NSView()
        trainerBox.wantsLayer = true
        trainerBox.layer?.backgroundColor = NSColor.black.withAlphaComponent(0.25).cgColor
        trainerBox.layer?.borderColor = MonokaiTheme.border.cgColor
        trainerBox.layer?.borderWidth = 1.0
        trainerBox.layer?.cornerRadius = 6
        trainerBox.translatesAutoresizingMaskIntoConstraints = false

        let trHeader = NSTextField(labelWithString: L10n.t("TRAINER (TS / T — SZUKAJ ŻYĆ / AMUNICJI)", "TRAINER (TS / T — FIND LIVES / AMMO)"))
        trHeader.font = .systemFont(ofSize: 10, weight: .bold)
        trHeader.textColor = MonokaiTheme.orange
        trHeader.translatesAutoresizingMaskIntoConstraints = false

        trainerWidthPopup.controlSize = .small
        trainerWidthPopup.font = .systemFont(ofSize: 11)
        trainerWidthPopup.addItems(withTitles: [
            L10n.t("8-bit (0..255)", "8-bit (0..255)"),
            L10n.t("16-bit (0..65535)", "16-bit (0..65535)"),
            L10n.t("32-bit", "32-bit")
        ])
        trainerWidthPopup.translatesAutoresizingMaskIntoConstraints = false

        trainerValField.placeholderString = L10n.t("Wartość (np. 3 życia lub $03)", "Value (e.g. 3 lives or $03)")
        trainerValField.font = .monospacedSystemFont(ofSize: 12, weight: .medium)
        trainerValField.translatesAutoresizingMaskIntoConstraints = false

        let tsBtn = NSButton(title: L10n.t("Start (TS)", "Start (TS)"), target: self, action: #selector(onTrainerStart))
        tsBtn.bezelStyle = .rounded
        tsBtn.controlSize = .small
        tsBtn.toolTip = L10n.t("Nowe wyszukiwanie wartości w całej pamięci RAM (Trainer Start)", "New value search across all RAM (Trainer Start)")
        tsBtn.translatesAutoresizingMaskIntoConstraints = false

        let tFilterBtn = NSButton(title: L10n.t("Filtruj (T)", "Filter (T)"), target: self, action: #selector(onTrainerFilter))
        tFilterBtn.bezelStyle = .rounded
        tFilterBtn.controlSize = .small
        tFilterBtn.toolTip = L10n.t("Zawęź poprzednie wyniki do nowej wartości (np. po utracie życia)", "Filter previous candidates to new value (e.g. after losing a life)")
        tFilterBtn.translatesAutoresizingMaskIntoConstraints = false

        trainerCountLabel.font = .systemFont(ofSize: 11, weight: .medium)
        trainerCountLabel.textColor = MonokaiTheme.cyan
        trainerCountLabel.stringValue = L10n.t("Wpisz wartość (np. liczbę żyć) i kliknij Start (TS)", "Enter value (e.g. lives count) and click Start (TS)")
        trainerCountLabel.lineBreakMode = .byTruncatingTail
        trainerCountLabel.translatesAutoresizingMaskIntoConstraints = false

        let trColAddr = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("tr_addr"))
        trColAddr.title = L10n.t("Adres", "Address")
        trColAddr.width = 90
        let trColVal = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("tr_val"))
        trColVal.title = L10n.t("Aktualna wartość", "Current Value")
        trColVal.width = 110
        trainerTable.addTableColumn(trColAddr)
        trainerTable.addTableColumn(trColVal)
        trainerTable.backgroundColor = .clear
        trainerTable.rowHeight = 20
        trainerTable.dataSource = self
        trainerTable.delegate = self
        trainerTable.target = self
        trainerTable.action = #selector(onTrainerRowClicked)

        let trScroll = NSScrollView()
        trScroll.drawsBackground = false
        trScroll.hasVerticalScroller = true
        trScroll.documentView = trainerTable
        trScroll.translatesAutoresizingMaskIntoConstraints = false

        for v in [trHeader, trainerWidthPopup, trainerValField, tsBtn, tFilterBtn, trainerCountLabel, trScroll] {
            trainerBox.addSubview(v)
        }

        NSLayoutConstraint.activate([
            trHeader.topAnchor.constraint(equalTo: trainerBox.topAnchor, constant: 8),
            trHeader.leadingAnchor.constraint(equalTo: trainerBox.leadingAnchor, constant: 8),
            trHeader.trailingAnchor.constraint(equalTo: trainerBox.trailingAnchor, constant: -8),

            trainerWidthPopup.topAnchor.constraint(equalTo: trHeader.bottomAnchor, constant: 6),
            trainerWidthPopup.leadingAnchor.constraint(equalTo: trainerBox.leadingAnchor, constant: 8),
            trainerWidthPopup.trailingAnchor.constraint(equalTo: trainerBox.trailingAnchor, constant: -8),

            trainerValField.topAnchor.constraint(equalTo: trainerWidthPopup.bottomAnchor, constant: 6),
            trainerValField.leadingAnchor.constraint(equalTo: trainerBox.leadingAnchor, constant: 8),
            trainerValField.trailingAnchor.constraint(equalTo: trainerBox.trailingAnchor, constant: -8),

            tsBtn.topAnchor.constraint(equalTo: trainerValField.bottomAnchor, constant: 6),
            tsBtn.leadingAnchor.constraint(equalTo: trainerBox.leadingAnchor, constant: 8),

            tFilterBtn.centerYAnchor.constraint(equalTo: tsBtn.centerYAnchor),
            tFilterBtn.leadingAnchor.constraint(equalTo: tsBtn.trailingAnchor, constant: 6),
            tFilterBtn.trailingAnchor.constraint(lessThanOrEqualTo: trainerBox.trailingAnchor, constant: -8),

            trainerCountLabel.topAnchor.constraint(equalTo: tsBtn.bottomAnchor, constant: 6),
            trainerCountLabel.leadingAnchor.constraint(equalTo: trainerBox.leadingAnchor, constant: 8),
            trainerCountLabel.trailingAnchor.constraint(equalTo: trainerBox.trailingAnchor, constant: -8),

            trScroll.topAnchor.constraint(equalTo: trainerCountLabel.bottomAnchor, constant: 6),
            trScroll.leadingAnchor.constraint(equalTo: trainerBox.leadingAnchor, constant: 4),
            trScroll.trailingAnchor.constraint(equalTo: trainerBox.trailingAnchor, constant: -4),
            trScroll.bottomAnchor.constraint(equalTo: trainerBox.bottomAnchor, constant: -6)
        ])

        for v in [
            banner, saveArfBtn, resumeBtn, regsLabel,
            addrLbl, addrField, goBtn, prevPageBtn, nextPageBtn, regionPopup,
            hexScroll, trainerBox,
            pokeTitle, pokeAddrField, pokeWidthPopup, pokeValField, pokeBtn
        ] {
            contentView.addSubview(v)
        }

        NSLayoutConstraint.activate([
            banner.topAnchor.constraint(equalTo: contentView.topAnchor, constant: 12),
            banner.leadingAnchor.constraint(equalTo: contentView.leadingAnchor, constant: 14),

            resumeBtn.centerYAnchor.constraint(equalTo: banner.centerYAnchor),
            resumeBtn.trailingAnchor.constraint(equalTo: contentView.trailingAnchor, constant: -14),

            saveArfBtn.centerYAnchor.constraint(equalTo: banner.centerYAnchor),
            saveArfBtn.trailingAnchor.constraint(equalTo: resumeBtn.leadingAnchor, constant: -8),

            regsLabel.topAnchor.constraint(equalTo: banner.bottomAnchor, constant: 8),
            regsLabel.leadingAnchor.constraint(equalTo: contentView.leadingAnchor, constant: 14),
            regsLabel.trailingAnchor.constraint(equalTo: contentView.trailingAnchor, constant: -14),
            regsLabel.heightAnchor.constraint(equalToConstant: 48),

            addrLbl.topAnchor.constraint(equalTo: regsLabel.bottomAnchor, constant: 10),
            addrLbl.leadingAnchor.constraint(equalTo: contentView.leadingAnchor, constant: 14),

            addrField.centerYAnchor.constraint(equalTo: addrLbl.centerYAnchor),
            addrField.leadingAnchor.constraint(equalTo: addrLbl.trailingAnchor, constant: 6),
            addrField.widthAnchor.constraint(equalToConstant: 84),

            goBtn.centerYAnchor.constraint(equalTo: addrLbl.centerYAnchor),
            goBtn.leadingAnchor.constraint(equalTo: addrField.trailingAnchor, constant: 4),

            prevPageBtn.centerYAnchor.constraint(equalTo: addrLbl.centerYAnchor),
            prevPageBtn.leadingAnchor.constraint(equalTo: goBtn.trailingAnchor, constant: 8),

            nextPageBtn.centerYAnchor.constraint(equalTo: addrLbl.centerYAnchor),
            nextPageBtn.leadingAnchor.constraint(equalTo: prevPageBtn.trailingAnchor, constant: 4),

            regionPopup.centerYAnchor.constraint(equalTo: addrLbl.centerYAnchor),
            regionPopup.leadingAnchor.constraint(equalTo: nextPageBtn.trailingAnchor, constant: 10),

            hexScroll.topAnchor.constraint(equalTo: addrLbl.bottomAnchor, constant: 8),
            hexScroll.leadingAnchor.constraint(equalTo: contentView.leadingAnchor, constant: 14),
            hexScroll.bottomAnchor.constraint(equalTo: pokeTitle.topAnchor, constant: -12),
            hexScroll.widthAnchor.constraint(equalToConstant: 565),

            trainerBox.topAnchor.constraint(equalTo: hexScroll.topAnchor),
            trainerBox.leadingAnchor.constraint(equalTo: hexScroll.trailingAnchor, constant: 10),
            trainerBox.trailingAnchor.constraint(equalTo: contentView.trailingAnchor, constant: -14),
            trainerBox.bottomAnchor.constraint(equalTo: hexScroll.bottomAnchor),

            pokeTitle.leadingAnchor.constraint(equalTo: contentView.leadingAnchor, constant: 14),
            pokeTitle.bottomAnchor.constraint(equalTo: contentView.bottomAnchor, constant: -14),

            pokeAddrField.centerYAnchor.constraint(equalTo: pokeTitle.centerYAnchor),
            pokeAddrField.leadingAnchor.constraint(equalTo: pokeTitle.trailingAnchor, constant: 8),
            pokeAddrField.widthAnchor.constraint(equalToConstant: 84),

            pokeWidthPopup.centerYAnchor.constraint(equalTo: pokeTitle.centerYAnchor),
            pokeWidthPopup.leadingAnchor.constraint(equalTo: pokeAddrField.trailingAnchor, constant: 6),

            pokeValField.centerYAnchor.constraint(equalTo: pokeTitle.centerYAnchor),
            pokeValField.leadingAnchor.constraint(equalTo: pokeWidthPopup.trailingAnchor, constant: 6),
            pokeValField.widthAnchor.constraint(equalToConstant: 180),

            pokeBtn.centerYAnchor.constraint(equalTo: pokeTitle.centerYAnchor),
            pokeBtn.leadingAnchor.constraint(equalTo: pokeValField.trailingAnchor, constant: 8)
        ])
    }

    func refreshAll() {
        updateRegionsPopup()
        updateRegistersBanner()
        addrField.stringValue = String(format: "%06X", baseAddress)
        hexTable.reloadData()
        trainerTable.reloadData()
    }

    private func updateRegionsPopup() {
        regionPopup.removeAllItems()
        let chipSize = amiga_get_chip_ram_size(machine)
        regionPopup.addItem(withTitle: String(format: "Chip RAM ($000000 - $%06X)", chipSize > 0 ? chipSize - 1 : 0))
        regionPopup.lastItem?.tag = 0x000000

        let slowSize = amiga_get_slow_ram_size(machine)
        if slowSize > 0 {
            regionPopup.addItem(withTitle: String(format: "Slow RAM ($C00000 - $%06X)", 0xC00000 + slowSize - 1))
            regionPopup.lastItem?.tag = 0xC00000
        }

        let fastBase = amiga_get_fast_ram_base(machine)
        let fastSize = amiga_get_fast_ram_size(machine)
        if fastSize > 0 {
            regionPopup.addItem(withTitle: String(format: "Fast RAM ($%06X - $%06X)", fastBase, fastBase + fastSize - 1))
            regionPopup.lastItem?.tag = Int(fastBase)
        }

        let mbFastSize = amiga_get_mb_fast_ram_size(machine)
        if mbFastSize > 0 {
            regionPopup.addItem(withTitle: String(format: "A4000 MB Fast RAM ($07000000 - $%08X)", 0x07000000 + mbFastSize - 1))
            regionPopup.lastItem?.tag = 0x07000000
        }

        regionPopup.addItem(withTitle: "Kickstart ROM ($F80000)")
        regionPopup.lastItem?.tag = 0xF80000
    }

    private func updateRegistersBanner() {
        let pc = amiga_get_pc(machine)
        let sr = amiga_get_sr(machine)
        var dParts: [String] = []
        var aParts: [String] = []
        for i in 0..<8 {
            dParts.append(String(format: "D%d:%08X", i, amiga_get_d_reg(machine, Int32(i))))
            aParts.append(String(format: "A%d:%08X", i, amiga_get_a_reg(machine, Int32(i))))
        }
        let line1 = String(format: "  PC:%08X   SR:%04X   USP:%08X", pc, sr, amiga_get_a_reg(machine, 7))
        let line2 = "  " + dParts.joined(separator: " ")
        let line3 = "  " + aParts.joined(separator: " ")
        regsLabel.stringValue = "\(line1)\n\(line2)\n\(line3)"
    }

    private func parseNumber(_ text: String) -> UInt32? {
        let s = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !s.isEmpty else { return nil }
        if s.hasPrefix("$") {
            return UInt32(s.dropFirst(), radix: 16)
        } else if s.lowercased().hasPrefix("0x") {
            return UInt32(s.dropFirst(2), radix: 16)
        }
        return UInt32(s, radix: 10) ?? UInt32(s, radix: 16)
    }

    private func parseHexAddress(_ text: String) -> UInt32? {
        var s = text.trimmingCharacters(in: .whitespacesAndNewlines)
        if s.hasPrefix("$") { s = String(s.dropFirst()) }
        if s.lowercased().hasPrefix("0x") { s = String(s.dropFirst(2)) }
        return UInt32(s, radix: 16)
    }

    private func readValue(at addr: UInt32, width: Int) -> UInt32 {
        switch width {
        case 2:
            let hi = UInt32(amiga_peek_ram8(machine, addr))
            let lo = UInt32(amiga_peek_ram8(machine, addr &+ 1))
            return (hi << 8) | lo
        case 4:
            let b0 = UInt32(amiga_peek_ram8(machine, addr))
            let b1 = UInt32(amiga_peek_ram8(machine, addr &+ 1))
            let b2 = UInt32(amiga_peek_ram8(machine, addr &+ 2))
            let b3 = UInt32(amiga_peek_ram8(machine, addr &+ 3))
            return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3
        default:
            return UInt32(amiga_peek_ram8(machine, addr))
        }
    }

    private func writeValue(_ val: UInt32, at addr: UInt32, width: Int) {
        switch width {
        case 2:
            amiga_poke_ram8(machine, addr, UInt8((val >> 8) & 0xFF))
            amiga_poke_ram8(machine, addr &+ 1, UInt8(val & 0xFF))
        case 4:
            amiga_poke_ram8(machine, addr, UInt8((val >> 24) & 0xFF))
            amiga_poke_ram8(machine, addr &+ 1, UInt8((val >> 16) & 0xFF))
            amiga_poke_ram8(machine, addr &+ 2, UInt8((val >> 8) & 0xFF))
            amiga_poke_ram8(machine, addr &+ 3, UInt8(val & 0xFF))
        default:
            amiga_poke_ram8(machine, addr, UInt8(val & 0xFF))
        }
    }

    private func activeRamRanges() -> [(UInt32, UInt32)] {
        var ranges: [(UInt32, UInt32)] = []
        let chipSize = amiga_get_chip_ram_size(machine)
        if chipSize > 0 {
            ranges.append((0x000000, chipSize))
        }
        let slowSize = amiga_get_slow_ram_size(machine)
        if slowSize > 0 {
            ranges.append((0xC00000, 0xC00000 + slowSize))
        }
        let fastBase = amiga_get_fast_ram_base(machine)
        let fastSize = amiga_get_fast_ram_size(machine)
        if fastSize > 0 {
            ranges.append((fastBase, fastBase + fastSize))
        }
        let mbFastSize = amiga_get_mb_fast_ram_size(machine)
        if mbFastSize > 0 {
            ranges.append((0x07000000, 0x07000000 + mbFastSize))
        }
        return ranges
    }

    // MARK: - Actions

    @objc private func onAddressEntered() {
        if let a = parseHexAddress(addrField.stringValue) {
            baseAddress = a & ~UInt32(0x0F)
            pokeAddrField.stringValue = String(format: "%06X", a)
            hexTable.reloadData()
        }
    }

    @objc private func onPrevPage() {
        baseAddress = (baseAddress >= 256) ? (baseAddress - 256) : 0
        addrField.stringValue = String(format: "%06X", baseAddress)
        hexTable.reloadData()
    }

    @objc private func onNextPage() {
        baseAddress = baseAddress &+ 256
        addrField.stringValue = String(format: "%06X", baseAddress)
        hexTable.reloadData()
    }

    @objc private func onRegionSelected() {
        let tag = regionPopup.selectedTag()
        baseAddress = UInt32(bitPattern: Int32(tag))
        addrField.stringValue = String(format: "%06X", baseAddress)
        pokeAddrField.stringValue = String(format: "%06X", baseAddress)
        hexTable.reloadData()
    }

    @objc private func onHexRowDoubleClicked() {
        let row = hexTable.clickedRow >= 0 ? hexTable.clickedRow : hexTable.selectedRow
        guard row >= 0 else { return }
        let rowAddr = baseAddress &+ UInt32(row) * bytesPerRow
        pokeAddrField.stringValue = String(format: "%06X", rowAddr)
        window?.makeFirstResponder(pokeValField)
    }

    @objc private func onPokeClicked() {
        guard let addr = parseHexAddress(pokeAddrField.stringValue) else { return }
        let mode = pokeWidthPopup.indexOfSelectedItem
        if mode == 3 {
            // Sequence of space-separated hex bytes
            let tokens = pokeValField.stringValue
                .replacingOccurrences(of: ",", with: " ")
                .split(separator: " ")
            var offset: UInt32 = 0
            for tok in tokens {
                let clean = tok.hasPrefix("$") ? String(tok.dropFirst()) : String(tok)
                if let b = UInt8(clean, radix: 16) {
                    amiga_poke_ram8(machine, addr &+ offset, b)
                    offset &+= 1
                }
            }
        } else {
            let width = (mode == 2) ? 4 : ((mode == 1) ? 2 : 1)
            guard let val = parseNumber(pokeValField.stringValue) else { return }
            writeValue(val, at: addr, width: width)
        }
        hexTable.reloadData()
        trainerTable.reloadData()
    }

    @objc private func onTrainerStart() {
        guard let target = parseNumber(trainerValField.stringValue) else { return }
        let mode = trainerWidthPopup.indexOfSelectedItem
        trainerWidth = (mode == 2) ? 4 : ((mode == 1) ? 2 : 1)
        let step = UInt32(trainerWidth == 1 ? 1 : 2)

        var matches: [UInt32] = []
        for (start, end) in activeRamRanges() {
            var a = start
            while a + UInt32(trainerWidth) <= end {
                if readValue(at: a, width: trainerWidth) == target {
                    matches.append(a)
                    if matches.count >= 50000 { break }
                }
                a += step
            }
        }
        trainerCandidates = matches
        trainerCountLabel.stringValue = L10n.t(
            "Znaleziono adresów: \(matches.count) (zmień stan w grze i kliknij Filtruj T)",
            "Found addresses: \(matches.count) (change state in game & click Filter T)"
        )
        trainerTable.reloadData()
    }

    @objc private func onTrainerFilter() {
        guard let target = parseNumber(trainerValField.stringValue) else { return }
        if trainerCandidates.isEmpty {
            onTrainerStart()
            return
        }
        trainerCandidates = trainerCandidates.filter { readValue(at: $0, width: trainerWidth) == target }
        trainerCountLabel.stringValue = L10n.t(
            "Pozostało adresów: \(trainerCandidates.count) (kliknij adres, aby edytować)",
            "Remaining addresses: \(trainerCandidates.count) (click address to edit)"
        )
        trainerTable.reloadData()
    }

    @objc private func onTrainerRowClicked() {
        let row = trainerTable.clickedRow >= 0 ? trainerTable.clickedRow : trainerTable.selectedRow
        guard row >= 0 && row < trainerCandidates.count else { return }
        let addr = trainerCandidates[row]
        baseAddress = addr & ~UInt32(0x0F)
        addrField.stringValue = String(format: "%06X", baseAddress)
        pokeAddrField.stringValue = String(format: "%06X", addr)
        pokeWidthPopup.selectItem(at: trainerWidth == 4 ? 2 : (trainerWidth == 2 ? 1 : 0))
        hexTable.reloadData()
        window?.makeFirstResponder(pokeValField)
    }

    @objc private func onSaveArfClicked() {
        onSaveSnapshot()
    }

    @objc private func onResumeClicked() {
        window?.close()
        onResumeEmulation()
    }

    // MARK: - NSTableViewDataSource & Delegate

    func numberOfRows(in tableView: NSTableView) -> Int {
        if tableView === hexTable {
            return visibleRows
        }
        return min(trainerCandidates.count, 500)
    }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        let id = tableColumn?.identifier.rawValue ?? ""
        let lbl = NSTextField(labelWithString: "")
        lbl.font = .monospacedSystemFont(ofSize: 11.5, weight: .regular)

        if tableView === hexTable {
            let rowAddr = baseAddress &+ UInt32(row) * bytesPerRow
            if id == "addr" {
                lbl.stringValue = String(format: "$%06X", rowAddr)
                lbl.textColor = MonokaiTheme.orange
            } else if id == "hex" {
                var hexParts: [String] = []
                for i in 0..<16 {
                    let b = amiga_peek_ram8(machine, rowAddr &+ UInt32(i))
                    hexParts.append(String(format: "%02X", b))
                    if i == 7 { hexParts.append("") }
                }
                lbl.stringValue = hexParts.joined(separator: " ")
                lbl.textColor = MonokaiTheme.text
            } else {
                var ascii = ""
                for i in 0..<16 {
                    let b = amiga_peek_ram8(machine, rowAddr &+ UInt32(i))
                    if b >= 32 && b < 127 {
                        ascii.append(Character(UnicodeScalar(b)))
                    } else {
                        ascii.append(".")
                    }
                }
                lbl.stringValue = ascii
                lbl.textColor = MonokaiTheme.cyan
            }
            return lbl
        } else {
            guard row < trainerCandidates.count else { return lbl }
            let addr = trainerCandidates[row]
            if id == "tr_addr" {
                lbl.stringValue = String(format: "$%06X", addr)
                lbl.textColor = MonokaiTheme.pink
            } else {
                let v = readValue(at: addr, width: trainerWidth)
                lbl.stringValue = String(format: "%u ($%X)", v, v)
                lbl.textColor = MonokaiTheme.green
            }
            return lbl
        }
    }

    func tableViewSelectionDidChange(_ notification: Notification) {
        guard let tv = notification.object as? NSTableView, tv === hexTable else { return }
        let row = hexTable.selectedRow
        guard row >= 0 else { return }
        let rowAddr = baseAddress &+ UInt32(row) * bytesPerRow
        pokeAddrField.stringValue = String(format: "%06X", rowAddr)
    }
}
