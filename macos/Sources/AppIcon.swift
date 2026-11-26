import AppKit

enum AmigaAppIcon {
    static func makeIcon(size: CGFloat = 512) -> NSImage {
        let img = NSImage(size: NSSize(width: size, height: size))
        img.lockFocus()
        defer { img.unlockFocus() }

        guard let ctx = NSGraphicsContext.current?.cgContext else { return img }
        ctx.setAllowsAntialiasing(true)
        ctx.setShouldAntialias(true)

        // macOS squircle-like rounded card with subtle shadow
        let inset = size * 0.09
        let cardRect = NSRect(x: inset, y: inset, width: size - inset * 2, height: size - inset * 2)
        let corner = cardRect.width * 0.225
        let cardPath = NSBezierPath(roundedRect: cardRect, xRadius: corner, yRadius: corner)

        ctx.saveGState()
        let shadow = NSShadow()
        shadow.shadowColor = NSColor(white: 0.0, alpha: 0.38)
        shadow.shadowOffset = NSSize(width: 0, height: -size * 0.025)
        shadow.shadowBlurRadius = size * 0.045
        shadow.set()
        NSColor(calibratedRed: 0.90, green: 0.91, blue: 0.94, alpha: 1.0).setFill()
        cardPath.fill()
        ctx.restoreGState()

        // Classic Amiga 1200 platinum/ivory metallic gradient background
        ctx.saveGState()
        cardPath.addClip()
        let bgGrad = NSGradient(colors: [
            NSColor(calibratedRed: 0.96, green: 0.965, blue: 0.98, alpha: 1.0),
            NSColor(calibratedRed: 0.85, green: 0.87, blue: 0.90, alpha: 1.0)
        ])
        bgGrad?.draw(in: cardRect, angle: -90)

        // Subtle top highlight sheen
        let sheenRect = NSRect(x: cardRect.minX, y: cardRect.midY, width: cardRect.width, height: cardRect.height * 0.5)
        let sheenGrad = NSGradient(colors: [
            NSColor(white: 1.0, alpha: 0.38),
            NSColor(white: 1.0, alpha: 0.0)
        ])
        sheenGrad?.draw(in: sheenRect, angle: -90)
        ctx.restoreGState()

        // Subtle inner rim
        NSColor(white: 1.0, alpha: 0.65).setStroke()
        cardPath.lineWidth = size * 0.008
        cardPath.stroke()

        // "Ami the Emu" logo:
        // Stylized black '[' bracket with angled right tips + 2 inner LED bars in Amiga LED colors (forming an 'E')
        let w = cardRect.width
        let cx = cardRect.midX
        let cy = cardRect.midY

        let bracketLeft = cx - w * 0.275
        let bracketInnerLeft = cx - w * 0.145
        let bracketTopOuter = cy + w * 0.295
        let bracketBottomOuter = cy - w * 0.295
        let bracketArmThick = w * 0.118
        let bracketTopInner = bracketTopOuter - bracketArmThick
        let bracketBottomInner = bracketBottomOuter + bracketArmThick

        let tipOuterRight = cx + w * 0.225
        let tipInnerRight = cx + w * 0.125
        let outerR = w * 0.095
        let innerR = w * 0.038

        let bracketPath = NSBezierPath()
        // Start at top-right outer tip and trace counter-clockwise around the '[' shape
        bracketPath.move(to: NSPoint(x: tipOuterRight, y: bracketTopOuter))
        bracketPath.line(to: NSPoint(x: bracketLeft + outerR, y: bracketTopOuter))
        bracketPath.appendArc(
            withCenter: NSPoint(x: bracketLeft + outerR, y: bracketTopOuter - outerR),
            radius: outerR,
            startAngle: 90,
            endAngle: 180,
            clockwise: false
        )
        bracketPath.line(to: NSPoint(x: bracketLeft, y: bracketBottomOuter + outerR))
        bracketPath.appendArc(
            withCenter: NSPoint(x: bracketLeft + outerR, y: bracketBottomOuter + outerR),
            radius: outerR,
            startAngle: 180,
            endAngle: 270,
            clockwise: false
        )
        bracketPath.line(to: NSPoint(x: tipOuterRight, y: bracketBottomOuter))
        // Angled bottom tip cut inward (Commodore/Amiga wing angle)
        bracketPath.line(to: NSPoint(x: tipInnerRight, y: bracketBottomInner))
        bracketPath.line(to: NSPoint(x: bracketInnerLeft + innerR, y: bracketBottomInner))
        bracketPath.appendArc(
            withCenter: NSPoint(x: bracketInnerLeft + innerR, y: bracketBottomInner + innerR),
            radius: innerR,
            startAngle: 270,
            endAngle: 180,
            clockwise: true
        )
        bracketPath.line(to: NSPoint(x: bracketInnerLeft, y: bracketTopInner - innerR))
        bracketPath.appendArc(
            withCenter: NSPoint(x: bracketInnerLeft + innerR, y: bracketTopInner - innerR),
            radius: innerR,
            startAngle: 180,
            endAngle: 90,
            clockwise: true
        )
        bracketPath.line(to: NSPoint(x: tipInnerRight, y: bracketTopInner))
        bracketPath.close()

        ctx.saveGState()
        let bracketShadow = NSShadow()
        bracketShadow.shadowColor = NSColor(white: 0.0, alpha: 0.28)
        bracketShadow.shadowOffset = NSSize(width: 0, height: -size * 0.012)
        bracketShadow.shadowBlurRadius = size * 0.022
        bracketShadow.set()

        let bracketGrad = NSGradient(colors: [
            NSColor(calibratedRed: 0.16, green: 0.17, blue: 0.19, alpha: 1.0),
            NSColor(calibratedRed: 0.06, green: 0.065, blue: 0.08, alpha: 1.0)
        ])
        bracketGrad?.draw(in: bracketPath, angle: -90)
        ctx.restoreGState()

        // Two horizontal Amiga LED slots inside the '[' bracket
        let ledLeft = bracketInnerLeft + w * 0.065
        let ledWidth = w * 0.275
        let ledHeight = w * 0.072
        let ledGap = w * 0.052
        let ledCorner = ledHeight * 0.22

        let topLedRect = NSRect(
            x: ledLeft,
            y: cy + ledGap * 0.5,
            width: ledWidth,
            height: ledHeight
        )
        let bottomLedRect = NSRect(
            x: ledLeft,
            y: cy - ledGap * 0.5 - ledHeight,
            width: ledWidth,
            height: ledHeight
        )

        func drawAmigaLed(rect: NSRect, baseColor: NSColor, coreColor: NSColor, glowColor: NSColor) {
            let bezelRect = rect.insetBy(dx: -size * 0.006, dy: -size * 0.006)
            let bezelPath = NSBezierPath(roundedRect: bezelRect, xRadius: ledCorner * 1.25, yRadius: ledCorner * 1.25)
            NSColor(calibratedWhite: 0.10, alpha: 0.88).setFill()
            bezelPath.fill()

            let ledPath = NSBezierPath(roundedRect: rect, xRadius: ledCorner, yRadius: ledCorner)
            ctx.saveGState()
            let glow = NSShadow()
            glow.shadowColor = glowColor.withAlphaComponent(0.75)
            glow.shadowOffset = .zero
            glow.shadowBlurRadius = size * 0.024
            glow.set()
            baseColor.setFill()
            ledPath.fill()
            ctx.restoreGState()

            let grad = NSGradient(colors: [coreColor, baseColor])
            grad?.draw(in: ledPath, angle: -90)

            let coreRect = rect.insetBy(dx: rect.width * 0.08, dy: rect.height * 0.24)
            let corePath = NSBezierPath(roundedRect: coreRect, xRadius: coreRect.height * 0.35, yRadius: coreRect.height * 0.35)
            coreColor.withAlphaComponent(0.85).setFill()
            corePath.fill()
        }

        // Top LED: Amiga POWER bright green
        drawAmigaLed(
            rect: topLedRect,
            baseColor: NSColor(calibratedRed: 0.12, green: 0.86, blue: 0.26, alpha: 1.0),
            coreColor: NSColor(calibratedRed: 0.62, green: 1.00, blue: 0.68, alpha: 1.0),
            glowColor: NSColor(calibratedRed: 0.18, green: 0.95, blue: 0.32, alpha: 1.0)
        )

        // Bottom LED: Amiga DRIVE active amber/orange
        drawAmigaLed(
            rect: bottomLedRect,
            baseColor: NSColor(calibratedRed: 0.98, green: 0.58, blue: 0.06, alpha: 1.0),
            coreColor: NSColor(calibratedRed: 1.00, green: 0.88, blue: 0.42, alpha: 1.0),
            glowColor: NSColor(calibratedRed: 1.00, green: 0.64, blue: 0.10, alpha: 1.0)
        )

        return img
    }
}
