import Foundation
import CryptoKit
import Darwin

// A bounded HTTP transfer with durable partial files. A Range response must
// agree with the local offset and catalog size before any bytes are appended.
// Servers that ignore Range restart cleanly. The final SHA-256 remains the
// authority even if the server changed while a download was paused.
final class MachineDownloadTransfer: NSObject, URLSessionDataDelegate, @unchecked Sendable {
    private let source: URL
    private let destination: URL
    private let expectedBytes: Int64?
    private let maximumBytes: Int64
    private let progress: (Int64) -> Void
    private let configuration: URLSessionConfiguration
    private let lock = NSLock()
    private var continuation: CheckedContinuation<URL, Error>?
    private var session: URLSession?
    private var task: URLSessionDataTask?
    private var cancelled = false
    // Remaining fields are used only on the serial delegate queue.
    private var output: FileHandle?
    private var received: Int64 = 0
    private var requestedOffset: Int64 = 0
    private var acceptedResponse = false
    private var failure: Error?

    init(source: URL, destination: URL, expectedBytes: Int64?, maximumBytes: Int64,
         configuration: URLSessionConfiguration = .ephemeral,
         progress: @escaping (Int64) -> Void = { _ in }) {
        self.source = source
        self.destination = destination
        self.expectedBytes = expectedBytes
        self.maximumBytes = maximumBytes
        self.configuration = configuration
        self.progress = progress
    }

    func run() async throws -> URL {
        guard DownloadableMachine.secureURL(source) else {
            throw MachineDownloadError.network("The server must use a secure HTTPS address.")
        }
        return try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { continuation in
                lock.lock()
                if cancelled {
                    lock.unlock()
                    continuation.resume(throwing: CancellationError())
                    return
                }
                self.continuation = continuation
                let queue = OperationQueue()
                queue.maxConcurrentOperationCount = 1
                configuration.urlCache = nil
                configuration.requestCachePolicy = .reloadIgnoringLocalCacheData
                configuration.timeoutIntervalForRequest = 45
                configuration.timeoutIntervalForResource = 24 * 60 * 60
                let session = URLSession(configuration: configuration, delegate: self, delegateQueue: queue)
                self.session = session
                var request = URLRequest(url: source)
                request.setValue("identity", forHTTPHeaderField: "Accept-Encoding")
                // Catalogs are always fetched fresh; only archive files resume.
                if let expectedBytes, let values = try? destination.resourceValues(
                    forKeys: [.fileSizeKey, .isRegularFileKey, .isSymbolicLinkKey]
                ), values.isRegularFile == true, values.isSymbolicLink != true,
                   let size = values.fileSize, size > 0, Int64(size) < expectedBytes {
                    requestedOffset = Int64(size)
                    request.setValue("bytes=\(size)-", forHTTPHeaderField: "Range")
                }
                let task = session.dataTask(with: request)
                self.task = task
                lock.unlock()
                task.resume()
            }
        } onCancel: {
            self.cancel()
        }
    }

    func cancel() {
        lock.lock()
        cancelled = true
        let task = task
        lock.unlock()
        task?.cancel()
    }

    func urlSession(_ session: URLSession, task: URLSessionTask,
                    willPerformHTTPRedirection response: HTTPURLResponse,
                    newRequest request: URLRequest,
                    completionHandler: @escaping (URLRequest?) -> Void) {
        // No HTTPS downgrades or surprise credentials/third-party requests.
        guard let url = request.url, DownloadableMachine.secureURL(url),
              url.host?.lowercased() == source.host?.lowercased(),
              (url.port ?? 443) == (source.port ?? 443) else {
            failure = MachineDownloadError.network("The server redirected to an unexpected address.")
            completionHandler(nil)
            return
        }
        completionHandler(request)
    }

    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask,
                    didReceive response: URLResponse,
                    completionHandler: @escaping (URLSession.ResponseDisposition) -> Void) {
        do {
            guard let response = response as? HTTPURLResponse else {
                throw MachineDownloadError.network("The server returned an unreadable response.")
            }
            guard response.value(forHTTPHeaderField: "Content-Encoding").map({
                $0.lowercased() == "identity"
            }) ?? true else {
                throw MachineDownloadError.network("The server changed the download encoding.")
            }
            switch response.statusCode {
            case 200:
                received = 0
            case 206:
                guard requestedOffset > 0, let expectedBytes,
                      response.value(forHTTPHeaderField: "Content-Range") ==
                        "bytes \(requestedOffset)-\(expectedBytes - 1)/\(expectedBytes)" else {
                    throw MachineDownloadError.network("The server could not resume this download. Remove the partial download and retry.")
                }
                received = requestedOffset
            default:
                throw MachineDownloadError.network("The server returned HTTP \(response.statusCode). Try again later.")
            }
            if response.expectedContentLength >= 0 {
                guard response.expectedContentLength <= maximumBytes - received,
                      expectedBytes.map({ response.expectedContentLength == $0 - received }) ?? true else {
                    throw MachineDownloadError.network("The file size doesn't match the catalog. Reload the catalog and retry.")
                }
            }
            // Never follow a cache-file symlink supplied by another process.
            if let values = try? destination.resourceValues(forKeys: [.isSymbolicLinkKey, .isRegularFileKey]) {
                guard values.isSymbolicLink != true, values.isRegularFile == true else {
                    throw MachineDownloadError.network("The saved download is not a regular file. Remove it and retry.")
                }
            }
            let descriptor = open(destination.path, O_WRONLY | O_CREAT | O_NOFOLLOW, 0o600)
            guard descriptor >= 0 else {
                throw MachineDownloadError.network("The download folder isn't writable.")
            }
            var fileInfo = stat()
            guard fstat(descriptor, &fileInfo) == 0,
                  fileInfo.st_mode & S_IFMT == S_IFREG,
                  received == 0 || fileInfo.st_size == received else {
                close(descriptor)
                throw MachineDownloadError.network("The saved download changed. Remove it and retry.")
            }
            let output = FileHandle(fileDescriptor: descriptor, closeOnDealloc: true)
            self.output = output
            try output.truncate(atOffset: UInt64(received))
            try output.seek(toOffset: UInt64(received))
            acceptedResponse = true
            progress(received)
            completionHandler(.allow)
        } catch {
            failure = error
            completionHandler(.cancel)
        }
    }

    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive data: Data) {
        guard failure == nil, acceptedResponse else { return }
        do {
            guard Int64(data.count) <= maximumBytes - received else {
                throw MachineDownloadError.network("The server sent more data than expected.")
            }
            try output?.write(contentsOf: data)
            received += Int64(data.count)
            progress(received)
        } catch {
            failure = error
            dataTask.cancel()
        }
    }

    func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        do { try output?.close() } catch { failure = failure ?? error }
        output = nil
        lock.lock()
        let continuation = continuation
        self.continuation = nil
        let wasCancelled = cancelled
        self.task = nil
        self.session = nil
        lock.unlock()
        session.finishTasksAndInvalidate()
        guard let continuation else { return }
        if wasCancelled { continuation.resume(throwing: CancellationError()) }
        else if let failure { continuation.resume(throwing: failure) }
        else if let error { continuation.resume(throwing: error) }
        else if !acceptedResponse || expectedBytes.map({ received != $0 }) == true {
            continuation.resume(throwing: MachineDownloadError.network("The file was incomplete. Retry to continue the download."))
        } else { continuation.resume(returning: destination) }
    }

    static func verify(_ file: URL, machine: DownloadableMachine,
                       cancelled: () -> Bool = { false },
                       progress: (Int64) -> Void = { _ in }) throws {
        let values = try file.resourceValues(forKeys: [.fileSizeKey, .isRegularFileKey, .isSymbolicLinkKey])
        guard values.isRegularFile == true, values.isSymbolicLink != true,
              values.fileSize.map(Int64.init) == machine.archiveBytes else {
            throw MachineDownloadError.integrity
        }
        let input = try FileHandle(forReadingFrom: file)
        defer { try? input.close() }
        var hash = SHA256()
        var count: Int64 = 0
        while let chunk = try input.read(upToCount: 1_048_576), !chunk.isEmpty {
            if cancelled() { throw CancellationError() }
            hash.update(data: chunk)
            count += Int64(chunk.count)
            progress(count)
        }
        guard count == machine.archiveBytes,
              hash.finalize().map({ String(format: "%02x", $0) }).joined() == machine.sha256 else {
            throw MachineDownloadError.integrity
        }
    }
}

// Used by background hashing/extraction without depending on a UI actor.
final class MachineDownloadCancellation: @unchecked Sendable {
    private let lock = NSLock()
    private var value = false
    var isCancelled: Bool { lock.lock(); defer { lock.unlock() }; return value }
    func cancel() { lock.lock(); value = true; lock.unlock() }
}
