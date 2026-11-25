// Inspect the installed app's native windows without reading other windows'
// titles, capturing the screen, or requiring Accessibility permission.
import AppKit
import CoreGraphics

func fail(_ message: String) -> Never {
    FileHandle.standardError.write(Data((message + "\n").utf8))
    exit(1)
}

let arguments = CommandLine.arguments
guard arguments.count == 3, let pid = Int32(arguments[2]) else {
    fail("Expected frame or quit followed by an application PID.")
}
if arguments[1] == "quit" {
    guard let app = NSRunningApplication(processIdentifier: pid), app.terminate() else {
        fail("The installed application did not accept a normal quit request.")
    }
    exit(0)
}
guard arguments[1] == "frame",
      let windows = CGWindowListCopyWindowInfo([.optionOnScreenOnly, .excludeDesktopElements],
                                               kCGNullWindowID) as? [[String: Any]] else {
    fail("Could not get the WindowServer window list.")
}
var frames: [String] = []
for window in windows {
    guard (window[kCGWindowOwnerPID as String] as? NSNumber)?.int32Value == pid,
          (window[kCGWindowLayer as String] as? NSNumber)?.intValue == 0,
          let number = window[kCGWindowNumber as String] as? NSNumber,
          let bounds = window[kCGWindowBounds as String] as? [String: NSNumber],
          let x = bounds["X"], let y = bounds["Y"],
          let width = bounds["Width"], let height = bounds["Height"],
          width.doubleValue > 0, height.doubleValue > 0 else { continue }
    frames.append("\(number) \(x) \(y) \(width) \(height)")
}
guard frames.count == 1 else { fail("Expected one native writer window. Found \(frames).") }
print(frames[0])
