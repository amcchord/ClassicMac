import CoreGraphics

// Raw Mac OS 9.2.1 Appearance Manager measurements.
// Independent captures and rectangles: Fixtures/Platinum/os9-window-reference.json.
extension MeasuredChrome {
    static let WINDOW_CLOSE_PRESSED = [
        "888888888888c",
        "822222222222f",
        "824455667782f",
        "824556677882f",
        "825566778892f",
        "825667788992f",
        "826677889992f",
        "826778899a92f",
        "82778899aa92f",
        "8278899aab92f",
        "828899999992f",
        "822222222222f",
        "cffffffffffff",
    ]
    static let WINDOW_ZOOM_PRESSED = [
        "888888888888c",
        "822222222222f",
        "824455627782f",
        "824556627882f",
        "825566728892f",
        "825667728992f",
        "826677829992f",
        "822222229a92f",
        "82778899aa92f",
        "8278899aab92f",
        "828899999992f",
        "822222222222f",
        "cffffffffffff",
    ]
    static let WINDOW_COLLAPSE_PRESSED = [
        "888888888888c",
        "822222222222f",
        "824455667782f",
        "824556677882f",
        "825566778892f",
        "822222222222f",
        "826677889992f",
        "822222222222f",
        "82778899aa92f",
        "8278899aab92f",
        "828899999992f",
        "822222222222f",
        "cffffffffffff",
    ]
    static let WINDOW_GROW = [
        "0000000000000000f",
        "0ffffffffffffffff",
        "0fccccccccccccccc",
        "0fccccccccccccccc",
        "0fcccccccffcccccc",
        "0fccccccfc7cccccc",
        "0fcccccfc7cffcccc",
        "0fccccfc7cfc7cccc",
        "0fcccfc7cfc7cffcc",
        "0fccfc7cfc7cfc7cc",
        "0fcca7cfc7cfc7ccc",
        "0fccccfc7cfc7cccc",
        "0fcccca7cfc7ccccc",
        "0fccccccfc7cccccc",
        "0fcccccca7ccccccc",
        "0fccccccccccccccc",
        "ffccccccccccccccc",
    ]
    static let WINDOW_GROW_INACTIVE = [
        "5555555555555555d",
        "5dddddddddddddddd",
        "5dddddddddddddddd",
        "5dddddddddddddddd",
        "5dddddddddddddddd",
        "5dddddddddddddddd",
        "5dddddddddddddddd",
        "5dddddddddddddddd",
        "5dddddddddddddddd",
        "5dddddddddddddddd",
        "5dddddddddddddddd",
        "5dddddddddddddddd",
        "5dddddddddddddddd",
        "5dddddddddddddddd",
        "5dddddddddddddddd",
        "5dddddddddddddddd",
        "ddddddddddddddddd",
    ]
    static let SMALL_BEVEL_NORMAL: [(String,String,String)] = [
        ("66", "6", "65"),
        ("6f", "f", "c3"),
        ("6f", "c", "83"),
        ("6c", "8", "83"),
        ("53", "3", "33"),
    ]
    static let SMALL_BEVEL_PRESSED: [(String,String,String)] = [
        ("11", "1", "12"),
        ("15", "5", "74"),
        ("15", "8", "a4"),
        ("17", "a", "a4"),
        ("24", "4", "44"),
    ]
    static let SMALL_BEVEL_DISABLED: [(String,String,String)] = [
        ("aa", "a", "aa"),
        ("ad", "d", "d9"),
        ("ad", "d", "d9"),
        ("ad", "d", "d9"),
        ("a9", "9", "99"),
    ]
}
