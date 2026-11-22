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
            NSColor(white: 1.0, alpha: 0.35),
            NSColor(white: 1.0, alpha: 0.0)
        ])
        sheenGrad?.draw(in: sheenRect, angle: -90)
        ctx.restoreGState()

        // Subtle inner rim
        NSColor(white: 1.0, alpha: 0.65).setStroke()
        cardPath.lineWidth = size * 0.008
        cardPath.stroke()

        // Commodore "C=" logo geometry
        let R = cardRect.width * 0.315
        let r = R * 0.54
        let cutX = R * 0.36
        let cx = cardRect.midX - R * 0.11
        let cy = cardRect.midY

        let thetaOuter = acos(cutX / R)
        let thetaInner = acos(cutX / r)

        let cPath = NSBezierPath()
        let outerStartDeg = thetaOuter * 180.0 / .pi
        let outerEndDeg = 360.0 - outerStartDeg
        let innerStartDeg = thetaInner * 180.0 / .pi
        let innerEndDeg = 360.0 - innerStartDeg

        cPath.appendArc(
            withCenter: NSPoint(x: cx, y: cy),
            radius: R,
            startAngle: outerStartDeg,
            endAngle: outerEndDeg,
            clockwise: false
        )
        cPath.line(to: NSPoint(x: cx + cutX, y: cy - r * sin(thetaInner)))
        cPath.appendArc(
            withCenter: NSPoint(x: cx, y: cy),
            radius: r,
            startAngle: innerEndDeg,
            endAngle: innerStartDeg,
            clockwise: true
        )
        cPath.close()

        let wingLeft = cx + cutX + R * 0.08
        let wingOuterRight = cx + R * 1.16
        let wingInnerRight = cx + R * 0.88
        let midGap = R * 0.095
        let wingOuterY = R * 0.56

        let topWing = NSBezierPath()
        topWing.move(to: NSPoint(x: wingLeft, y: cy + wingOuterY))
        topWing.line(to: NSPoint(x: wingOuterRight, y: cy + wingOuterY))
        topWing.line(to: NSPoint(x: wingInnerRight, y: cy + midGap * 0.5))
        topWing.line(to: NSPoint(x: wingLeft, y: cy + midGap * 0.5))
        topWing.close()

        let bottomWing = NSBezierPath()
        bottomWing.move(to: NSPoint(x: wingLeft, y: cy - midGap * 0.5))
        bottomWing.line(to: NSPoint(x: wingInnerRight, y: cy - midGap * 0.5))
        bottomWing.line(to: NSPoint(x: wingOuterRight, y: cy - wingOuterY))
        bottomWing.line(to: NSPoint(x: wingLeft, y: cy - wingOuterY))
        bottomWing.close()

        let commodoreBlue = NSColor(calibratedRed: 0.09, green: 0.24, blue: 0.58, alpha: 1.0)
        let commodoreRed = NSColor(calibratedRed: 0.85, green: 0.14, blue: 0.16, alpha: 1.0)

        ctx.saveGState()
        let logoShadow = NSShadow()
        logoShadow.shadowColor = NSColor(white: 0.0, alpha: 0.22)
        logoShadow.shadowOffset = NSSize(width: 0, height: -size * 0.012)
        logoShadow.shadowBlurRadius = size * 0.02
        logoShadow.set()

        commodoreBlue.setFill()
        cPath.fill()
        topWing.fill()

        commodoreRed.setFill()
        bottomWing.fill()
        ctx.restoreGState()

        return img
    }
}
