import AppKit
import Metal
import MetalKit
import AmigaCore

final class AmigaMetalView: MTKView, MTKViewDelegate {
    private var commandQueue: MTLCommandQueue!
    private var pipelineState: MTLRenderPipelineState!
    private var screenTexture: MTLTexture!

    private let machine: OpaquePointer
    private let fbWidth = Int(amiga_fb_width())
    private let fbHeight = Int(amiga_fb_height())
    private var lastFrameTime: CFTimeInterval = 0
    private var frameAcc: Double = 0
    private var didPrebuffer: Bool = false
    var warpMode: Bool = false
    var joystickMode: Bool = false
    var stretchWidescreen: Bool = false
    var onMouseGrabChanged: ((Bool) -> Void)?
    private var mouseEventMonitor: Any?
    var mouseGrabbed: Bool = false {
        didSet {
            guard mouseGrabbed != oldValue else { return }
            if mouseGrabbed {
                if let src = CGEventSource(stateID: .hidSystemState) {
                    src.localEventsSuppressionInterval = 0.0
                }
                warpCursorToCenter()
                CGAssociateMouseAndMouseCursorPosition(boolean_t(0))
                NSCursor.hide()
                if mouseEventMonitor == nil {
                    mouseEventMonitor = NSEvent.addLocalMonitorForEvents(
                        matching: [.mouseMoved, .leftMouseDragged, .rightMouseDragged]
                    ) { [weak self] event in
                        guard let self = self, self.mouseGrabbed else { return event }
                        self.sendMouseDelta(event)
                        return event
                    }
                }
            } else {
                if let monitor = mouseEventMonitor {
                    NSEvent.removeMonitor(monitor)
                    mouseEventMonitor = nil
                }
                CGAssociateMouseAndMouseCursorPosition(boolean_t(1))
                NSCursor.unhide()
            }
            onMouseGrabChanged?(mouseGrabbed)
        }
    }

    private func warpCursorToCenter() {
        guard let win = window, let screen = win.screen ?? NSScreen.main else { return }
        let viewRect = convert(bounds, to: nil)
        let winRect = win.convertToScreen(viewRect)
        let screenTop = NSScreen.screens.first?.frame.maxY ?? screen.frame.maxY
        let centerQuartz = CGPoint(x: winRect.midX, y: screenTop - winRect.midY)
        CGWarpMouseCursorPosition(centerQuartz)
    }

    private var joyUp = false
    private var joyDown = false
    private var joyLeft = false
    private var joyRight = false
    private var joyFire = false
    private var mouseLeft = false
    private var mouseRight = false
    private var mouseAccX: Double = 0
    private var mouseAccY: Double = 0
    private var prevModifiers: NSEvent.ModifierFlags = []

    override var acceptsFirstResponder: Bool { true }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        for area in trackingAreas {
            removeTrackingArea(area)
        }
        let area = NSTrackingArea(
            rect: bounds,
            options: [.mouseMoved, .activeInKeyWindow, .inVisibleRect],
            owner: self,
            userInfo: nil
        )
        addTrackingArea(area)
    }

    private func sendMouseDelta(_ event: NSEvent) {
        mouseAccX += event.deltaX
        mouseAccY += event.deltaY
        let dx = Int32(mouseAccX.rounded(.towardZero))
        let dy = Int32(mouseAccY.rounded(.towardZero))
        if dx != 0 || dy != 0 {
            mouseAccX -= Double(dx)
            mouseAccY -= Double(dy)
            let clampedDx = max(-60, min(60, dx))
            let clampedDy = max(-60, min(60, dy))
            amiga_mouse_move(machine, clampedDx, clampedDy)
        }
    }

    override func mouseMoved(with event: NSEvent) {
        if !mouseGrabbed {
            sendMouseDelta(event)
        }
    }

    override func mouseDragged(with event: NSEvent) {
        if !mouseGrabbed {
            sendMouseDelta(event)
        }
    }

    override func rightMouseDragged(with event: NSEvent) {
        if !mouseGrabbed {
            sendMouseDelta(event)
        }
    }

    override func mouseDown(with event: NSEvent) {
        window?.makeFirstResponder(self)
        if !mouseGrabbed {
            mouseGrabbed = true
        }
        mouseLeft = true
        amiga_mouse_buttons(machine, 1, mouseRight ? 1 : 0)
    }

    override func mouseUp(with event: NSEvent) {
        mouseLeft = false
        amiga_mouse_buttons(machine, 0, mouseRight ? 1 : 0)
    }

    override func rightMouseDown(with event: NSEvent) {
        window?.makeFirstResponder(self)
        mouseRight = true
        amiga_mouse_buttons(machine, mouseLeft ? 1 : 0, 1)
    }

    override func rightMouseUp(with event: NSEvent) {
        mouseRight = false
        amiga_mouse_buttons(machine, mouseLeft ? 1 : 0, 0)
    }

    func resetJoystickState() {
        joyUp = false
        joyDown = false
        joyLeft = false
        joyRight = false
        joyFire = false
        pushJoystickState()
    }

    private func pushJoystickState() {
        amiga_set_joystick(
            machine,
            joyUp ? 1 : 0,
            joyDown ? 1 : 0,
            joyLeft ? 1 : 0,
            joyRight ? 1 : 0,
            joyFire ? 1 : 0
        )
    }

    private func macKeyCodeToAmigaRawKey(_ code: UInt16) -> UInt8? {
        switch code {
        case 50: return 0x00 // ` ~
        case 18: return 0x01 // 1
        case 19: return 0x02 // 2
        case 20: return 0x03 // 3
        case 21: return 0x04 // 4
        case 23: return 0x05 // 5
        case 22: return 0x06 // 6
        case 26: return 0x07 // 7
        case 28: return 0x08 // 8
        case 25: return 0x09 // 9
        case 29: return 0x0A // 0
        case 27: return 0x0B // -
        case 24: return 0x0C // =
        case 42: return 0x0D // \
        case 12: return 0x10 // Q
        case 13: return 0x11 // W
        case 14: return 0x12 // E
        case 15: return 0x13 // R
        case 17: return 0x14 // T
        case 16: return 0x15 // Y
        case 32: return 0x16 // U
        case 34: return 0x17 // I
        case 31: return 0x18 // O
        case 35: return 0x19 // P
        case 33: return 0x1A // [
        case 30: return 0x1B // ]
        case 0:  return 0x20 // A
        case 1:  return 0x21 // S
        case 2:  return 0x22 // D
        case 3:  return 0x23 // F
        case 5:  return 0x24 // G
        case 4:  return 0x25 // H
        case 38: return 0x26 // J
        case 40: return 0x27 // K
        case 37: return 0x28 // L
        case 41: return 0x29 // ;
        case 39: return 0x2A // '
        case 6:  return 0x31 // Z
        case 7:  return 0x32 // X
        case 8:  return 0x33 // C
        case 9:  return 0x34 // V
        case 11: return 0x35 // B
        case 45: return 0x36 // N
        case 46: return 0x37 // M
        case 43: return 0x38 // ,
        case 47: return 0x39 // .
        case 44: return 0x3A // /
        case 49: return 0x40 // Space
        case 51: return 0x41 // Backspace
        case 48: return 0x42 // Tab
        case 76: return 0x43 // Numpad Enter
        case 36: return 0x44 // Return
        case 53: return 0x45 // Escape
        case 117: return 0x46 // Forward Delete
        case 126: return 0x4C // Up
        case 125: return 0x4D // Down
        case 124: return 0x4E // Right
        case 123: return 0x4F // Left
        case 122: return 0x50 // F1
        case 120: return 0x51 // F2
        case 99:  return 0x52 // F3
        case 118: return 0x53 // F4
        case 96:  return 0x54 // F5
        case 97:  return 0x55 // F6
        case 98:  return 0x56 // F7
        case 100: return 0x57 // F8
        case 101: return 0x58 // F9
        case 109: return 0x59 // F10
        case 82: return 0x0F // KP 0
        case 83: return 0x1D // KP 1
        case 84: return 0x1E // KP 2
        case 85: return 0x1F // KP 3
        case 86: return 0x2D // KP 4
        case 87: return 0x2E // KP 5
        case 88: return 0x2F // KP 6
        case 89: return 0x3D // KP 7
        case 91: return 0x3E // KP 8
        case 92: return 0x3F // KP 9
        default: return nil
        }
    }

    override func keyDown(with event: NSEvent) {
        if event.modifierFlags.contains(.command) {
            super.keyDown(with: event)
            return
        }
        if event.isARepeat { return }

        if joystickMode {
            switch event.keyCode {
            case 126: joyUp = true; pushJoystickState(); return
            case 125: joyDown = true; pushJoystickState(); return
            case 123: joyLeft = true; pushJoystickState(); return
            case 124: joyRight = true; pushJoystickState(); return
            default: break
            }
        }
        if let raw = macKeyCodeToAmigaRawKey(event.keyCode) {
            amiga_key_event(machine, raw, 1)
        }
    }

    override func keyUp(with event: NSEvent) {
        if joystickMode {
            switch event.keyCode {
            case 126: joyUp = false; pushJoystickState(); return
            case 125: joyDown = false; pushJoystickState(); return
            case 123: joyLeft = false; pushJoystickState(); return
            case 124: joyRight = false; pushJoystickState(); return
            default: break
            }
        }
        if let raw = macKeyCodeToAmigaRawKey(event.keyCode) {
            amiga_key_event(machine, raw, 0)
        }
    }

    override func flagsChanged(with event: NSEvent) {
        let flags = event.modifierFlags
        let shiftNow = flags.contains(.shift)
        let shiftPrev = prevModifiers.contains(.shift)
        if shiftNow != shiftPrev {
            if joystickMode {
                joyFire = shiftNow
                pushJoystickState()
            } else {
                amiga_key_event(machine, 0x60, shiftNow ? 1 : 0) // Left Shift
            }
        }

        let ctrlNow = flags.contains(.control)
        let ctrlPrev = prevModifiers.contains(.control)
        if ctrlNow != ctrlPrev {
            amiga_key_event(machine, 0x63, ctrlNow ? 1 : 0) // Control
        }

        let optNow = flags.contains(.option)
        let optPrev = prevModifiers.contains(.option)
        if optNow != optPrev {
            amiga_key_event(machine, 0x64, optNow ? 1 : 0) // Left Alt
        }

        prevModifiers = flags
    }

    init(frame: NSRect, machine: OpaquePointer) {
        self.machine = machine
        guard let device = MTLCreateSystemDefaultDevice() else {
            fatalError("Metal is not supported on this Mac.")
        }
        super.init(frame: frame, device: device)
        self.colorPixelFormat = .bgra8Unorm
        self.preferredFramesPerSecond = 60
        self.clearColor = MTLClearColor(red: 0, green: 0, blue: 0, alpha: 1)
        self.delegate = self

        setupMetal(device: device)
    }

    required init(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    private func setupMetal(device: MTLDevice) {
        commandQueue = device.makeCommandQueue()

        let shaderSource = """
        #include <metal_stdlib>
        using namespace metal;

        struct VertexOut {
            float4 position [[position]];
            float2 uv;
        };

        vertex VertexOut amiga_vertex(uint vid [[vertex_id]]) {
            constexpr float2 pos[4] = {
                float2(-1.0, -1.0),
                float2( 1.0, -1.0),
                float2(-1.0,  1.0),
                float2( 1.0,  1.0)
            };
            constexpr float2 uvs[4] = {
                float2(0.0, 1.0),
                float2(1.0, 1.0),
                float2(0.0, 0.0),
                float2(1.0, 0.0)
            };
            VertexOut out;
            out.position = float4(pos[vid], 0.0, 1.0);
            out.uv = uvs[vid];
            return out;
        }

        fragment float4 amiga_fragment(VertexOut in [[stage_in]],
                                       texture2d<float> screenTex [[texture(0)]]) {
            float2 texSize = float2(screenTex.get_width(), screenTex.get_height());
            float2 texel = in.uv * texSize + 0.5;
            float2 i = floor(texel);
            float2 f = fract(texel);
            float2 fw = max(fwidth(texel), float2(1e-4));
            float2 t = clamp((f - 0.5) / fw + 0.5, 0.0, 1.0);
            float2 uv = (i - 0.5 + t) / texSize;
            constexpr sampler s(mag_filter::linear, min_filter::linear, address::clamp_to_edge);
            return screenTex.sample(s, uv);
        }
        """

        let library = try! device.makeLibrary(source: shaderSource, options: nil)

        let desc = MTLRenderPipelineDescriptor()
        desc.vertexFunction = library.makeFunction(name: "amiga_vertex")
        desc.fragmentFunction = library.makeFunction(name: "amiga_fragment")
        desc.colorAttachments[0].pixelFormat = colorPixelFormat
        pipelineState = try! device.makeRenderPipelineState(descriptor: desc)

        let texDesc = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: .bgra8Unorm,
            width: fbWidth,
            height: fbHeight,
            mipmapped: false
        )
        texDesc.usage = [.shaderRead]
        screenTexture = device.makeTexture(descriptor: texDesc)
    }

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}

    func draw(in view: MTKView) {
        let steps: Int
        if warpMode {
            steps = 12
            lastFrameTime = 0
            frameAcc = 0
        } else {
            let now = CACurrentMediaTime()
            if lastFrameTime == 0 {
                lastFrameTime = now
                if !didPrebuffer {
                    didPrebuffer = true
                    steps = 2
                } else {
                    steps = 1
                }
            } else {
                let dt = min(max(now - lastFrameTime, 0), 0.1)
                lastFrameTime = now
                let palFps = 7_093_790.0 / (312.0 * 454.0)
                frameAcc += dt * palFps
                steps = min(Int(frameAcc), 4)
                frameAcc -= Double(steps)
            }
        }
        for _ in 0..<steps {
            amiga_step_frame(machine)
        }

        if let fb = amiga_framebuffer(machine) {
            let region = MTLRegionMake2D(0, 0, fbWidth, fbHeight)
            screenTexture.replace(
                region: region,
                mipmapLevel: 0,
                withBytes: fb,
                bytesPerRow: fbWidth * MemoryLayout<UInt32>.stride
            )
        }

        guard let drawable = currentDrawable,
              let passDesc = currentRenderPassDescriptor,
              let cmdBuf = commandQueue.makeCommandBuffer(),
              let encoder = cmdBuf.makeRenderCommandEncoder(descriptor: passDesc) else {
            return
        }

        let dw = Double(drawableSize.width)
        let dh = Double(drawableSize.height)
        if dw > 0 && dh > 0 {
            if stretchWidescreen {
                encoder.setViewport(MTLViewport(originX: 0.0, originY: 0.0, width: dw, height: dh, znear: 0.0, zfar: 1.0))
            } else {
                let targetAspect = 4.0 / 3.0
                let currentAspect = dw / dh
                var vpW = dw
                var vpH = dh
                var vpX = 0.0
                var vpY = 0.0
                if currentAspect > targetAspect {
                    vpW = dh * targetAspect
                    vpX = (dw - vpW) * 0.5
                } else {
                    vpH = dw / targetAspect
                    vpY = (dh - vpH) * 0.5
                }
                encoder.setViewport(MTLViewport(originX: vpX, originY: vpY, width: vpW, height: vpH, znear: 0.0, zfar: 1.0))
            }
        }

        encoder.setRenderPipelineState(pipelineState)
        encoder.setFragmentTexture(screenTexture, index: 0)
        encoder.drawPrimitives(type: .triangleStrip, vertexStart: 0, vertexCount: 4)
        encoder.endEncoding()

        cmdBuf.present(drawable)
        cmdBuf.commit()
    }
}
