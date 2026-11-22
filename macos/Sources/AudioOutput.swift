import AVFoundation
import AmigaCore

final class AmigaAudioOutput {
    private let engine = AVAudioEngine()
    private var sourceNode: AVAudioSourceNode!
    private var scratch = [Float](repeating: 0, count: 4096 * 2)
    private var lastL: Float = 0
    private var lastR: Float = 0

    init(machine: OpaquePointer) {
        let format = AVAudioFormat(standardFormatWithSampleRate: 48000, channels: 2)!

        sourceNode = AVAudioSourceNode(format: format) { [weak self] _, _, frameCount, audioBufferList -> OSStatus in
            guard let self = self else { return noErr }
            let frames = Int(frameCount)
            if self.scratch.count < frames * 2 {
                self.scratch = [Float](repeating: 0, count: frames * 2)
            }

            let got = self.scratch.withUnsafeMutableBufferPointer { buf in
                Int(amiga_read_audio(machine, buf.baseAddress, frames))
            }

            let abl = UnsafeMutableAudioBufferListPointer(audioBufferList)
            if abl.count == 2,
               let outL = abl[0].mData?.assumingMemoryBound(to: Float.self),
               let outR = abl[1].mData?.assumingMemoryBound(to: Float.self) {
                for i in 0..<got {
                    outL[i] = self.scratch[i * 2]
                    outR[i] = self.scratch[i * 2 + 1]
                }
                if got > 0 {
                    self.lastL = outL[got - 1]
                    self.lastR = outR[got - 1]
                }
                for i in got..<frames {
                    self.lastL *= 0.995
                    self.lastR *= 0.995
                    outL[i] = self.lastL
                    outR[i] = self.lastR
                }
            }
            return noErr
        }

        engine.attach(sourceNode)
        engine.connect(sourceNode, to: engine.mainMixerNode, format: format)
        try? engine.start()
    }

    func stop() {
        engine.stop()
    }
}
