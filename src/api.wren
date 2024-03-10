foreign class Bitmap {
    construct create(width, height) {}

    foreign f_set(x, y, color)
    foreign f_clear(color)

    set(x, y, color) {
        if (color is Color) {
            f_set(x, y, color.toNum)
        } else {
            f_set(x, y, color)
        }
    }

    clear(color) {
        if (color is Color) {
            f_clear(color.toNum)
        } else {
            f_clear(color)
        }
    }

    foreign width
    foreign height
}

class Color {
    construct new(r, g, b) {
        init_(r, g, b, 255)
    }

    construct new(r, g, b, a) {
        init_(r, g, b, a)
    }

    init_(r, g, b, a) {
        if (r < 0 || r > 255) Fiber.abort("Red channel out of range")
        if (g < 0 || g > 255) Fiber.abort("Green channel out of range")
        if (b < 0 || b > 255) Fiber.abort("Blue channel out of range")
        if (a < 0 || a > 255) Fiber.abort("Alpha channel out of range")

        _r = r
        _g = g
        _b = b
        _a = a
    }

    toNum { a << 24 | r << 16 | g << 8 | b }
    static fromNum(v) {
        var r = v & 0xFF
        var g = (v >> 8) & 0xFF
        var b = (v >> 16) & 0xFF
        var a = (v >> 24) & 0xFF
        return Color.new(r, g, b, a)
    }

    toString { "Color (" + r.toString + ", " + g.toString + ", " + b.toString + ", " + a.toString + ")" }

    ==(other) {
        if (other is Color) {
            return other.r == r && other.g == g && other.b == b && other.a == a
        } else {
            return false
        }
    }

    !=(other) {
        if (other is Color) {
            return other.r != r || other.g != g || other.b != b || other.a != a
        } else {
            return true
        }
    }

    r { _r }
    g { _g }
    b { _b }
    a { _a }

    r=(v) {
        if (v < 0 || v > 255) Fiber.abort("Red channel out of range")
        _r = v
    }
    g=(v) {
        if (v < 0 || v > 255) Fiber.abort("Green channel out of range")
        _g = v
    }
    b=(v) {
        if (v < 0 || v > 255) Fiber.abort("Blue channel out of range")
        _b = v
    }
    a=(v) {
        if (v < 0 || v > 255) Fiber.abort("Alpha channel out of range")
        _a = v
    }

    static none { Color.new(0, 0, 0, 0) }
    static black { Color.new(0, 0, 0) }
    static darkBlue { Color.new(29, 43, 83) }
    static darkPurple { Color.new(126, 37, 83) }
    static darkGreen { Color.new(0, 135, 81) }
    static brown { Color.new(171, 82, 54) }
    static darkGray { Color.new(95, 87, 79) }
    static lightGray { Color.new(194, 195, 199) }
    static white { Color.new(255, 241, 232) }
    static red { Color.new(255, 0, 77) }
    static orange { Color.new(255, 163, 0) }
    static yellow { Color.new(255, 236, 39) }
    static green { Color.new(0, 228, 54) }
    static blue { Color.new(41, 173, 255) }
    static indigo { Color.new(131, 118, 156) }
    static pink { Color.new(255, 119, 168) }
    static peach { Color.new(255, 204, 170) }
}

class OS {
    foreign static name
    foreign static basilVersion
    foreign static args
    foreign static f_exit(code)

    static exit(code) {
        f_exit(code)
        Fiber.suspend()
    }

    static exit() {
        exit(0)
    }
}

foreign class Timer {
    construct new() {}

    foreign tick()
    foreign tick(framerate)

    foreign time
    foreign delta
}

class Window {
    foreign static init(title, width, height)
    foreign static quit()
    foreign static poll()
    foreign static update(bitmap)

    foreign static width
    foreign static height
    foreign static title
}
