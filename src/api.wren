foreign class Bitmap {
    construct create(width, height) {}

    foreign f_set(x, y, color)

    set(x, y, color) {
        if (color is Color) {
            f_set(x, y, color.rgb)
        } else {
            f_set(x, y, color)
        }
    }

    foreign width
    foreign height
}

class Color {
    construct new(r, g, b) {
        _r = r
        _g = g
        _b = b
        _a = 255
    }

    construct new(r, g, b, a) {
        _r = r
        _g = g
        _b = b
        _a = a
    }

    rgb {
        return a << 24 | r << 16 | g << 8 | b
    }

    a { _a }
    r { _r }
    g { _g }
    b { _b }
}

class Window {
    foreign static init(title, width, height)
    foreign static quit()
    foreign static update(bitmap)
    foreign static poll()
}
