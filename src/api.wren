foreign class Color {
    foreign construct new(r, g, b, a)
    foreign construct new(r, g, b)
    foreign construct new(num)

    foreign r
    foreign g
    foreign b
    foreign a

    foreign r=(v)
    foreign g=(v)
    foreign b=(v)
    foreign a=(v)

    toString {
        return "Color (r: %(r), g: %(g), b: %(b), a: %(a))"
    }

    static none { new(0, 0, 0, 0) }
    static black { new(0, 0, 0) }
    static darkBlue { new(29, 43, 83) }
    static darkPurple { new(126, 37, 83) }
    static darkGreen { new(0, 135, 81) }
    static brown { new(171, 82, 54) }
    static darkGray { new(95, 87, 79) }
    static lightGray { new(194, 195, 199) }
    static white { new(255, 241, 232) }
    static red { new(255, 0, 77) }
    static orange { new(255, 163, 0) }
    static yellow { new(255, 236, 39) }
    static green { new(0, 228, 54) }
    static blue { new(41, 173, 255) }
    static indigo { new(131, 118, 156) }
    static pink { new(255, 119, 168) }
    static peach { new(255, 204, 170) }
}

foreign class Image {
    foreign construct new(width, height)
    foreign construct new(pathOrImage)

    foreign width
    foreign height

    toString {
        return "Image (width: %(width), height: %(height))"
    }

    foreign clip(x, y, width, height)

    clip() {
        clip(0, 0, -1, -1)
    }

    //foreign save(path)
    //foreign resize(width, height)

    foreign f_get(x, y)

    get(x, y) { Color.new(f_get(x, y)) }

    foreign set(x, y, color)
    foreign clear(color)
    foreign fill(x, y, width, height, color)
    foreign line(x0, y0, x1, y1, color)
    foreign rect(x, y, width, height, color)
    foreign fillRect(x, y, width, height, color)
    foreign circle(x, y, radius, color)
    foreign fillCircle(x, y, radius, color)
    foreign print(text, x, y, color)

    foreign blit(image, dx, dy, sx, sy, width, height)

    blit(image, x, y) {
        blit(image, x, y, 0, 0, image.width, image.height)
    }

    foreign blitAlpha(image, dx, dy, sx, sy, width, height, alpha)

    blitAlpha(image, x, y) {
        blitAlpha(image, x, y, 0, 0, image.width, image.height, 1)
    }

    blitAlpha(image, x, y, alpha) {
        blitAlpha(image, x, y, 0, 0, image.width, image.height, alpha)
    }

    blitAlpha(image, dx, dy, sx, sy, width, height) {
        blitAlpha(image, dx, dy, sx, sy, width, height, 1)
    }

    foreign blitTint(image, dx, dy, sx, sy, width, height, tint)

    blitTint(image, x, y, tint) {
        blitTint(image, x, y, 0, 0, image.width, image.height, tint)
    }
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

class Window {
    foreign static init(title, width, height)
    foreign static quit()
    foreign static update(image)
    foreign static keyHeld(key)
    foreign static keyPressed(key)
    foreign static mouseHeld(button)
    foreign static mousePressed(button)

    foreign static width
    foreign static height
    foreign static title
    foreign static closed
    foreign static mouseX
    foreign static mouseY

    foreign static integerScaling
    foreign static integerScaling=(v)

    foreign static time()
    foreign static targetFps=(v)
}
