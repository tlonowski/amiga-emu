import AppKit

/// Captures parallel (`PAR:` / `PRT:`) and serial (`SER:`) output streams from the emulated Amiga,
/// interprets Amiga ANSI/ISO and Epson ESC/P control codes (styles + dot-matrix graphics bands),
/// and dispatches print jobs to the native macOS printing system (`NSPrintOperation`).
final class AmigaPrinterSpooler {
    private var parallelBuffer = Data()
    private var serialBuffer = Data()
    private var lastSubmittedData = Data()
    private var lastByteTime: TimeInterval = 0
    private var isPrintingModal = false

    /// Idle delay (in seconds) after the last parallel-port byte before auto-opening the macOS Print dialog.
    private let autoPrintIdleSeconds: TimeInterval = 1.8

    var autoPrintEnabled: Bool = (UserDefaults.standard.object(forKey: "amigaAutoPrintEnabled") as? Bool) ?? true {
        didSet {
            UserDefaults.standard.set(autoPrintEnabled, forKey: "amigaAutoPrintEnabled")
        }
    }

    var hasPendingData: Bool {
        return !parallelBuffer.isEmpty || !serialBuffer.isEmpty || !lastSubmittedData.isEmpty
    }

    var pendingByteCount: Int {
        return parallelBuffer.count + serialBuffer.count
    }

    func appendParallelBytes(_ bytes: [UInt8]) {
        guard !bytes.isEmpty else { return }
        parallelBuffer.append(contentsOf: bytes)
        lastByteTime = ProcessInfo.processInfo.systemUptime
    }

    func appendSerialBytes(_ bytes: [UInt8]) {
        guard !bytes.isEmpty else { return }
        serialBuffer.append(contentsOf: bytes)
    }

    func clearBuffers() {
        parallelBuffer.removeAll()
        serialBuffer.removeAll()
    }

    /// Called periodically (e.g. 30 Hz from AppDelegate) to auto-trigger printing once a parallel job finishes.
    func pollAutoPrint(in window: NSWindow?) {
        guard autoPrintEnabled, !isPrintingModal, !parallelBuffer.isEmpty else { return }
        let elapsed = ProcessInfo.processInfo.systemUptime - lastByteTime
        if elapsed >= autoPrintIdleSeconds {
            let jobData = parallelBuffer
            parallelBuffer.removeAll()
            lastSubmittedData = jobData
            presentPrintOperation(for: jobData, sourceLabel: "PAR: / PRT:", in: window)
        }
    }

    /// Manually triggers printing of the current parallel/serial buffer (or last completed job).
    func printCurrentJob(in window: NSWindow?) {
        guard !isPrintingModal else { return }
        let jobData: Data
        let sourceLabel: String
        if !parallelBuffer.isEmpty {
            jobData = parallelBuffer
            parallelBuffer.removeAll()
            lastSubmittedData = jobData
            sourceLabel = "PAR: / PRT:"
        } else if !serialBuffer.isEmpty {
            jobData = serialBuffer
            serialBuffer.removeAll()
            lastSubmittedData = jobData
            sourceLabel = "SER:"
        } else if !lastSubmittedData.isEmpty {
            jobData = lastSubmittedData
            sourceLabel = "PAR: / PRT:"
        } else {
            let alert = NSAlert()
            alert.messageText = L10n.t("Bufor drukarki Amigi jest pusty", "Amiga Printer Buffer Is Empty")
            alert.informativeText = L10n.t(
                "Wyślij wydruk z AmigaOS na port równoległy (PRT: lub PAR:) albo szeregowy (SER:), np. w Shellu:\nEcho \"Test wydruku z Amigi\" >PAR:",
                "Send a printout from AmigaOS to the parallel (PRT: or PAR:) or serial (SER:) port, e.g. in Shell:\nEcho \"Amiga print test\" >PAR:"
            )
            alert.addButton(withTitle: "OK")
            alert.runModal()
            return
        }
        presentPrintOperation(for: jobData, sourceLabel: sourceLabel, in: window)
    }

    // MARK: - ESC/P & Amiga ISO/ANSI Parser

    static func parseAmigaPrintStream(_ data: Data) -> NSAttributedString {
        let result = NSMutableAttributedString()
        let bytes = [UInt8](data)
        let n = bytes.count

        var isBold = false
        var isItalic = false
        var isUnderline = false
        var col = 0
        var currentRun = ""

        func currentAttributes() -> [NSAttributedString.Key: Any] {
            var traits: NSFontTraitMask = []
            if isBold { traits.insert(.boldFontMask) }
            if isItalic { traits.insert(.italicFontMask) }
            let baseFont = NSFont.monospacedSystemFont(ofSize: 10.5, weight: isBold ? .bold : .regular)
            let font = traits.isEmpty
                ? baseFont
                : (NSFontManager.shared.convert(baseFont, toHaveTrait: traits))
            var attrs: [NSAttributedString.Key: Any] = [
                .font: font,
                .foregroundColor: NSColor.black
            ]
            if isUnderline {
                attrs[.underlineStyle] = NSUnderlineStyle.single.rawValue
            }
            return attrs
        }

        func flushRun() {
            guard !currentRun.isEmpty else { return }
            result.append(NSAttributedString(string: currentRun, attributes: currentAttributes()))
            currentRun.removeAll(keepingCapacity: true)
        }

        func appendGraphicsStrip(mode: UInt8, cols: Int, payload: ArraySlice<UInt8>) {
            guard cols > 0 else { return }
            let pins = (mode >= 32) ? 24 : 8
            let bytesPerCol = (pins == 24) ? 3 : 1
            let img = NSImage(size: NSSize(width: max(1, cols), height: pins))
            img.lockFocus()
            NSColor.white.setFill()
            NSBezierPath.fill(NSRect(x: 0, y: 0, width: cols, height: pins))
            NSColor.black.setFill()
            var offset = payload.startIndex
            for x in 0..<cols {
                for b in 0..<bytesPerCol {
                    guard offset < payload.endIndex else { break }
                    let byte = payload[offset]
                    offset += 1
                    for bit in 0..<8 {
                        if (byte & (0x80 >> bit)) != 0 {
                            let y = pins - 1 - (b * 8 + bit)
                            NSBezierPath.fill(NSRect(x: x, y: y, width: 1, height: 1))
                        }
                    }
                }
            }
            img.unlockFocus()
            let attachment = NSTextAttachment()
            attachment.image = img
            result.append(NSAttributedString(attachment: attachment))
            result.append(NSAttributedString(string: "\n", attributes: currentAttributes()))
            col = 0
        }

        var i = 0
        while i < n {
            let b = bytes[i]

            // ESC sequences (0x1B) or 8-bit CSI (0x9B)
            if b == 0x1B || b == 0x9B {
                flushRun()
                let isCsi8 = (b == 0x9B)
                i += 1
                if i >= n { break }

                let cmd = isCsi8 ? UInt8(ascii: "[") : bytes[i]
                if !isCsi8 { i += 1 }

                if cmd == UInt8(ascii: "[") {
                    // ANSI / Amiga printer.device CSI sequence: ESC [ params finalByte
                    var paramStr = ""
                    while i < n && bytes[i] >= 0x20 && bytes[i] <= 0x3F {
                        paramStr.append(Character(UnicodeScalar(bytes[i])))
                        i += 1
                    }
                    if i < n {
                        let finalByte = bytes[i]
                        i += 1
                        if finalByte == UInt8(ascii: "m") {
                            let parts = paramStr.split(separator: ";").compactMap { Int($0) }
                            for code in (parts.isEmpty ? [0] : parts) {
                                switch code {
                                case 0:
                                    isBold = false
                                    isItalic = false
                                    isUnderline = false
                                case 1: isBold = true
                                case 3: isItalic = true
                                case 4: isUnderline = true
                                case 22: isBold = false
                                case 23: isItalic = false
                                case 24: isUnderline = false
                                default: break
                                }
                            }
                        }
                    }
                    continue
                }

                switch cmd {
                case UInt8(ascii: "@"), UInt8(ascii: "c"):
                    isBold = false
                    isItalic = false
                    isUnderline = false
                case UInt8(ascii: "E"), UInt8(ascii: "G"):
                    isBold = true
                case UInt8(ascii: "F"), UInt8(ascii: "H"):
                    isBold = false
                case UInt8(ascii: "4"):
                    isItalic = true
                case UInt8(ascii: "5"):
                    isItalic = false
                case UInt8(ascii: "-"):
                    if i < n {
                        let arg = bytes[i]
                        isUnderline = (arg == 1 || arg == UInt8(ascii: "1"))
                        i += 1
                    }
                case UInt8(ascii: "!"):
                    if i < n {
                        let mask = bytes[i]
                        isBold = (mask & 0x08) != 0
                        isItalic = (mask & 0x40) != 0
                        isUnderline = (mask & 0x80) != 0
                        i += 1
                    }
                case UInt8(ascii: "K"), UInt8(ascii: "L"), UInt8(ascii: "Y"), UInt8(ascii: "Z"):
                    // 8-pin ESC/P bit image: nL nH <cols bytes>
                    if i + 1 < n {
                        let cols = Int(bytes[i]) + Int(bytes[i + 1]) * 256
                        i += 2
                        let end = min(n, i + cols)
                        appendGraphicsStrip(mode: 0, cols: cols, payload: bytes[i..<end])
                        i = end
                    }
                case UInt8(ascii: "*"):
                    // ESC * m nL nH <data>
                    if i + 2 < n {
                        let m = bytes[i]
                        let cols = Int(bytes[i + 1]) + Int(bytes[i + 2]) * 256
                        i += 3
                        let bytesPerCol = (m >= 32) ? 3 : 1
                        let totalBytes = cols * bytesPerCol
                        let end = min(n, i + totalBytes)
                        appendGraphicsStrip(mode: m, cols: cols, payload: bytes[i..<end])
                        i = end
                    }
                case UInt8(ascii: "3"), UInt8(ascii: "A"), UInt8(ascii: "J"), UInt8(ascii: "d"),
                     UInt8(ascii: "l"), UInt8(ascii: "Q"), UInt8(ascii: "N"), UInt8(ascii: "C"),
                     UInt8(ascii: "W"), UInt8(ascii: "x"), UInt8(ascii: "k"), UInt8(ascii: "S"),
                     UInt8(ascii: "a"), UInt8(ascii: "R"), UInt8(ascii: "t"):
                    // 1-byte argument ESC/P commands
                    if i < n {
                        if cmd == UInt8(ascii: "C") && bytes[i] == 0 && i + 1 < n {
                            i += 2
                        } else {
                            i += 1
                        }
                    }
                default:
                    break
                }
                continue
            }

            switch b {
            case 0x0A: // LF
                currentRun.append("\n")
                col = 0
            case 0x0D: // CR
                if i + 1 < n && bytes[i + 1] == 0x0A {
                    // Handled by following LF
                } else {
                    currentRun.append("\n")
                    col = 0
                }
            case 0x09: // TAB
                let spaces = 8 - (col % 8)
                currentRun.append(String(repeating: " ", count: spaces))
                col += spaces
            case 0x0C: // Form Feed (FF)
                currentRun.append("\u{000C}")
                col = 0
            case 0x08: // Backspace
                if !currentRun.isEmpty && currentRun.last != "\n" {
                    currentRun.removeLast()
                    col = max(0, col - 1)
                }
            case 0x00...0x1F, 0x7F:
                // Ignore non-printable control characters (SI, DC2, SO, DC4, BELL, etc.)
                break
            default:
                // ISO-8859-1 (Latin-1 / AmigaOS default codepage) maps directly to Unicode scalar U+0020..U+00FF
                currentRun.append(Character(UnicodeScalar(b)))
                col += 1
            }
            i += 1
        }

        flushRun()
        if result.length == 0 {
            result.append(NSAttributedString(string: " ", attributes: currentAttributes()))
        }
        return result
    }

    // MARK: - Native macOS Print Dialog (NSPrintOperation)

    private func presentPrintOperation(for data: Data, sourceLabel: String, in window: NSWindow?) {
        let attributed = Self.parseAmigaPrintStream(data)
        let printInfo = (NSPrintInfo.shared.copy() as? NSPrintInfo) ?? NSPrintInfo()
        printInfo.topMargin = 36
        printInfo.bottomMargin = 36
        printInfo.leftMargin = 36
        printInfo.rightMargin = 36
        printInfo.isHorizontallyCentered = false
        printInfo.isVerticallyCentered = false

        let pageSize = printInfo.paperSize
        let printableWidth = max(200, pageSize.width - printInfo.leftMargin - printInfo.rightMargin)
        let printableHeight = max(200, pageSize.height - printInfo.topMargin - printInfo.bottomMargin)

        let textView = NSTextView(frame: NSRect(x: 0, y: 0, width: printableWidth, height: printableHeight))
        textView.isEditable = false
        textView.isSelectable = false
        textView.drawsBackground = true
        textView.backgroundColor = .white
        textView.textContainerInset = .zero
        textView.textContainer?.lineFragmentPadding = 0
        textView.textStorage?.setAttributedString(attributed)
        textView.layoutManager?.ensureLayout(for: textView.textContainer!)

        let op = NSPrintOperation(view: textView, printInfo: printInfo)
        op.jobTitle = "AmigaEmu — \(sourceLabel)"
        op.showsPrintPanel = true
        op.showsProgressPanel = true

        isPrintingModal = true
        _ = op.run()
        isPrintingModal = false
    }
}
