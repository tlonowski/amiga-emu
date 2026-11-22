import AppKit

protocol LeftSidebarDelegate: AnyObject {
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
    func sidebarDidChangeFastRam(mb: Int, for profile: AmigaProfileConfig)
    func sidebarDidRequestApplyAndReset(profile: AmigaProfileConfig)
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

    func configure(profiles: [AmigaModelType: AmigaProfileConfig], selected: AmigaModelType) {
        self.profiles = profiles
        self.selectedModel = selected
        reloadUI()
    }

    func updateSelected(_ model: AmigaModelType) {
        self.selectedModel = model
        reloadUI()
    }

    func reloadUI() {
        guard isViewLoaded else { return }
        titleLabel?.stringValue = L10n.t("MASZYNY AMIGA", "AMIGA MACHINES")
        hwHeaderLabel?.stringValue = L10n.t("KONFIGURACJA SPRZĘTU", "HARDWARE CONFIGURATION")
        applyBtn?.title = L10n.t("Restartuj maszynę", "Restart Machine")
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

        root.addSubview(titleLabel)
        root.addSubview(tableScroll)
        root.addSubview(divider)
        root.addSubview(hwHeader)
        root.addSubview(scrollView)
        root.addSubview(applyBtn)

        NSLayoutConstraint.activate([
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

            hwHeader.topAnchor.constraint(equalTo: divider.bottomAnchor, constant: 10),
            hwHeader.leadingAnchor.constraint(equalTo: root.leadingAnchor, constant: 14),
            hwHeader.trailingAnchor.constraint(equalTo: root.trailingAnchor, constant: -14),

            scrollView.topAnchor.constraint(equalTo: hwHeader.bottomAnchor, constant: 4),
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
    }

    private func makeSectionHeader(_ text: String) -> NSTextField {
        let lbl = NSTextField(labelWithString: text)
        lbl.font = .systemFont(ofSize: 11, weight: .semibold)
        lbl.textColor = .secondaryLabelColor
        lbl.translatesAutoresizingMaskIntoConstraints = false
        return lbl
    }

    private func rebuildHardwareSection() {
        for v in hardwareStack.arrangedSubviews {
            hardwareStack.removeArrangedSubview(v)
            v.removeFromSuperview()
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

        // 4. Fast RAM
        hardwareStack.addArrangedSubview(makeSubTitle("Fast RAM"))
        let ramSeg = NSSegmentedControl(labels: [L10n.t("Brak", "None"), "2 MB", "4 MB", "8 MB"], trackingMode: .selectOne, target: self, action: #selector(onFastRamChanged(_:)))
        ramSeg.controlSize = .small
        let ramValues = [0, 2, 4, 8]
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
        img.image = NSImage(systemSymbolName: "opticaldiscdrive", accessibilityDescription: nil)
        img.contentTintColor = unit == 0 ? MonokaiTheme.orange : MonokaiTheme.muted

        let title = NSTextField(labelWithString: "DF\(unit): \(diskName)")
        title.font = .systemFont(ofSize: 11)
        title.textColor = MonokaiTheme.text
        title.lineBreakMode = .byTruncatingMiddle
        title.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)

        top.addArrangedSubview(img)
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
        img.image = NSImage(systemSymbolName: "internaldrive", accessibilityDescription: nil)

        let lbl = NSTextField(labelWithString: "DH\(unit): \(name)")
        lbl.font = .systemFont(ofSize: 11)
        lbl.lineBreakMode = .byTruncatingMiddle
        lbl.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)

        let delBtn = NSButton(title: L10n.t("Usuń", "Remove"), target: self, action: #selector(onRemoveHardDrive(_:)))
        delBtn.tag = unit
        delBtn.bezelStyle = .rounded
        delBtn.controlSize = .mini

        row.addArrangedSubview(img)
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

    @objc private func onFastRamChanged(_ sender: NSSegmentedControl) {
        let ramValues = [0, 2, 4, 8]
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

        let sub = NSTextField(labelWithString: model.subtitle)
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
