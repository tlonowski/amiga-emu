import Foundation
import GameController

/// Polls and monitors connected USB / Bluetooth game controllers via Apple's GameController framework
/// and translates D-pad / analog stick + action buttons into Amiga JoyPort (Port 2) directions + Fire.
final class AmigaGameControllerManager {
    struct JoyState: Equatable {
        var up = false
        var down = false
        var left = false
        var right = false
        var fire = false
    }

    var onStateChanged: ((JoyState) -> Void)?
    var onControllersChanged: ((Int, String?) -> Void)?

    private(set) var currentState = JoyState()
    private(set) var connectedCount: Int = 0
    private(set) var primaryControllerName: String?

    private let deadzone: Float = 0.35
    private var observers: [NSObjectProtocol] = []

    func start() {
        let nc = NotificationCenter.default
        observers.append(nc.addObserver(forName: .GCControllerDidConnect, object: nil, queue: .main) { [weak self] _ in
            self?.refreshControllers()
        })
        observers.append(nc.addObserver(forName: .GCControllerDidDisconnect, object: nil, queue: .main) { [weak self] _ in
            self?.refreshControllers()
        })
        GCController.startWirelessControllerDiscovery(completionHandler: nil)
        refreshControllers()
    }

    func stop() {
        GCController.stopWirelessControllerDiscovery()
        for obs in observers {
            NotificationCenter.default.removeObserver(obs)
        }
        observers.removeAll()
    }

    deinit {
        stop()
    }

    func refreshControllers() {
        let controllers = GCController.controllers()
        connectedCount = controllers.count
        primaryControllerName = controllers.first?.vendorName ?? controllers.first?.productCategory

        for controller in controllers {
            if let ext = controller.extendedGamepad {
                ext.valueChangedHandler = { [weak self] _, _ in
                    self?.recomputeState()
                }
            } else if let micro = controller.microGamepad {
                micro.valueChangedHandler = { [weak self] _, _ in
                    self?.recomputeState()
                }
            }
        }
        recomputeState()
        onControllersChanged?(connectedCount, primaryControllerName)
    }

    func recomputeState() {
        var next = JoyState()
        for controller in GCController.controllers() {
            if let ext = controller.extendedGamepad {
                if ext.dpad.up.isPressed || ext.leftThumbstick.yAxis.value > deadzone { next.up = true }
                if ext.dpad.down.isPressed || ext.leftThumbstick.yAxis.value < -deadzone { next.down = true }
                if ext.dpad.left.isPressed || ext.leftThumbstick.xAxis.value < -deadzone { next.left = true }
                if ext.dpad.right.isPressed || ext.leftThumbstick.xAxis.value > deadzone { next.right = true }
                if ext.buttonA.isPressed || ext.buttonB.isPressed || ext.buttonX.isPressed || ext.rightTrigger.isPressed {
                    next.fire = true
                }
            } else if let micro = controller.microGamepad {
                if micro.dpad.up.isPressed || micro.dpad.yAxis.value > deadzone { next.up = true }
                if micro.dpad.down.isPressed || micro.dpad.yAxis.value < -deadzone { next.down = true }
                if micro.dpad.left.isPressed || micro.dpad.xAxis.value < -deadzone { next.left = true }
                if micro.dpad.right.isPressed || micro.dpad.xAxis.value > deadzone { next.right = true }
                if micro.buttonA.isPressed || micro.buttonX.isPressed {
                    next.fire = true
                }
            }
        }
        if next != currentState {
            currentState = next
            onStateChanged?(next)
        }
    }
}
