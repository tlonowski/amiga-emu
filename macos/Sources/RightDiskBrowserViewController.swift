import AppKit

protocol RightDiskBrowserDelegate: AnyObject {
    func diskBrowserDidSelectDisk(_ path: String, unit: Int, resetMachine: Bool)
    func diskBrowserDidSelectDiskSet(_ paths: [String], resetMachine: Bool)
    func diskBrowserDidRequestBlankDisk(unit: Int)
}

private final class DiskTreeNode {
    let name: String
    let path: String?
    var children: [DiskTreeNode]

    var isFolder: Bool { path == nil }

    /// If this folder directly contains 1..4 .adf images (and no subfolders), returns their sorted paths.
    var autoMountDiskPaths: [String]? {
        guard isFolder, !children.isEmpty, children.count <= 4 else { return nil }
        let paths = children.compactMap { $0.path }
        guard paths.count == children.count else { return nil }
        return paths
    }

    init(name: String, path: String? = nil, children: [DiskTreeNode] = []) {
        self.name = name
        self.path = path
        self.children = children
    }
}

final class RightDiskBrowserViewController: NSViewController, NSOutlineViewDataSource, NSOutlineViewDelegate, NSSearchFieldDelegate {
    weak var delegate: RightDiskBrowserDelegate?

    private var allItems: [DiskImageItem] = []
    private var rootNodes: [DiskTreeNode] = []

    private let searchField = NSSearchField()
    private let outlineView = NSOutlineView()
    private let unitPopup = NSPopUpButton()
    private var headerLabel: NSTextField!
    private var blankBtn: NSButton!
    private var refreshBtn: NSButton!
    private var insertBtn: NSButton!
    private var bootBtn: NSButton!

    override func loadView() {
        let root = MonokaiTheme.makePanelView()
        self.view = root

        headerLabel = NSTextField(labelWithString: L10n.t("OBRAZY DYSKÓW (.ADF)", "DISK IMAGES (.ADF)"))
        let header = headerLabel!
        header.font = .systemFont(ofSize: 11, weight: .semibold)
        header.textColor = MonokaiTheme.yellow
        header.translatesAutoresizingMaskIntoConstraints = false

        blankBtn = NSButton(title: L10n.t("+ Pusty", "+ Blank"), target: self, action: #selector(onBlankClicked))
        blankBtn.bezelStyle = .inline
        blankBtn.controlSize = .small
        blankBtn.toolTip = L10n.t("Utwórz nowy pusty dysk .adf (880 KB OFS)", "Create a new blank .adf disk (880 KB OFS)")
        blankBtn.translatesAutoresizingMaskIntoConstraints = false

        refreshBtn = NSButton(image: NSImage(systemSymbolName: "arrow.clockwise", accessibilityDescription: L10n.t("Odśwież", "Refresh"))!, target: self, action: #selector(reloadLibrary))
        refreshBtn.bezelStyle = .inline
        refreshBtn.controlSize = .small
        refreshBtn.translatesAutoresizingMaskIntoConstraints = false

        searchField.placeholderString = L10n.t("Szukaj obrazu...", "Search disk image...")
        searchField.delegate = self
        searchField.translatesAutoresizingMaskIntoConstraints = false

        let col = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("disk"))
        col.width = 240
        col.resizingMask = .autoresizingMask
        outlineView.addTableColumn(col)
        outlineView.outlineTableColumn = col
        outlineView.columnAutoresizingStyle = .uniformColumnAutoresizingStyle
        outlineView.headerView = nil
        outlineView.backgroundColor = .clear
        outlineView.rowHeight = 24
        outlineView.indentationPerLevel = 14
        outlineView.dataSource = self
        outlineView.delegate = self
        outlineView.target = self
        outlineView.doubleAction = #selector(onDoubleClick)

        let scroll = NSScrollView()
        scroll.drawsBackground = false
        scroll.hasVerticalScroller = true
        scroll.documentView = outlineView
        scroll.translatesAutoresizingMaskIntoConstraints = false

        unitPopup.addItem(withTitle: "DF0:")
        unitPopup.lastItem?.tag = 0
        unitPopup.controlSize = .small
        unitPopup.translatesAutoresizingMaskIntoConstraints = false

        insertBtn = NSButton(title: L10n.t("Włóż", "Insert"), target: self, action: #selector(onInsertClicked))
        insertBtn.bezelStyle = .rounded
        insertBtn.controlSize = .regular
        insertBtn.translatesAutoresizingMaskIntoConstraints = false

        bootBtn = NSButton(title: L10n.t("Bootuj", "Boot"), target: self, action: #selector(onBootClicked))
        bootBtn.bezelStyle = .rounded
        bootBtn.controlSize = .regular
        bootBtn.toolTip = L10n.t("Włóż wybrany obraz do DF0: i zrestartuj Amigę", "Insert selected image into DF0: and reset Amiga")
        bootBtn.translatesAutoresizingMaskIntoConstraints = false

        root.addSubview(header)
        root.addSubview(blankBtn)
        root.addSubview(refreshBtn)
        root.addSubview(searchField)
        root.addSubview(scroll)
        root.addSubview(unitPopup)
        root.addSubview(insertBtn)
        root.addSubview(bootBtn)

        NSLayoutConstraint.activate([
            header.topAnchor.constraint(equalTo: root.safeAreaLayoutGuide.topAnchor, constant: 10),
            header.leadingAnchor.constraint(equalTo: root.leadingAnchor, constant: 12),

            refreshBtn.centerYAnchor.constraint(equalTo: header.centerYAnchor),
            refreshBtn.trailingAnchor.constraint(equalTo: root.trailingAnchor, constant: -10),

            blankBtn.centerYAnchor.constraint(equalTo: header.centerYAnchor),
            blankBtn.trailingAnchor.constraint(equalTo: refreshBtn.leadingAnchor, constant: -6),

            searchField.topAnchor.constraint(equalTo: header.bottomAnchor, constant: 8),
            searchField.leadingAnchor.constraint(equalTo: root.leadingAnchor, constant: 10),
            searchField.trailingAnchor.constraint(equalTo: root.trailingAnchor, constant: -10),

            scroll.topAnchor.constraint(equalTo: searchField.bottomAnchor, constant: 8),
            scroll.leadingAnchor.constraint(equalTo: root.leadingAnchor),
            scroll.trailingAnchor.constraint(equalTo: root.trailingAnchor),
            scroll.bottomAnchor.constraint(equalTo: insertBtn.topAnchor, constant: -10),

            unitPopup.leadingAnchor.constraint(equalTo: root.leadingAnchor, constant: 10),
            unitPopup.centerYAnchor.constraint(equalTo: insertBtn.centerYAnchor),
            unitPopup.widthAnchor.constraint(equalToConstant: 64),

            insertBtn.leadingAnchor.constraint(equalTo: unitPopup.trailingAnchor, constant: 6),
            insertBtn.bottomAnchor.constraint(equalTo: root.safeAreaLayoutGuide.bottomAnchor, constant: -12),

            bootBtn.leadingAnchor.constraint(equalTo: insertBtn.trailingAnchor, constant: 6),
            bootBtn.trailingAnchor.constraint(equalTo: root.trailingAnchor, constant: -10),
            bootBtn.centerYAnchor.constraint(equalTo: insertBtn.centerYAnchor),
            bootBtn.widthAnchor.constraint(equalTo: insertBtn.widthAnchor)
        ])

        reloadLibrary()
    }

    func updateAvailableUnits(_ units: [Int]) {
        let prev = unitPopup.selectedTag()
        unitPopup.removeAllItems()
        for u in units {
            unitPopup.addItem(withTitle: "DF\(u):")
            unitPopup.lastItem?.tag = u
        }
        if !unitPopup.selectItem(withTag: prev), unitPopup.numberOfItems > 0 {
            unitPopup.selectItem(at: 0)
        }
    }

    @objc func reloadLibrary() {
        allItems = DiskLibrary.scanImages()
        applyFilter()
    }

    func refreshLocalization() {
        guard isViewLoaded else { return }
        headerLabel?.stringValue = L10n.t("OBRAZY DYSKÓW (.ADF)", "DISK IMAGES (.ADF)")
        blankBtn?.title = L10n.t("+ Pusty", "+ Blank")
        blankBtn?.toolTip = L10n.t("Utwórz nowy pusty dysk .adf (880 KB OFS)", "Create a new blank .adf disk (880 KB OFS)")
        searchField.placeholderString = L10n.t("Szukaj obrazu...", "Search disk image...")
        insertBtn?.title = L10n.t("Włóż", "Insert")
        bootBtn?.title = L10n.t("Bootuj", "Boot")
        bootBtn?.toolTip = L10n.t("Włóż wybrany obraz do DF0: i zrestartuj Amigę", "Insert selected image into DF0: and reset Amiga")
    }

    private func buildTree(from items: [DiskImageItem]) -> [DiskTreeNode] {
        let root = DiskTreeNode(name: "")
        let prefix = DiskLibrary.rootPath.hasSuffix("/") ? DiskLibrary.rootPath : (DiskLibrary.rootPath + "/")

        for item in items {
            let rel = item.path.hasPrefix(prefix) ? String(item.path.dropFirst(prefix.count)) : item.name
            let parts = rel.split(separator: "/").map(String.init)
            var current = root
            if parts.count > 1 {
                for folderName in parts.dropLast() {
                    if let existing = current.children.first(where: { $0.isFolder && $0.name == folderName }) {
                        current = existing
                    } else {
                        let folder = DiskTreeNode(name: folderName)
                        current.children.append(folder)
                        current = folder
                    }
                }
            }
            current.children.append(DiskTreeNode(name: item.name, path: item.path))
        }

        func sortRecursive(_ node: DiskTreeNode) {
            node.children.sort {
                if $0.isFolder != $1.isFolder { return $0.isFolder }
                return $0.name.localizedStandardCompare($1.name) == .orderedAscending
            }
            for child in node.children where child.isFolder {
                sortRecursive(child)
            }
        }
        sortRecursive(root)
        return root.children
    }

    private func applyFilter() {
        let q = searchField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        let filtered: [DiskImageItem]
        if q.isEmpty {
            filtered = allItems
        } else {
            filtered = allItems.filter {
                $0.name.localizedCaseInsensitiveContains(q) || $0.path.localizedCaseInsensitiveContains(q)
            }
        }
        rootNodes = buildTree(from: filtered)
        outlineView.reloadData()
        if !q.isEmpty {
            outlineView.expandItem(nil, expandChildren: true)
        }
        outlineView.sizeLastColumnToFit()
    }

    func controlTextDidChange(_ obj: Notification) {
        applyFilter()
    }

    private func selectedNode() -> DiskTreeNode? {
        let row = outlineView.selectedRow
        guard row >= 0 else { return nil }
        return outlineView.item(atRow: row) as? DiskTreeNode
    }

    private func selectedDiskPath() -> String? {
        return selectedNode()?.path
    }

    @objc private func onDoubleClick() {
        let row = outlineView.clickedRow >= 0 ? outlineView.clickedRow : outlineView.selectedRow
        guard row >= 0, let node = outlineView.item(atRow: row) as? DiskTreeNode else { return }
        if node.isFolder {
            if let paths = node.autoMountDiskPaths {
                outlineView.expandItem(node)
                delegate?.diskBrowserDidSelectDiskSet(paths, resetMachine: false)
            } else if outlineView.isItemExpanded(node) {
                outlineView.collapseItem(node)
            } else {
                outlineView.expandItem(node)
            }
        } else if let path = node.path {
            delegate?.diskBrowserDidSelectDisk(path, unit: 0, resetMachine: false)
        }
    }

    @objc private func onInsertClicked() {
        guard let node = selectedNode() else { return }
        if let paths = node.autoMountDiskPaths {
            delegate?.diskBrowserDidSelectDiskSet(paths, resetMachine: false)
        } else if let path = node.path {
            let unit = max(0, unitPopup.selectedTag())
            delegate?.diskBrowserDidSelectDisk(path, unit: unit, resetMachine: false)
        }
    }

    @objc private func onBootClicked() {
        guard let node = selectedNode() else { return }
        if let paths = node.autoMountDiskPaths {
            delegate?.diskBrowserDidSelectDiskSet(paths, resetMachine: true)
        } else if let path = node.path {
            delegate?.diskBrowserDidSelectDisk(path, unit: 0, resetMachine: true)
        }
    }

    @objc private func onBlankClicked() {
        let unit = max(0, unitPopup.selectedTag())
        delegate?.diskBrowserDidRequestBlankDisk(unit: unit)
    }

    // MARK: - NSOutlineViewDataSource & Delegate

    func outlineViewSelectionDidChange(_ notification: Notification) {
        guard let node = selectedNode(), let paths = node.autoMountDiskPaths else { return }
        if !outlineView.isItemExpanded(node) {
            outlineView.expandItem(node)
        }
        delegate?.diskBrowserDidSelectDiskSet(paths, resetMachine: false)
    }

    func outlineView(_ outlineView: NSOutlineView, numberOfChildrenOfItem item: Any?) -> Int {
        if let node = item as? DiskTreeNode {
            return node.children.count
        }
        return rootNodes.count
    }

    func outlineView(_ outlineView: NSOutlineView, child index: Int, ofItem item: Any?) -> Any {
        if let node = item as? DiskTreeNode {
            return node.children[index]
        }
        return rootNodes[index]
    }

    func outlineView(_ outlineView: NSOutlineView, isItemExpandable item: Any) -> Bool {
        guard let node = item as? DiskTreeNode else { return false }
        return node.isFolder && !node.children.isEmpty
    }

    func outlineView(_ outlineView: NSOutlineView, viewFor tableColumn: NSTableColumn?, item: Any) -> NSView? {
        guard let node = item as? DiskTreeNode else { return nil }
        let cell = NSTableCellView()

        let icon = NSImageView()
        let autoDisks = node.autoMountDiskPaths
        let symbolName = node.isFolder ? (autoDisks != nil ? "folder.fill.badge.plus" : "folder.fill") : "opticaldiscdrive"
        icon.image = NSImage(systemSymbolName: symbolName, accessibilityDescription: nil)
        icon.contentTintColor = node.isFolder ? (autoDisks != nil ? MonokaiTheme.green : MonokaiTheme.yellow) : MonokaiTheme.cyan
        icon.translatesAutoresizingMaskIntoConstraints = false

        let labelText = (autoDisks != nil) ? "\(node.name) (\(autoDisks!.count) ADF)" : node.name
        let title = NSTextField(labelWithString: labelText)
        title.font = .systemFont(ofSize: 12, weight: node.isFolder ? .medium : .regular)
        title.textColor = node.isFolder ? MonokaiTheme.text : MonokaiTheme.text
        title.lineBreakMode = .byTruncatingTail
        title.translatesAutoresizingMaskIntoConstraints = false

        cell.addSubview(icon)
        cell.addSubview(title)

        NSLayoutConstraint.activate([
            icon.leadingAnchor.constraint(equalTo: cell.leadingAnchor, constant: 4),
            icon.centerYAnchor.constraint(equalTo: cell.centerYAnchor),
            icon.widthAnchor.constraint(equalToConstant: 15),
            icon.heightAnchor.constraint(equalToConstant: 15),

            title.leadingAnchor.constraint(equalTo: icon.trailingAnchor, constant: 6),
            title.trailingAnchor.constraint(equalTo: cell.trailingAnchor, constant: -6),
            title.centerYAnchor.constraint(equalTo: cell.centerYAnchor)
        ])
        return cell
    }
}
